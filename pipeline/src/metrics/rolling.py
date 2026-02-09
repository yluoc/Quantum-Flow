"""Rolling metrics aggregator for latency tracking."""

from __future__ import annotations

import collections
import csv
import os
import time
from collections import deque
from datetime import datetime, timezone
from typing import Any

from src.normalizer import NormalizedEvent
from src.time_helpers import now_mono_ms


class RollingMetrics:
    """O(1) update rolling metrics with percentile computation."""
    
    def __init__(self, window_seconds: float = 5.0):
        """
        Args:
            window_seconds: Rolling window size in seconds
        """
        self.window_seconds = window_seconds
        self.window_ms = int(window_seconds * 1000)
        
        self.latency_exchange_to_recv: deque[tuple[int, float]] = deque()
        self.latency_recv_to_decode: deque[tuple[int, int]] = deque()
        self.latency_decode_to_proc: deque[tuple[int, int]] = deque()
        
        # Per (symbol, channel) tracking for CSV export
        self.latency_by_key: dict[tuple[str, str], deque[tuple[int, float]]] = {}
        self.staleness_by_key: dict[tuple[str, str], deque[tuple[int, float]]] = {}
        self.last_ts_exchange: dict[tuple[str, str], int] = {}
        
        # Message counts per symbol
        self.message_counts: dict[str, int] = collections.defaultdict(int)
        
        # Zero latency counters
        self.count_zero_recv_to_decode: int = 0
        self.count_zero_decode_to_proc: int = 0
        self.total_events: int = 0
        
        self.last_print_time = time.monotonic()
    
    def update(self, event: NormalizedEvent) -> None:
        """Update metrics with a new event."""
        # Use monotonic ms for rolling window bookkeeping (same clock domain)
        t_mono_ms = now_mono_ms()
        
        lat_ex_to_recv_ms = event.ts_recv_epoch_ms - event.ts_exchange_ms
        lat_recv_to_decode_ns = event.ts_decoded_mono_ns - event.ts_recv_mono_ns
        lat_decode_to_proc_ns = event.ts_proc_mono_ns - event.ts_decoded_mono_ns
        
        self.total_events += 1
        if lat_recv_to_decode_ns == 0:
            self.count_zero_recv_to_decode += 1
        if lat_decode_to_proc_ns == 0:
            self.count_zero_decode_to_proc += 1
        
        self.latency_exchange_to_recv.append((t_mono_ms, lat_ex_to_recv_ms))
        self.latency_recv_to_decode.append((t_mono_ms, lat_recv_to_decode_ns))
        self.latency_decode_to_proc.append((t_mono_ms, lat_decode_to_proc_ns))
        
        # Remove old entries
        cutoff_ms = t_mono_ms - self.window_ms
        while self.latency_exchange_to_recv and self.latency_exchange_to_recv[0][0] < cutoff_ms:
            self.latency_exchange_to_recv.popleft()
        while self.latency_recv_to_decode and self.latency_recv_to_decode[0][0] < cutoff_ms:
            self.latency_recv_to_decode.popleft()
        while self.latency_decode_to_proc and self.latency_decode_to_proc[0][0] < cutoff_ms:
            self.latency_decode_to_proc.popleft()
        
        # Per (symbol, channel) tracking for CSV export
        key = (event.symbol, event.channel)
        
        # Track latency
        if key not in self.latency_by_key:
            self.latency_by_key[key] = deque()
        self.latency_by_key[key].append((t_mono_ms, lat_ex_to_recv_ms))
        while self.latency_by_key[key] and self.latency_by_key[key][0][0] < cutoff_ms:
            self.latency_by_key[key].popleft()
        
        # Track staleness (time between exchange timestamps)
        last_ts = self.last_ts_exchange.get(key)
        if last_ts is not None:
            stale_ms = event.ts_exchange_ms - last_ts
            if key not in self.staleness_by_key:
                self.staleness_by_key[key] = deque()
            self.staleness_by_key[key].append((t_mono_ms, stale_ms))
            while self.staleness_by_key[key] and self.staleness_by_key[key][0][0] < cutoff_ms:
                self.staleness_by_key[key].popleft()
        self.last_ts_exchange[key] = event.ts_exchange_ms
        
        # Update message count
        self.message_counts[event.symbol] += 1
    
    def _percentiles(self, values: list[float], p50: float, p95: float, p99: float) -> tuple[float, float, float]:
        """Compute percentiles from sorted values."""
        if not values:
            return (0.0, 0.0, 0.0)
        
        sorted_vals = sorted(values)
        n = len(sorted_vals)
        
        def percentile(p: float) -> float:
            idx = int((p / 100.0) * (n - 1))
            return sorted_vals[idx]
        
        return (percentile(p50), percentile(p95), percentile(p99))
    
    def print_stats(self, force: bool = False) -> None:
        """Print p50/p95/p99 latencies if 1 second has passed."""
        now = time.monotonic()
        if not force and (now - self.last_print_time) < 1.0:
            return
        
        self.last_print_time = now
        
        ex_to_recv_vals = [v for _, v in self.latency_exchange_to_recv]
        recv_to_decode_vals = [float(v) for _, v in self.latency_recv_to_decode]
        decode_to_proc_vals = [float(v) for _, v in self.latency_decode_to_proc]
        
        min_samples = 20
        msg_counts_str = ", ".join(f"{sym}:{cnt}" for sym, cnt in sorted(self.message_counts.items()))
        
        parts = []
        
        if len(ex_to_recv_vals) >= min_samples:
            ex_to_recv_p50, ex_to_recv_p95, ex_to_recv_p99 = self._percentiles(ex_to_recv_vals, 50, 95, 99)
            parts.append(f"Ex→Recv p50={ex_to_recv_p50:.1f}ms p95={ex_to_recv_p95:.1f}ms p99={ex_to_recv_p99:.1f}ms")
        
        if len(recv_to_decode_vals) >= min_samples:
            recv_to_decode_p50, recv_to_decode_p95, recv_to_decode_p99 = self._percentiles(recv_to_decode_vals, 50, 95, 99)
            zero_rate_recv_decode = (self.count_zero_recv_to_decode / max(1, self.total_events)) * 100.0
            parts.append(f"Recv→Decode p50={recv_to_decode_p50/1000.0:.3f}us p95={recv_to_decode_p95/1000.0:.3f}us p99={recv_to_decode_p99/1000.0:.3f}us (zero={zero_rate_recv_decode:.1f}%)")
        
        if len(decode_to_proc_vals) >= min_samples:
            decode_to_proc_p50, decode_to_proc_p95, decode_to_proc_p99 = self._percentiles(decode_to_proc_vals, 50, 95, 99)
            zero_rate_decode_proc = (self.count_zero_decode_to_proc / max(1, self.total_events)) * 100.0
            parts.append(f"Decode→Proc p50={decode_to_proc_p50/1000.0:.3f}us p95={decode_to_proc_p95/1000.0:.3f}us p99={decode_to_proc_p99/1000.0:.3f}us (zero={zero_rate_decode_proc:.1f}%)")
        
        if parts:
            print(f"Metrics | {' | '.join(parts)} | Msgs: {msg_counts_str}")
    
    def export_csv(self, path: str) -> None:
        """Export metrics to CSV file with mean/std/min/max statistics."""
        os.makedirs(os.path.dirname(path), exist_ok=True)
        gen = datetime.now(timezone.utc).isoformat()
        
        keys = sorted(set(self.latency_by_key.keys()) | set(self.staleness_by_key.keys()))
        
        with open(path, "w", newline="", encoding="utf-8") as f:
            w = csv.writer(f)
            w.writerow([
                "generated_at_utc",
                "symbol",
                "channel",
                "lat_count",
                "lat_mean_ms",
                "lat_std_ms",
                "lat_min_ms",
                "lat_max_ms",
                "stale_count",
                "stale_mean_ms",
                "stale_std_ms",
                "stale_min_ms",
                "stale_max_ms",
            ])
            
            for (symbol, channel) in keys:
                key = (symbol, channel)
                # Compute latency stats
                lat_deque = self.latency_by_key.get(key, deque())
                lat_vals = [v for _, v in lat_deque]
                lat_count = len(lat_vals)
                if lat_count > 0:
                    lat_mean = sum(lat_vals) / lat_count
                    lat_std = (sum((x - lat_mean) ** 2 for x in lat_vals) / (lat_count - 1)) ** 0.5 if lat_count > 1 else 0.0
                    lat_min = min(lat_vals)
                    lat_max = max(lat_vals)
                else:
                    lat_mean = lat_std = lat_min = lat_max = 0.0
                
                # Compute staleness stats
                stale_deque = self.staleness_by_key.get(key, deque())
                stale_vals = [v for _, v in stale_deque]
                stale_count = len(stale_vals)
                if stale_count > 0:
                    stale_mean = sum(stale_vals) / stale_count
                    stale_std = (sum((x - stale_mean) ** 2 for x in stale_vals) / (stale_count - 1)) ** 0.5 if stale_count > 1 else 0.0
                    stale_min = min(stale_vals)
                    stale_max = max(stale_vals)
                else:
                    stale_mean = stale_std = stale_min = stale_max = 0.0
                
                w.writerow([
                    gen,
                    symbol,
                    channel,
                    lat_count,
                    f"{lat_mean:.3f}",
                    f"{lat_std:.3f}",
                    f"{lat_min:.3f}",
                    f"{lat_max:.3f}",
                    stale_count,
                    f"{stale_mean:.3f}",
                    f"{stale_std:.3f}",
                    f"{stale_min:.3f}",
                    f"{stale_max:.3f}",
                ])
