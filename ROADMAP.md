# Roadmap and larger refactor TODOs

Small hardening changes belong in normal issues. The items below intentionally
remain TODOs because they alter security boundaries, storage guarantees,
hardware support or multiple public interfaces.

## P0 — production release blockers

- [ ] Design and implement authenticated encryption for LoRa history, ACK and command frames.
- [ ] Replace the routing hash with a random public identifier and provision per-device keys.
- [ ] Add replay-safe, authenticated BLE provisioning with QR bootstrap and key rotation.
- [ ] Replace password-only ArduinoOTA with signed images, Secure Boot v2, flash/NVS encryption and documented eFuse/key custody.
- [ ] Replace QoS-0-only gateway publishing with durable local buffering and broker-confirmed delivery.
- [ ] Port the gateway to a current ESP32-S3/SX1262 board and share its RadioLib driver with the tracker.
- [ ] Add broker/browser integration tests and hardware-in-the-loop tests for both boards.
- [ ] Run multi-day battery, GNSS, deep-sleep, RF-loss, queue-overflow and reconnect qualification.
- [ ] Enforce regional airtime/duty-cycle budgets across retries and document supported regions.
- [ ] Add crash-safe RTC history metadata with magic, schema, CRC and tested recovery.
- [ ] Select a software/hardware license and publish a vulnerability-response policy.

## P1 — reliability and operations

- [ ] Add fleet health metrics, structured logs and alerting for stale devices, low battery and storage failures.
- [ ] Add database backup/restore, integrity checking and configurable retention tooling.
- [ ] Add configuration export/import with encrypted secrets and an auditable device inventory.
- [ ] Add safe firmware rollout groups, rollback metadata and minimum-version enforcement.
- [ ] Add factory acceptance tests for GNSS, radio, current draw, battery gauge and enclosure seals.
- [ ] Define queue overflow priorities and explicit data-loss telemetry.
- [ ] Add multi-gateway ACK collision avoidance and reception-quality analytics.

## P1 — application

- [ ] Build an authenticated onboarding UI for BLE and Wi-Fi.
- [ ] Add automatic history pagination, background synchronization and export.
- [ ] Add a geographical map with selectable offline/local tiles.
- [ ] Add encrypted local secret storage protected by platform biometrics or PIN.
- [ ] Add alerts for stale trackers, low battery, missing gateways and unusual movement.
- [ ] Build the deferred mobile app from generated shared schemas.

## P2 — maintainability and expansion

- [ ] Move duplicated embedded headers into a reusable PlatformIO library.
- [ ] Generate C++, Python, JavaScript and mobile models/test vectors from one schema source.
- [ ] Split the large tracker and gateway sketches into testable services and hardware adapters.
- [ ] Rename the remaining historical embedded filenames and C++ namespaces as part of the shared-library refactor.
- [ ] Fuzz LoRa, MQTT and onboarding parsers and timestamp chains.
- [ ] Add altitude, speed, heading, HDOP, fix quality and radio metadata.
- [ ] Define relay routing, hop limits, deduplication and trust boundaries.
- [ ] Revisit 32-bit Unix timestamps before 2106.

## Hardware backlog

- [ ] Integrate a low-power wake-on-motion accelerometer.
- [ ] Integrate a real fuel gauge and temperature-aware battery policy.
- [ ] Evaluate a current-lifecycle secure element after the key architecture is defined.
- [ ] Validate antenna placement, body attenuation, enclosure impact and mounting safety.
- [ ] Produce a custom protected power design and IP-rated enclosure after prototype measurements.
