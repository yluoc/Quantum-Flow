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
    ts_exchange_ms: int          # epoch ms (from OKX)
    ts_recv_epoch_ms: int        # epoch ms (local, for exchange→recv latency)
    ts_recv_mono_ns: int         # monotonic ns at frame receipt (for recv→decode latency)
    ts_decoded_mono_ns: int      # monotonic ns after JSON decode (for decode→proc latency)
    ts_proc_mono_ns: int         # monotonic ns at end of normalization (for decode→proc latency)
    payload: BookPayload | TradePayload


def normalize_okx(ts_recv_epoch_ms: int, ts_recv_mono_ns: int, ts_decoded_mono_ns: int, msg: dict[str, Any]) -> list[NormalizedEvent]:
    """
    Normalize an OKX WebSocket message to NormalizedEvent(s).
    
    Args:
        ts_recv_epoch_ms: Epoch ms when message was received (for exchange→recv latency)
        ts_recv_mono_ns: Monotonic ns at frame receipt (for recv→decode latency)
        ts_decoded_mono_ns: Monotonic ns after JSON decode (for decode→proc latency)
        msg: Raw OKX message dict
        
    Returns:
        List of NormalizedEvent(s) - can return multiple events for trades channel
    """
    # Ignore non-data events (subscribe confirmations, errors, etc.)
    if msg.get("event") in ("subscribe", "unsubscribe", "error"):
        return []
    
    # Extract channel and data
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
        # books5 sends single item
        d0 = data[0]
        
        # Extract exchange timestamp (OKX provides as string)
        ts_exchange_str = d0.get("ts", "0")
        try:
            ts_exchange_ms = int(ts_exchange_str)
        except (ValueError, TypeError):
            return []
        
        # Extract bids and asks
        raw_bids = d0.get("bids") or []
        raw_asks = d0.get("asks") or []
        
        # Convert to BookLevel tuples: OKX format is [price_str, size_str, liquidated_count, order_count]
        # We use: price, size, count (order_count is index 3)
        bids: list[BookLevel] = []
        for level in raw_bids:
            if not isinstance(level, list) or len(level) < 4:
                continue
            try:
                price = float(level[0])
                size = float(level[1])
                count = int(level[3]) if len(level) > 3 else 0
                bids.append(BookLevel(price=price, size=size, count=count))
            except (ValueError, TypeError, IndexError):
                continue
        
        asks: list[BookLevel] = []
        for level in raw_asks:
            if not isinstance(level, list) or len(level) < 4:
                continue
            try:
                price = float(level[0])
                size = float(level[1])
                count = int(level[3]) if len(level) > 3 else 0
                asks.append(BookLevel(price=price, size=size, count=count))
            except (ValueError, TypeError, IndexError):
                continue
        
        # Compute best bid/ask from first level
        best_bid = bids[0].price if bids else 0.0
        best_ask = asks[0].price if asks else 0.0
        
        # Create payload
        payload = BookPayload(
            n=5,
            best_bid=best_bid,
            best_ask=best_ask,
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

        events.append(NormalizedEvent(
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
        ))
    
    elif channel == "trades":
        # trades can send multiple items
        for d in data:
            ts_exchange_str = d.get("ts", "0")
            try:
                ts_exchange_ms = int(ts_exchange_str)
            except (ValueError, TypeError):
                continue
            
            payload = TradePayload(
                price=float(d["px"]),
                size=float(d["sz"]),
                side=d["side"],
                trade_id=d.get("tradeId"),
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
            
            events.append(NormalizedEvent(
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
            ))
    
    return events
