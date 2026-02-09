"""Stdout sink - prints compact one-liners."""

from __future__ import annotations

from src.normalizer import NormalizedEvent
from src.sinks.base import Sink


class StdoutSink(Sink):
    """Prints compact one-liners to stdout."""
    
    async def write(self, event: NormalizedEvent) -> None:
        """Print a compact one-liner with deterministic field ordering."""
        from src.normalizer import BookPayload, TradePayload
        
        lat_ex_to_recv_ms = event.ts_recv_epoch_ms - event.ts_exchange_ms
        lat_recv_to_decode_us = (event.ts_decoded_mono_ns - event.ts_recv_mono_ns) / 1000.0
        lat_decode_to_proc_us = (event.ts_proc_mono_ns - event.ts_decoded_mono_ns) / 1000.0
        
        if isinstance(event.payload, BookPayload):
            spread = event.payload.best_ask - event.payload.best_bid
            print(
                f"{event.symbol} | "
                f"bid={event.payload.best_bid:.2f} ask={event.payload.best_ask:.2f} spread={spread:.2f} | "
                f"Ex→Recv={lat_ex_to_recv_ms}ms Recv→Decode={lat_recv_to_decode_us:.3f}us Decode→Proc={lat_decode_to_proc_us:.3f}us"
            )
        elif isinstance(event.payload, TradePayload):
            print(
                f"{event.symbol} | "
                f"trade {event.payload.side} price={event.payload.price:.2f} size={event.payload.size:.6f} | "
                f"Ex→Recv={lat_ex_to_recv_ms}ms Recv→Decode={lat_recv_to_decode_us:.3f}us Decode→Proc={lat_decode_to_proc_us:.3f}us"
            )
    
    async def close(self) -> None:
        """No-op for stdout."""
        pass
