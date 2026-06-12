BUILD_DIR  := build
SAN_BUILD_DIR := build-sanitize
JOBS       := $(shell nproc)
WS_PORT    := 9001
SYMBOLS    := BTC-USDT-SWAP,ETH-USDT-SWAP
CHANNELS   := books5,trades
BRIDGE_SOCK := /tmp/quantumflow_bridge.sock
PIPELINE_CTRL_SOCK := /tmp/quantumflow_pipeline_ctrl.sock

.PHONY: all configure configure-sanitize build build-sanitize run run-engine pipeline-run web test test-sanitize clean headless

## Default: build everything
all: build web-install

## Configure CMake (WebUI ON)
configure:
	@mkdir -p $(BUILD_DIR)
	cd $(BUILD_DIR) && cmake -DQUANTUMFLOW_BUILD_WEBUI=ON ..

## Configure sanitizer build (Debug + ASan/UBSan)
configure-sanitize:
	@mkdir -p $(SAN_BUILD_DIR)
	cd $(SAN_BUILD_DIR) && cmake -DCMAKE_BUILD_TYPE=Debug -DQUANTUMFLOW_ENABLE_SANITIZERS=ON -DQUANTUMFLOW_BUILD_WEBUI=ON ..

## Build C++ engine
build: configure
	cd $(BUILD_DIR) && make -j$(JOBS)

## Build sanitizer configuration
build-sanitize: configure-sanitize
	cd $(SAN_BUILD_DIR) && make -j$(JOBS)

## Run C++ engine
run: run-engine

## Run C++ engine (WebSocket UI + Unix socket bridge ingress)
run-engine: build
	./$(BUILD_DIR)/quantumflow --symbols $(SYMBOLS) --ws-port $(WS_PORT) --bridge-socket $(BRIDGE_SOCK) --pipeline-control-socket $(PIPELINE_CTRL_SOCK)

## Run the C++ market-data pipeline and push events into the C++ engine
pipeline-run: build
	./$(BUILD_DIR)/quantumflow_pipeline --symbols $(SYMBOLS) --channels $(CHANNELS) --cpp-bridge --bridge-socket $(BRIDGE_SOCK) --control-socket $(PIPELINE_CTRL_SOCK) --no-jsonl

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

## Run tests under ASan/UBSan
test-sanitize: build-sanitize
	cd $(SAN_BUILD_DIR) && ctest --output-on-failure

## Build headless-only (no WebSocket server)
headless:
	@mkdir -p $(BUILD_DIR)
	cd $(BUILD_DIR) && cmake -DQUANTUMFLOW_BUILD_WEBUI=OFF .. && make -j$(JOBS)

## Clean build artifacts
clean:
	rm -rf $(BUILD_DIR)
