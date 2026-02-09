"""JSONL sink - writes normalized events to partitioned files."""

from __future__ import annotations

import asyncio
import os
from datetime import datetime, timezone
from typing import Any

try:
    import aiofiles
    HAS_AIOFILES = True
except ImportError:
    HAS_AIOFILES = False

from src.normalizer import NormalizedEvent
from src.sinks.base import Sink


def _partition_path(root: str, channel: str, symbol: str, ts_ms: int) -> str:
    """Generate partitioned file path: data/okx/{channel}/{YYYY-MM-DD}/{symbol}.jsonl"""
    dt = datetime.fromtimestamp(ts_ms / 1000, tz=timezone.utc)
    return os.path.join(
        root,
        "okx",
        channel,
        dt.strftime("%Y-%m-%d"),
        f"{symbol}.jsonl",
    )


def _event_to_dict(event: NormalizedEvent) -> dict[str, Any]:
    """Convert NormalizedEvent to dict for JSON serialization."""
    from src.normalizer import BookPayload, TradePayload
    
    base = {
        "exchange": event.exchange,
        "symbol": event.symbol,
        "channel": event.channel,
        "event_type": event.event_type,
        "ts_exchange_ms": event.ts_exchange_ms,
        "ts_recv_epoch_ms": event.ts_recv_epoch_ms,
        "ts_recv_mono_ns": event.ts_recv_mono_ns,
        "ts_decoded_mono_ns": event.ts_decoded_mono_ns,
        "ts_proc_mono_ns": event.ts_proc_mono_ns,
    }
    
    if isinstance(event.payload, BookPayload):
        base["payload"] = {
            "n": event.payload.n,
            "best_bid": event.payload.best_bid,
            "best_ask": event.payload.best_ask,
            "bids": [
                [level.price, level.size, level.count]
                for level in event.payload.bids
            ],
            "asks": [
                [level.price, level.size, level.count]
                for level in event.payload.asks
            ],
        }
    elif isinstance(event.payload, TradePayload):
        base["payload"] = {
            "price": event.payload.price,
            "size": event.payload.size,
            "side": event.payload.side,
            "trade_id": event.payload.trade_id,
        }
    
    return base


class JsonlSink(Sink):
    """Writes normalized events to partitioned JSONL files."""
    
    def __init__(
        self,
        root: str,
        flush_interval_sec: float = 1.0,
        flush_count: int = 100,
    ):
        """
        Args:
            root: Root directory for data files
            flush_interval_sec: Flush at least every N seconds
            flush_count: Flush at least every N events
        """
        self.root = root
        self.flush_interval_sec = flush_interval_sec
        self.flush_count = flush_count
        
        # Buffer: path -> list of event dicts
        self.buffer: dict[str, list[dict[str, Any]]] = {}
        self.buffer_count = 0
        self.last_flush_time = asyncio.get_event_loop().time()
        
        # File handles (path -> file handle)
        self._handles: dict[str, Any] = {}
    
    async def write(self, event: NormalizedEvent) -> None:
        """Buffer an event for writing."""
        # Determine file path (use epoch ms for partitioning to match wall clock)
        path = _partition_path(
            self.root,
            event.channel,
            event.symbol,
            event.ts_recv_epoch_ms,
        )
        
        # Convert to dict
        event_dict = _event_to_dict(event)
        
        # Add to buffer
        self.buffer.setdefault(path, []).append(event_dict)
        self.buffer_count += 1
        
        # Check if we should flush
        now = asyncio.get_event_loop().time()
        should_flush = (
            self.buffer_count >= self.flush_count
            or (now - self.last_flush_time) >= self.flush_interval_sec
        )
        
        if should_flush:
            await self._flush()
    
    async def _flush(self) -> None:
        """Flush all buffered events to disk."""
        if not self.buffer:
            return
        
        for path, events in self.buffer.items():
            if not events:
                continue
            
            # Ensure directory exists
            os.makedirs(os.path.dirname(path), exist_ok=True)
            
            # Write events
            if HAS_AIOFILES:
                async with aiofiles.open(path, "a", encoding="utf-8") as f:
                    for event_dict in events:
                        import json
                        line = json.dumps(event_dict, separators=(",", ":"), ensure_ascii=False) + "\n"
                        await f.write(line)
            else:
                # Fallback to asyncio.to_thread
                def _write_sync():
                    with open(path, "a", encoding="utf-8") as f:
                        for event_dict in events:
                            import json
                            line = json.dumps(event_dict, separators=(",", ":"), ensure_ascii=False) + "\n"
                            f.write(line)
                
                await asyncio.to_thread(_write_sync)
        
        # Clear buffer
        self.buffer.clear()
        self.buffer_count = 0
        self.last_flush_time = asyncio.get_event_loop().time()
    
    async def close(self) -> None:
        """Flush all pending writes."""
        await self._flush()
