BUILD_DIR  := build
JOBS       := $(shell nproc)
WS_PORT    := 9001
SYMBOLS    := BTC-USDT-SWAP,ETH-USDT-SWAP
CHANNELS   := books5,trades
BRIDGE_SOCK := /tmp/quantumflow_bridge.sock
PYTHON     := python3
PIPELINE_VENV := pipeline/.venv

.PHONY: all configure build run run-engine pipeline-venv pipeline-install pipeline-run web test clean headless

## Default: build everything
all: build web-install

## Configure CMake (WebUI ON, Bridge OFF)
configure:
	@mkdir -p $(BUILD_DIR)
	cd $(BUILD_DIR) && cmake -DQUANTUMFLOW_BUILD_WEBUI=ON -DQUANTUMFLOW_BUILD_BRIDGE=OFF ..

## Build C++ engine
build: configure
	cd $(BUILD_DIR) && make -j$(JOBS)

## Run C++ engine
run: run-engine

## Run C++ engine (WebSocket UI + Unix socket bridge ingress)
run-engine: build
	./$(BUILD_DIR)/quantumflow --symbols $(SYMBOLS) --ws-port $(WS_PORT) --bridge-socket $(BRIDGE_SOCK)

## Run Python pipeline and push events into the C++ engine
pipeline-venv:
	$(PYTHON) -m venv $(PIPELINE_VENV)

pipeline-install: pipeline-venv
	$(PIPELINE_VENV)/bin/python -m pip install --upgrade pip
	$(PIPELINE_VENV)/bin/python -m pip install -r pipeline/requirements.txt

pipeline-run: pipeline-install
	cd pipeline && PYTHONPATH=. .venv/bin/python -m src.app --symbols $(SYMBOLS) --channels $(CHANNELS) --cpp-bridge --bridge-socket $(BRIDGE_SOCK) --no-jsonl

## Install web dependencies
web-install:
	cd web && npm install

## Start React dev server (localhost:5173)
web:
	cd web && npm run dev

## Build React for production
web-build:
	cd web && npm run build

## Run all C++ tests
test: build
	cd $(BUILD_DIR) && ctest --output-on-failure

## Build headless-only (no WebSocket server)
headless:
	@mkdir -p $(BUILD_DIR)
	cd $(BUILD_DIR) && cmake -DQUANTUMFLOW_BUILD_WEBUI=OFF -DQUANTUMFLOW_BUILD_BRIDGE=OFF .. && make -j$(JOBS)

## Clean build artifacts
clean:
	rm -rf $(BUILD_DIR)
