# QuantumFlow

Low-latency trading engine with a Python market-data pipeline and a live web UI.

## Highlights
- C++ limit-order-book core with pluggable strategies
- Python websocket ingest and normalization pipeline
- Shared-memory bridge between Python and C++
- Optional WebSocket server + dashboard

## Quick Start
1. Build the C++ engine:
   ```bash
   cmake -S . -B build
   cmake --build build
   ```
2. Install the Python pipeline deps:
   ```bash
   python -m pip install -r pipeline/requirements.txt
   ```
3. Run the engine (defaults to WebUI mode):
   ```bash
   ./build/quantumflow
   ```

## Layout
- `bridge/` shared-memory bridge + bindings
- `pipeline/` Python market-data pipeline
- `strategies/` trading strategies
- `web/` dashboard UI
- `orderbook/` LOB implementation

## Notes
Pass `--headless` to run without the WebUI, and `--symbols` to set instruments.
