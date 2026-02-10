"""Normalize OKX WebSocket messages to a standard event format."""

from __future__ import annotations

import time
from typing import Any

import msgspec

_DEBUG = True


class BookLevel(msgspec.Struct, frozen=True):
    """Single order book level: (price, size, count)."""

    price: float
    size: float
    count: int


class BookPayload(msgspec.Struct, frozen=True):
    """Payload for book_topn events."""

    n: int
    best_bid: float
    best_ask: float
    bids: list[BookLevel]
    asks: list[BookLevel]


class TradePayload(msgspec.Struct, frozen=True):
    """Payload for trade events."""

    price: float
    size: float
    side: str
    trade_id: str | None


class NormalizedEvent(msgspec.Struct, frozen=True):
    """Normalized market data event."""

    exchange: str
    symbol: str
    channel: str
    event_type: str
    ts_exchange_ms: int
    ts_recv_epoch_ms: int
    ts_recv_mono_ns: int
    ts_decoded_mono_ns: int
    ts_proc_mono_ns: int
    payload: BookPayload | TradePayload


def _parse_levels(raw_levels: list[Any]) -> list[BookLevel]:
    levels: list[BookLevel] = []
    for level in raw_levels:
        if not isinstance(level, list) or len(level) < 4:
            continue
        try:
            levels.append(
                BookLevel(
                    price=float(level[0]),
                    size=float(level[1]),
                    count=int(level[3]),
                )
            )
        except (ValueError, TypeError, IndexError):
            continue
    return levels


def normalize_okx(
    ts_recv_epoch_ms: int,
    ts_recv_mono_ns: int,
    ts_decoded_mono_ns: int,
    msg: dict[str, Any],
) -> list[NormalizedEvent]:
    """Normalize an OKX WebSocket message to NormalizedEvent(s)."""
    if msg.get("event") in ("subscribe", "unsubscribe", "error"):
        return []

    arg = msg.get("arg") or {}
    channel = arg.get("channel")
    data = msg.get("data")

    if not channel or not isinstance(data, list) or not data:
        return []

    inst_id = arg.get("instId")
    if not inst_id:
        return []

    events: list[NormalizedEvent] = []

    if channel == "books5":
        d0 = data[0]
        try:
            ts_exchange_ms = int(d0.get("ts", "0"))
        except (ValueError, TypeError):
            return []

        bids = _parse_levels(d0.get("bids") or [])
        asks = _parse_levels(d0.get("asks") or [])

        payload = BookPayload(
            n=5,
            best_bid=(bids[0].price if bids else 0.0),
            best_ask=(asks[0].price if asks else 0.0),
            bids=bids,
            asks=asks,
        )

        ts_proc_mono_ns = time.monotonic_ns()
        if _DEBUG:
            if ts_decoded_mono_ns < ts_recv_mono_ns:
                raise RuntimeError(
                    f"Invariant violated: decoded_ns ({ts_decoded_mono_ns}) < recv_ns ({ts_recv_mono_ns})"
                )
            if ts_proc_mono_ns < ts_decoded_mono_ns:
                raise RuntimeError(
                    f"Invariant violated: proc_ns ({ts_proc_mono_ns}) < decoded_ns ({ts_decoded_mono_ns})"
                )

        events.append(
            NormalizedEvent(
                exchange="okx",
                symbol=inst_id,
                channel="books5",
                event_type="book_topn",
                ts_exchange_ms=ts_exchange_ms,
                ts_recv_epoch_ms=ts_recv_epoch_ms,
                ts_recv_mono_ns=ts_recv_mono_ns,
                ts_decoded_mono_ns=ts_decoded_mono_ns,
                ts_proc_mono_ns=ts_proc_mono_ns,
                payload=payload,
            )
        )

    elif channel == "trades":
        for d in data:
            try:
                ts_exchange_ms = int(d.get("ts", "0"))
            except (ValueError, TypeError):
                continue

            try:
                payload = TradePayload(
                    price=float(d["px"]),
                    size=float(d["sz"]),
                    side=d["side"],
                    trade_id=d.get("tradeId"),
                )
            except (KeyError, ValueError, TypeError):
                continue

            ts_proc_mono_ns = time.monotonic_ns()
            if _DEBUG:
                if ts_decoded_mono_ns < ts_recv_mono_ns:
                    raise RuntimeError(
                        f"Invariant violated: decoded_ns ({ts_decoded_mono_ns}) < recv_ns ({ts_recv_mono_ns})"
                    )
                if ts_proc_mono_ns < ts_decoded_mono_ns:
                    raise RuntimeError(
                        f"Invariant violated: proc_ns ({ts_proc_mono_ns}) < decoded_ns ({ts_decoded_mono_ns})"
                    )

            events.append(
                NormalizedEvent(
                    exchange="okx",
                    symbol=inst_id,
                    channel="trades",
                    event_type="trade",
                    ts_exchange_ms=ts_exchange_ms,
                    ts_recv_epoch_ms=ts_recv_epoch_ms,
                    ts_recv_mono_ns=ts_recv_mono_ns,
                    ts_decoded_mono_ns=ts_decoded_mono_ns,
                    ts_proc_mono_ns=ts_proc_mono_ns,
                    payload=payload,
                )
            )

    return events
