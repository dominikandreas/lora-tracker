# LoRa Tracker firmware contract simulator

This native host harness compiles and runs the protocol and configuration
headers from all three embedded components plus the portable C++ firmware core.
It covers binary frame layout, device hashes, ULEB128 malformed input,
configuration CRC and registry rules, relay parsing/hop/suppression/ACK cache,
and native parity for airtime, Germany radio policy, tracker sleep/retry/batch
policy and the relay/ACK collision guard. The same core is compiled to WASM for
the browser Network Lab.

It does not emulate ESP32 peripherals, RF propagation/collisions, power,
storage, BLE or Wi-Fi. Those require target builds and hardware-in-the-loop
tests.

Run it as part of the full simulator from `components/archiver`:

```text
python -m lora_tracker_archiver.simulator --embedded-suite
```

The command requires a C++17 compiler (`g++`, `clang++`, or `CXX`). It compiles
the test independently against tracker, gateway and repeater firmware headers.
