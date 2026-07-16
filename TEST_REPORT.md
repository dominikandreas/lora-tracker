# Validation report

Date: 2026-07-16

## Passed

- Archiver, protocol, service simulator and native embedded contracts: all 19 tests passed with the user-local Zig C++17 frontend.
- Existing monitoring PWA: all 6 Node.js tests passed, including malicious identity/coordinate and timestamp rejection.
- Browser Network Lab: all 7 deterministic engine and WASM golden-policy tests passed.
- Headless Chromium: all 3 Playwright scenarios passed against the real WASM application (time stepping, tracker/waypoint creation and configuration, MQTT outage, desktop/mobile layout and no page errors).
- Brokerless service simulation covered SQLite inserts, duplicates, history chunks and five QoS-1 archive confirmations.
- PlatformIO tracker `heltec_wifi_lora_32_v2`: compiled and linked with the portable core.
- PlatformIO tracker `heltec_wireless_tracker`: compiled and linked with the portable core.
- PlatformIO gateway `heltec_wifi_lora_32_v2`: compiled and linked with the relay-clear ACK guard.
- PlatformIO repeater `heltec_wifi_lora_32_v2`: compiled and linked with shared relay/airtime policy.
- PlatformIO repeater `heltec_wireless_tracker`: compiled and linked with shared relay/airtime policy.
- GitHub Pages renderer assembled 27 HTML pages plus `/simulator/`; all local links and assets resolved in a CI-equivalent preview.
- `git diff --check` passed in both repositories.

The native contracts compile the production portable C++ source and all three
embedded protocol/configuration copies. The WASM test executes the compiled
module against golden radio, airtime, sensitivity, tracker timing and ACK-guard
vectors. A deterministic multi-hop test archives tracker data through a relay
and returns an authenticated ACK; the corresponding simulation exposed and now
regression-tests the gateway/repeater ACK collision guard.

## Not covered

No physical device was flashed in this environment. Real-device RF, antenna and
installed ERP, GNSS, BLE pairing, deep-sleep current, battery behavior,
NVS/RTC power-loss recovery, brownouts, real MQTT TLS/ACL/persistence, browser
flashing and enclosure controls still require the qualification in
`docs/PRODUCTION_READINESS.md`.

The Network Lab propagation, vegetation/building attenuation, GNSS probability
and current models are explainable estimates but are not field-calibrated. They
must not be used as range, battery-life or compliance claims without measured
site and hardware data.
