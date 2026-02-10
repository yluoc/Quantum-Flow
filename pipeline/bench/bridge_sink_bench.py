#!/usr/bin/env python3
"""A/B benchmark for CppBridgeSink (python fallback vs native module)."""

from __future__ import annotations

import argparse
import asyncio
import os
import sys
import tempfile
import time
from pathlib import Path


def _ensure_paths() -> None:
    repo_root = Path(__file__).resolve().parents[2]
    pipeline_root = repo_root / "pipeline"
    build_root = repo_root / "build"
    for p in (pipeline_root, build_root, build_root / "lib", build_root / "bin"):
        s = str(p)
        if s not in sys.path:
            sys.path.insert(0, s)


_ensure_paths()

from src.normalizer import (  # noqa: E402
    BookLevel,
    BookPayload,
    NormalizedEvent,
    TradePayload,
)
from src.sinks.bridge import CppBridgeSink  # noqa: E402


def _make_book_event(ts: int) -> NormalizedEvent:
    bids = [
        BookLevel(price=43000.0 - i, size=0.05 + i * 0.01, count=10 + i)
        for i in range(5)
    ]
    asks = [
        BookLevel(price=43001.0 + i, size=0.04 + i * 0.01, count=9 + i)
        for i in range(5)
    ]
    payload = BookPayload(
        n=5,
        best_bid=bids[0].price,
        best_ask=asks[0].price,
        bids=bids,
        asks=asks,
    )
    return NormalizedEvent(
        exchange="okx",
        symbol="BTC-USDT-SWAP",
        channel="books5",
        event_type="book_topn",
        ts_exchange_ms=1,
        ts_recv_epoch_ms=1,
        ts_recv_mono_ns=ts,
        ts_decoded_mono_ns=ts,
        ts_proc_mono_ns=ts,
        payload=payload,
    )


def _make_trade_event(ts: int) -> NormalizedEvent:
    payload = TradePayload(
        price=43000.5,
        size=0.123,
        side="buy",
        trade_id="t1",
    )
    return NormalizedEvent(
        exchange="okx",
        symbol="BTC-USDT-SWAP",
        channel="trades",
        event_type="trade",
        ts_exchange_ms=1,
        ts_recv_epoch_ms=1,
        ts_recv_mono_ns=ts,
        ts_decoded_mono_ns=ts,
        ts_proc_mono_ns=ts,
        payload=payload,
    )


async def _bench(mode: str, socket_path: str, event_kind: str, events: int, warmup: int) -> dict[str, float]:
    os.environ["QF_BRIDGE_MODE"] = mode
    sink = CppBridgeSink(socket_path=socket_path)
    ts = time.monotonic_ns()
    event = _make_book_event(ts) if event_kind == "book" else _make_trade_event(ts)
    packets_per_event = 10 if event_kind == "book" else 1

    for _ in range(warmup):
        await sink.write(event)

    t0 = time.perf_counter_ns()
    for _ in range(events):
        await sink.write(event)
    t1 = time.perf_counter_ns()

    await sink.close()
    elapsed_s = max((t1 - t0) / 1_000_000_000.0, 1e-12)
    return {
        "events_per_s": events / elapsed_s,
        "packets_per_s": (events * packets_per_event) / elapsed_s,
        "us_per_event": (elapsed_s * 1_000_000.0) / events,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description="Benchmark bridge sink modes")
    parser.add_argument("--events", type=int, default=40000)
    parser.add_argument("--warmup", type=int, default=5000)
    parser.add_argument("--kind", choices=("book", "trade"), default="book")
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="qf-bridge-bench-") as td:
        # In sandboxed environments AF_UNIX bind may be blocked, so we benchmark
        # encode+sendto path against a guaranteed-missing socket path.
        socket_path = os.path.join(td, "missing.sock")
        py = asyncio.run(_bench("python", socket_path, args.kind, args.events, args.warmup))
        native = asyncio.run(_bench("native", socket_path, args.kind, args.events, args.warmup))

    speedup = native["events_per_s"] / max(py["events_per_s"], 1e-9)
    print(f"Bridge sink benchmark ({args.kind}, events={args.events}, warmup={args.warmup})")
    print(f"python: events/s={py['events_per_s']:.2f} packets/s={py['packets_per_s']:.2f} us/event={py['us_per_event']:.3f}")
    print(f"native: events/s={native['events_per_s']:.2f} packets/s={native['packets_per_s']:.2f} us/event={native['us_per_event']:.3f}")
    print(f"speedup(native/python): {speedup:.3f}x")


if __name__ == "__main__":
    main()
