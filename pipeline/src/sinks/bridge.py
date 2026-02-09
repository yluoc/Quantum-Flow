"""C++ bridge sink via Unix-domain datagram socket (cross-process IPC)."""

from __future__ import annotations

import logging
import socket
import struct

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


class CppBridgeSink(Sink):
    """Sink that pushes market data events to the C++ engine over Unix socket."""

    def __init__(self, socket_path: str = _DEFAULT_BRIDGE_SOCKET) -> None:
        self._socket_path = socket_path
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
        self._sock.close()
        logger.info(
            "CppBridgeSink closed - sent=%d dropped=%d socket=%s",
            self._sent,
            self._dropped,
            self._socket_path,
        )
