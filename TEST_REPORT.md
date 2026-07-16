# Validation report

Date: 2026-07-16

## Passed

- Archiver and native contracts: 18 tests passed with the Zig C++17 compiler.
- Web app: 6 tests passed on Node.js 24 LTS.
- Brokerless pipeline: 2 trackers × 12 points, duplicate receptions, storage,
  pagination and service callbacks passed.
- Native embedded contracts: tracker, gateway and repeater header copies
  compiled with C++17 and passed protocol/configuration/relay assertions.
- PlatformIO tracker `heltec_wifi_lora_32_v2`: compiled and linked.
- PlatformIO tracker `heltec_wireless_tracker`: compiled and linked.
- PlatformIO gateway `heltec_wifi_lora_32_v2`: compiled and linked.
- PlatformIO repeater `heltec_wifi_lora_32_v2`: compiled and linked.
- PlatformIO repeater `heltec_wireless_tracker`: compiled and linked.
- Release packaging: merged factory images, application binaries, ELF files,
  ESP Web Tools manifests and SHA-256 checksums were freshly generated for all
  five targets. ESP32 bootloader magic was verified at `0x1000`; ESP32-S3
  bootloader magic was verified at `0x0`.

The real tracker build found and fixed an RTC slow-memory overflow by reducing
the retained queue from 500 to 448 points, leaving space for the other retained
state.

## Not covered

No device was flashed in this environment. The firmware performs an AES-GCM
round-trip and tamper-rejection self-test at boot, but real-device RF, GNSS,
BLE pairing, deep-sleep current, battery behavior, NVS/RTC power-loss recovery,
MQTT broker acknowledgement semantics, browser flashing and hardware controls
still require the qualification listed in `docs/PRODUCTION_READINESS.md`.
Repeater propagation, hidden-node collisions, installed-topology suppression,
real airtime enforcement and end-to-end multi-hop ACK timing are likewise not
validated without at least one tracker, receiver and two repeater boards.
