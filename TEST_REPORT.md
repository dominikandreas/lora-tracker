# Validation report

Date: 2026-07-15

## Passed

- Archiver: 16 tests passed; one compiler-dependent test was skipped in the
  plain Python run and then covered by the explicit embedded suite.
- Web app: 6 tests passed on Node.js 24 LTS.
- Brokerless pipeline: 2 trackers × 12 points, duplicate receptions, storage,
  pagination and service callbacks passed.
- Native embedded contracts: tracker and gateway header copies compiled with
  C++17 and passed protocol/configuration assertions.
- PlatformIO tracker `heltec_wifi_lora_32_v2`: compiled and linked.
- PlatformIO tracker `heltec_wireless_tracker`: compiled and linked.
- PlatformIO gateway `heltec_wifi_lora_32_v2`: compiled and linked.

The real tracker build found and fixed an RTC slow-memory overflow by reducing
the retained queue from 500 to 448 points, leaving space for the other retained
state.

## Not covered

No device was flashed in this environment. RF, GNSS, deep-sleep current,
battery behavior, NVS/RTC power-loss recovery, real MQTT broker semantics,
browser rendering and hardware controls still require the qualification listed
in `docs/PRODUCTION_READINESS.md`.
