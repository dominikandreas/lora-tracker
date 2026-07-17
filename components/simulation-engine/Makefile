EMCC ?= emcc
CORE_DIR := ../firmware-core
WASM := dist/firmware-core.wasm
EXPORTS := '["_lt_core_version","_lt_validate_germany_radio","_lt_airtime_ms","_lt_ack_relay_guard_ms","_lt_receiver_sensitivity_dbm","_lt_tracker_sleep_s","_lt_tracker_retry_s","_lt_tracker_batch_due","_lt_forwarding_delay_ms"]'

.PHONY: all wasm build clean copy-assets

all: build

build: wasm copy-assets

wasm: $(WASM)

$(WASM): $(CORE_DIR)/src/lora_tracker_core.cpp $(CORE_DIR)/include/lora_tracker_core.h
	@mkdir -p dist
	$(EMCC) $(CORE_DIR)/src/lora_tracker_core.cpp \
		-I$(CORE_DIR)/include -std=c++17 -O1 \
		-s STANDALONE_WASM=1 -s FILESYSTEM=0 -s MALLOC=none \
		-s EXPORTED_FUNCTIONS=$(EXPORTS) -Wl,--no-entry -o $@

copy-assets:
	@mkdir -p dist
	cp -r src/*.js dist/

clean:
	rm -rf dist
