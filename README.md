# QuantumFlow

Low-latency trading engine with a Python market-data pipeline and a live web UI.

## Highlights
- C++ limit-order-book core with pluggable strategies
- Python websocket ingest and normalization pipeline
- Bridge ingress from Python to C++ (Unix socket IPC + in-process fallback)
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
   ./build/quantumflow --symbols BTC-USDT-SWAP,ETH-USDT-SWAP
   ```
4. Run the Python pipeline (in another terminal):
   ```bash
   cd pipeline
   PYTHONPATH=. python -m src.app \
     --symbols BTC-USDT-SWAP,ETH-USDT-SWAP \
     --channels books5,trades \
     --cpp-bridge
   ```

## Layout
- `bridge/` shared-memory bridge + bindings
- `pipeline/` Python market-data pipeline
- `strategies/` trading strategies
- `web/` dashboard UI
- `orderbook/` LOB implementation

## Notes
Pass `--headless` to run without the WebUI, and `--symbols` to set instruments.
