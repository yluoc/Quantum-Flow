"""Main application that wires together the OKX WebSocket pipeline."""

from __future__ import annotations

import argparse
import asyncio
import logging
import signal
import sys

from src.metrics import RollingMetrics
from src.normalizer import normalize_okx
from src.okx_ws import okx_stream
from src.sinks.jsonl import JsonlSink
from src.sinks.stdout import StdoutSink


# Configure minimal logging
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
    datefmt="%Y-%m-%d %H:%M:%S",
)
logger = logging.getLogger(__name__)


async def main_loop(
    url: str,
    symbols: list[str],
    channels: list[str],
    enable_stdout: bool,
    enable_jsonl: bool,
    enable_cpp_bridge: bool,
    bridge_socket: str,
    csv_export_path: str | None,
    csv_export_interval: float,
) -> None:
    """Main event loop."""
    stop = asyncio.Event()
    
    # Setup signal handlers for graceful shutdown
    def signal_handler():
        logger.info("Received shutdown signal, stopping...")
        stop.set()
    
    if sys.platform != "win32":
        loop = asyncio.get_event_loop()
        for sig in (signal.SIGTERM, signal.SIGINT):
            loop.add_signal_handler(sig, signal_handler)
    else:
        # Windows doesn't support signal handlers in the same way
        pass
    
    # Create sinks
    sinks = []
    if enable_stdout:
        sinks.append(StdoutSink())
    if enable_jsonl:
        sinks.append(JsonlSink(root="data", flush_interval_sec=1.0, flush_count=100))
    if enable_cpp_bridge:
        from src.sinks.bridge import CppBridgeSink
        sinks.append(CppBridgeSink(socket_path=bridge_socket))
    
    # Create metrics
    metrics = RollingMetrics(window_seconds=5.0)
    
    # Metrics printing task
    async def metrics_printer():
        while not stop.is_set():
            metrics.print_stats()
            await asyncio.sleep(1.0)
    
    # CSV export task
    async def csv_exporter():
        if csv_export_path:
            while not stop.is_set():
                await asyncio.sleep(csv_export_interval)
                try:
                    metrics.export_csv(csv_export_path)
                    logger.info(f"Exported metrics to {csv_export_path}")
                except Exception as e:
                    logger.error(f"Error exporting CSV: {e}", exc_info=True)
    
    # Main processing task
    async def process_stream():
        try:
            async for ts_recv_epoch_ms, ts_recv_mono_ns, ts_decoded_mono_ns, msg in okx_stream(url, symbols, channels, stop):
                # Normalize (returns list of events)
                events = normalize_okx(ts_recv_epoch_ms, ts_recv_mono_ns, ts_decoded_mono_ns, msg)
                if not events:
                    continue
                
                # Process each event
                for event in events:
                    # Update metrics
                    metrics.update(event)
                    
                    # Fan-out to sinks
                    for sink in sinks:
                        try:
                            await sink.write(event)
                        except Exception as e:
                            logger.error(f"Error writing to sink {type(sink).__name__}: {e}", exc_info=True)
        except Exception as e:
            logger.error(f"Error in stream processing: {e}", exc_info=True)
            stop.set()
    
    # Start tasks
    tasks = [
        asyncio.create_task(process_stream()),
        asyncio.create_task(metrics_printer()),
    ]
    if csv_export_path:
        tasks.append(asyncio.create_task(csv_exporter()))
    
    try:
        # Handle Ctrl+C on Windows
        if sys.platform == "win32":
            try:
                await asyncio.wait_for(asyncio.gather(*tasks), timeout=None)
            except KeyboardInterrupt:
                signal_handler()
        else:
            await asyncio.gather(*tasks)
    except KeyboardInterrupt:
        signal_handler()
    finally:
        # Wait for tasks to complete
        for task in tasks:
            task.cancel()
        for task in tasks:
            try:
                await task
            except asyncio.CancelledError:
                pass
        
        # Close sinks
        for sink in sinks:
            try:
                await sink.close()
            except Exception as e:
                logger.error(f"Error closing sink {type(sink).__name__}: {e}", exc_info=True)
        
        # Final metrics print and CSV export
        metrics.print_stats(force=True)
        if csv_export_path:
            try:
                metrics.export_csv(csv_export_path)
                logger.info(f"Final metrics exported to {csv_export_path}")
            except Exception as e:
                logger.error(f"Error in final CSV export: {e}", exc_info=True)
        logger.info("Shutdown complete")


def parse_args() -> argparse.Namespace:
    """Parse command-line arguments."""
    parser = argparse.ArgumentParser(
        description="OKX WebSocket market data pipeline",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument(
        "--symbols",
        type=str,
        default="BTC-USDT-SWAP,ETH-USDT-SWAP",
        help="Comma-separated list of trading pairs",
    )
    parser.add_argument(
        "--channels",
        type=str,
        default="books5,trades",
        help="Comma-separated list of channels (e.g., books5,trades)",
    )
    parser.add_argument(
        "--csv-export",
        type=str,
        default=None,
        help="Path to CSV export file (e.g., reports/metrics_summary.csv)",
    )
    parser.add_argument(
        "--csv-export-interval",
        type=float,
        default=30.0,
        help="CSV export interval in seconds",
    )
    parser.add_argument(
        "--url",
        type=str,
        default="wss://ws.okx.com:8443/ws/v5/public",
        help="WebSocket URL",
    )
    parser.add_argument(
        "--no-stdout",
        action="store_true",
        help="Disable stdout sink",
    )
    parser.add_argument(
        "--no-jsonl",
        action="store_true",
        help="Disable JSONL file sink",
    )
    parser.add_argument(
        "--cpp-bridge",
        action="store_true",
        help="Enable C++ bridge sink (push data to QuantumFlow Unix socket)",
    )
    parser.add_argument(
        "--bridge-socket",
        type=str,
        default="/tmp/quantumflow_bridge.sock",
        help="Unix socket path used by the C++ bridge sink",
    )
    return parser.parse_args()


def main() -> None:
    """Entry point."""
    args = parse_args()
    
    symbols = [s.strip() for s in args.symbols.split(",") if s.strip()]
    if not symbols:
        logger.error("No symbols provided")
        sys.exit(1)
    
    channels = [c.strip() for c in args.channels.split(",") if c.strip()]
    if not channels:
        logger.error("No channels provided")
        sys.exit(1)
    
    logger.info(f"Starting pipeline: symbols={symbols}, channels={channels}, url={args.url}")
    logger.info(
        f"Sinks: stdout={not args.no_stdout}, jsonl={not args.no_jsonl}, "
        f"cpp_bridge={args.cpp_bridge} socket={args.bridge_socket}"
    )
    if args.csv_export:
        logger.info(f"CSV export: {args.csv_export} (interval: {args.csv_export_interval}s)")
    
    try:
        asyncio.run(main_loop(
            url=args.url,
            symbols=symbols,
            channels=channels,
            enable_stdout=not args.no_stdout,
            enable_jsonl=not args.no_jsonl,
            enable_cpp_bridge=args.cpp_bridge,
            bridge_socket=args.bridge_socket,
            csv_export_path=args.csv_export,
            csv_export_interval=args.csv_export_interval,
        ))
    except KeyboardInterrupt:
        logger.info("Interrupted by user")
    except Exception as e:
        logger.error(f"Fatal error: {e}", exc_info=True)
        sys.exit(1)


if __name__ == "__main__":
    main()
