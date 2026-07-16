# LoRa Tracker firmware contract simulator

This native host harness compiles and runs the shared C++ protocol and
configuration headers from all three embedded components. It covers binary
frame layout, device hashes, ULEB128 malformed input, configuration CRC and
registry rules, plus relay parsing, hop bounds, suppression timing, ACK cache
lifetime and LoRa time-on-air calculation.

It does not emulate ESP32 peripherals, RF propagation/collisions, power,
storage, BLE or Wi-Fi. Those require target builds and hardware-in-the-loop
tests.

Run it as part of the full simulator from `components/archiver`:

```text
python -m lora_tracker_archiver.simulator --embedded-suite
```

The command requires a C++17 compiler (`g++`, `clang++`, or `CXX`). It compiles
the test independently against tracker, gateway and repeater firmware headers.
