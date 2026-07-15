# LoRa Tracker firmware contract simulator

This native host harness compiles and runs the shared C++ protocol and
configuration headers from both embedded components.  It covers binary frame
layout checks, device hash formatting, ULEB128 boundaries and malformed input,
configuration CRC validation, and gateway tracker registry rules.

It does not emulate ESP32 peripherals, radio traffic, power, storage, BLE or
Wi-Fi.  Those require target builds and hardware-in-the-loop tests.

Run it as part of the full simulator from `components/archiver`:

```text
python -m lora_tracker_archiver.simulator --embedded-suite
```

The command requires a C++17 compiler (`g++`, `clang++`, or `CXX`).  It compiles
the test once against `tracker-firmware` and once against `gateway-firmware`.
