"""C++ bridge sink — pushes normalized events to the C++ ring buffer via PyBind11."""

from __future__ import annotations

import logging
import time

from src.normalizer import BookPayload, NormalizedEvent, TradePayload
from src.sinks.base import Sink

logger = logging.getLogger(__name__)

# Quantity conversion: float size → satoshi units (multiply by 1e8)
_QTY_SCALE = int(1e8)


class CppBridgeSink(Sink):
    """Sink that pushes market data events to the C++ QuantumFlow ring buffer."""

    def __init__(self) -> None:
        try:
            import quantumflow_bridge  # type: ignore[import-not-found]
            self._bridge = quantumflow_bridge
        except ImportError as e:
            raise ImportError(
                "quantumflow_bridge module not found. "
                "Build the PyBind11 module first: cmake --build build --target quantumflow_bridge"
            ) from e

    async def write(self, event: NormalizedEvent) -> None:
        payload = event.payload
        ts_ns = event.ts_recv_mono_ns

        if isinstance(payload, BookPayload):
            # Push each bid level
            for level in payload.bids:
                self._bridge.push_market_data(
                    symbol=event.symbol,
                    side=0,  # buy
                    event_type=0,  # book_level
                    price=level.price,
                    quantity=int(level.size * _QTY_SCALE),
                    timestamp_ns=ts_ns,
                )
            # Push each ask level
            for level in payload.asks:
                self._bridge.push_market_data(
                    symbol=event.symbol,
                    side=1,  # sell
                    event_type=0,  # book_level
                    price=level.price,
                    quantity=int(level.size * _QTY_SCALE),
                    timestamp_ns=ts_ns,
                )

        elif isinstance(payload, TradePayload):
            side = 0 if payload.side == "buy" else 1
            self._bridge.push_market_data(
                symbol=event.symbol,
                side=side,
                event_type=1,  # trade
                price=payload.price,
                quantity=int(payload.size * _QTY_SCALE),
                timestamp_ns=ts_ns,
            )

    async def close(self) -> None:
        stats = self._bridge.bridge_stats()
        logger.info(
            "CppBridgeSink closed — pushed=%d popped=%d dropped=%d",
            stats["push_count"],
            stats["pop_count"],
            stats["drop_count"],
        )
