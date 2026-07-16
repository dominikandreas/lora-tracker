# Portable firmware policy core

This directory contains deterministic C++17 policy code shared by the ESP32
firmware and the browser simulator. It deliberately excludes Arduino, radio,
GNSS, storage, Wi-Fi and clock APIs. Those are supplied by the firmware or by
the simulator as adapters.

The browser build exports the same policy functions through a small C ABI. Run:

```text
make -C components/simulator-web wasm
```

The generated `firmware-core.wasm` is loaded by the simulator worker. Native
tests compile the same source with the host C++ compiler.
