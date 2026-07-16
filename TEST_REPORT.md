# Validation report

Date: 2026-07-16

## Passed

- Archiver, protocol, service simulator and native embedded contracts: all 19 tests passed with the user-local Zig 0.16.0 C++17 compiler.
- Web app: 6 Node.js tests passed, including malicious identity/coordinate and timestamp rejection.
- Brokerless service simulation covered SQLite inserts, duplicates, history chunks and five QoS-1 archive confirmations.
- PlatformIO tracker `heltec_wifi_lora_32_v2`: compiled and linked.
- PlatformIO tracker `heltec_wireless_tracker`: compiled and linked.
- PlatformIO gateway `heltec_wifi_lora_32_v2`: compiled and linked.
- PlatformIO repeater `heltec_wifi_lora_32_v2`: compiled and linked.
- PlatformIO repeater `heltec_wireless_tracker`: compiled and linked.
- Python bytecode compilation, JavaScript module tests and `git diff --check` passed.
- GitHub Pages renderer produced 26 HTML pages; every local link and asset resolved in a CI-equivalent temporary artifact.

The native embedded suite compiled and executed the protocol, configuration and
relay assertions independently against tracker, gateway and repeater header copies.

## Not covered

No device was flashed in this environment. The firmware performs an AES-GCM
round-trip and tamper-rejection self-test at boot, but real-device RF, GNSS,
BLE pairing, deep-sleep current, battery behavior, NVS/RTC power-loss recovery,
real MQTT broker archive-confirmation timing/ACL behavior, browser flashing and hardware controls
still require the qualification listed in `docs/PRODUCTION_READINESS.md`.
Repeater propagation, hidden-node collisions, installed-topology suppression,
calibrated rolling-hour airtime and installed ERP enforcement and end-to-end multi-hop ACK timing are likewise not
validated without at least one tracker, receiver and two repeater boards.
