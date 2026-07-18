# Roadmap and larger refactor TODOs

Small hardening changes belong in normal issues. The items below intentionally
remain TODOs because they alter security boundaries, storage guarantees,
hardware support or multiple public interfaces.

## P0 — production release blockers

- [x] Implement AES-256-GCM authenticated encryption for LoRa history and ACK frames.
- [x] Provision a random per-device LoRa key and reject monotonic boot/sequence replays.
- [ ] Replace the routing hash with a random public identifier and derive purpose-separated keys.
- [ ] Add QR bootstrap, automated key rotation/revocation and a gateway-blind ciphertext design.
- [x] Require BLE Secure Connections/MITM and an authenticated application session.
- [x] Show temporary first-boot credentials and random per-session BLE PINs on-device instead of requiring serial access.
- [x] Add physically bounded BLE first-claim and authenticated phone UI credential replacement.
- [x] Add a supported authenticated BLE onboarding client with claiming,
      credential replacement, transactional configuration and recovery actions.
- [ ] Add QR bootstrap and fleet credential/key recovery and rotation policy.
- [ ] Replace password-only ArduinoOTA with signed images, Secure Boot v2, flash/NVS encryption and documented eFuse/key custody.
- [x] Require an application-level archiver receipt before gateway dedup advancement and tracker ACK; retries remain idempotent across QoS-0 loss.
- [ ] Port the gateway to a current ESP32-S3/SX1262 board and share its RadioLib driver with the tracker.
- [ ] Add broker/browser integration tests and hardware-in-the-loop tests for both boards.
- [ ] Run multi-day battery, GNSS, deep-sleep, RF-loss, queue-overflow and reconnect qualification.
- [x] Enforce the Germany band-48 frequency, conducted-power and rolling-hour airtime profile across tracker, gateway and repeater transmissions.
- [x] Add RTC history metadata with magic, schema, bounds and CRC recovery.
- [ ] Move the tracker unacknowledged queue to a wear-levelled, power-loss-safe flash journal with atomic ACK checkpoints and boot-epoch continuity.
- [ ] Add fault-injection/HIL coverage for retained-memory corruption and brownouts.
- [ ] Select a software/hardware license and publish a vulnerability-response policy.

## P1 — reliability and operations

- [ ] Add fleet health metrics, structured logs and alerting for stale devices, low battery and storage failures.
- [ ] Add database backup/restore, integrity checking and configurable retention tooling.
- [ ] Add configuration export/import with encrypted secrets and an auditable device inventory.
- [ ] Add safe firmware rollout groups, rollback metadata and minimum-version enforcement.
- [ ] Add factory acceptance tests for GNSS, radio, current draw, battery gauge and enclosure seals.
- [ ] Define queue overflow priorities and explicit data-loss telemetry.
- [ ] Add multi-gateway ACK collision avoidance and reception-quality analytics.
- [ ] Add CAD/channel-busy retry and hardware qualification for hidden repeater nodes that cannot hear the selected forwarder.
- [ ] Persist conservative repeater airtime credit/debt across power loss without creating an NVS wear or reboot-reset loophole.

## P1 — application

- [x] Build an authenticated onboarding UI for tracker BLE (gateway provisioning remains on its captive portal).
- [x] Add automatic bounded history pagination; background synchronization and export remain future work.
- [x] Add a geographical map with an offline grid, opt-in online tiles and imported local raster PMTiles.
- [ ] Add encrypted local secret storage protected by platform biometrics or PIN (currently session-only).
- [x] Add open-app alerts for stale trackers, low battery and unusual movement.
- [ ] Add server-backed closed-app push and explicit gateway heartbeat/fleet-health alerts.
- [ ] Build the deferred mobile app from generated shared schemas.

## P2 — maintainability and expansion

- [ ] Move duplicated embedded headers into a reusable PlatformIO library.
- [ ] Generate C++, Python, JavaScript and mobile models/test vectors from one schema source.
- [ ] Split the large tracker and gateway sketches into testable services and hardware adapters.
- [ ] Rename the remaining historical embedded filenames and C++ namespaces as part of the shared-library refactor.
- [ ] Fuzz LoRa, MQTT and onboarding parsers and timestamp chains.
- [ ] Add altitude, speed, heading, HDOP, fix quality and radio metadata.
- [x] Extract deterministic radio/tracker/relay policies into a portable C++ core and compile it to WASM for the browser Network Lab.
- [x] Add a deterministic visual network simulator with configurable devices, waypoint movement, obstacles, environment, collisions, repeaters and an in-browser MQTT/archive service.
- [ ] Calibrate simulator propagation, foliage/building loss, GNSS and current models from repeatable field/HIL measurements and version the resulting site profiles.
- [x] Define and implement keyless bounded relay forwarding, hop limits,
      duplicate suppression, reverse ACK propagation and trust boundaries.
- [ ] Qualify repeater RF topology, collision behavior, legal airtime and power
      hardware with a multi-node hardware-in-the-loop testbed.
- [ ] Revisit 32-bit Unix timestamps before 2106.

## Hardware backlog

- [ ] Integrate a low-power wake-on-motion accelerometer.
- [ ] Integrate a real fuel gauge and temperature-aware battery policy.
- [ ] Evaluate a current-lifecycle secure element after the key architecture is defined.
- [ ] Validate antenna placement, body attenuation, enclosure impact and mounting safety.
- [ ] Produce a custom protected power design and IP-rated enclosure after prototype measurements.
