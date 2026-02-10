"""C++ bridge sink via Unix-domain datagram socket (cross-process IPC)."""

from __future__ import annotations

import importlib
import logging
import os
import socket
import struct
import sys
from pathlib import Path
from typing import Any

from src.normalizer import BookPayload, NormalizedEvent, TradePayload
from src.sinks.base import Sink

logger = logging.getLogger(__name__)

# Quantity conversion: float size -> integer lots
_QTY_SCALE = int(1e8)

# MarketDataPacket wire format (must match common/market_data_packet.hpp layout)
# char symbol[16], uint8 side, uint8 event_type, 6 bytes padding, double price,
# uint64 quantity, uint64 timestamp_ns, uint64 order_id
_PACKET_STRUCT = struct.Struct("<16sBB6xdQQQ")

_DEFAULT_BRIDGE_SOCKET = "/tmp/quantumflow_bridge.sock"


def _native_bridge_sender_cls() -> type[Any] | None:
    """Try to load the native UDS bridge extension class."""
    mode = os.getenv("QF_BRIDGE_MODE", "auto").strip().lower()
    if mode == "python":
        return None

    module_name = "quantumflow_uds_bridge"

    try:
        module = importlib.import_module(module_name)
        return getattr(module, "UdsBridgeSender", None)
    except Exception:
        pass

    here = Path(__file__).resolve()
    repo_root = here.parents[3]
    candidates = (
        repo_root / "build",
        repo_root / "build" / "lib",
        repo_root / "build" / "bin",
    )

    for candidate in candidates:
        if not candidate.exists():
            continue
        path_str = str(candidate)
        if path_str not in sys.path:
            sys.path.append(path_str)
        try:
            module = importlib.import_module(module_name)
            return getattr(module, "UdsBridgeSender", None)
        except Exception:
            continue

    if mode == "native":
        logger.warning("QF_BRIDGE_MODE=native but native bridge module could not be loaded")
    return None


class CppBridgeSink(Sink):
    """Sink that pushes market data events to the C++ engine over Unix socket."""

    def __init__(self, socket_path: str = _DEFAULT_BRIDGE_SOCKET) -> None:
        self._socket_path = socket_path
        self._native_sender: Any | None = None
        self._sock: socket.socket | None = None

        sender_cls = _native_bridge_sender_cls()
        if sender_cls is not None:
            try:
                self._native_sender = sender_cls(socket_path)
                logger.info(
                    "CppBridgeSink using native bridge extension (socket=%s)",
                    socket_path,
                )
            except Exception:
                logger.exception("Failed to init native bridge extension; falling back")

        if self._native_sender is None:
            self._sock = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
            self._sock.setblocking(False)
        self._sent = 0
        self._dropped = 0
        self._warned_missing_socket = False

    @staticmethod
    def _encode_symbol(symbol: str) -> bytes:
        raw = symbol.encode("ascii", errors="ignore")[:15]
        return raw.ljust(16, b"\0")

    def _send_packet(
        self,
        symbol: str,
        side: int,
        event_type: int,
        price: float,
        quantity: int,
        timestamp_ns: int,
        order_id: int = 0,
    ) -> None:
        payload = _PACKET_STRUCT.pack(
            self._encode_symbol(symbol),
            side,
            event_type,
            float(price),
            int(quantity),
            int(timestamp_ns),
            int(order_id),
        )
        if self._sock is None:
            return
        try:
            self._sock.sendto(payload, self._socket_path)
            self._sent += 1
        except FileNotFoundError:
            self._dropped += 1
            if not self._warned_missing_socket:
                logger.warning(
                    "Bridge socket %s not found. Start the C++ engine first.",
                    self._socket_path,
                )
                self._warned_missing_socket = True
        except (BlockingIOError, OSError):
            self._dropped += 1

    async def write(self, event: NormalizedEvent) -> None:
        payload = event.payload
        ts_ns = event.ts_recv_mono_ns

        if self._native_sender is not None:
            if isinstance(payload, BookPayload):
                self._native_sender.send_book(
                    event.symbol,
                    payload.bids,
                    payload.asks,
                    ts_ns,
                    _QTY_SCALE,
                )
                return
            if isinstance(payload, TradePayload):
                side = 0 if payload.side == "buy" else 1
                self._native_sender.send_trade(
                    event.symbol,
                    side,
                    payload.price,
                    payload.size,
                    ts_ns,
                    0,
                    _QTY_SCALE,
                )
                return

        if isinstance(payload, BookPayload):
            # Push each bid level
            for level in payload.bids:
                self._send_packet(
                    symbol=event.symbol,
                    side=0,  # buy
                    event_type=0,  # book_level
                    price=level.price,
                    quantity=int(level.size * _QTY_SCALE),
                    timestamp_ns=ts_ns,
                )
            # Push each ask level
            for level in payload.asks:
                self._send_packet(
                    symbol=event.symbol,
                    side=1,  # sell
                    event_type=0,  # book_level
                    price=level.price,
                    quantity=int(level.size * _QTY_SCALE),
                    timestamp_ns=ts_ns,
                )

        elif isinstance(payload, TradePayload):
            side = 0 if payload.side == "buy" else 1
            self._send_packet(
                symbol=event.symbol,
                side=side,
                event_type=1,  # trade
                price=payload.price,
                quantity=int(payload.size * _QTY_SCALE),
                timestamp_ns=ts_ns,
            )

    async def close(self) -> None:
        if self._native_sender is not None:
            try:
                native_stats = self._native_sender.stats()
            except Exception:
                native_stats = None
            try:
                self._native_sender.close()
            except Exception:
                logger.exception("Error closing native bridge sender")

        if self._sock is not None:
            self._sock.close()
        if self._native_sender is not None and native_stats is not None:
            logger.info(
                "CppBridgeSink closed (native) - sent=%s dropped=%s socket=%s",
                native_stats.get("sent"),
                native_stats.get("dropped"),
                native_stats.get("socket_path"),
            )
        else:
            logger.info(
                "CppBridgeSink closed (python fallback) - sent=%d dropped=%d socket=%s",
                self._sent,
                self._dropped,
                self._socket_path,
            )
