# Changelog

## Unreleased — production-hardening pass

- Removed support for all superseded LoRa, point and history-request schemas.
- Changed the product, package, workspace and default MQTT namespace to LoRa Tracker.
- Added certificate-verified gateway MQTT TLS and explicit plaintext opt-in.
- Required unique onboarding credentials for AP and HTTP administration.
- Disabled OTA without a password hash and disabled telnet logging by default.
- Strengthened archiver validation and SQLite durability settings.
- Fixed browser defaults, strict schema validation and offline asset caching.
- Added pinned tracker/gateway PlatformIO builds and embedded contract simulation.
- Fixed an ESP32 RTC slow-memory link overflow by sizing the history queue to 448 points.
- Added onboarding, configuration, hardware, operations, security and production-readiness guidance.
- Added per-tracker AES-256-GCM history/ACK frames and monotonic replay rejection.
- Added a distinct retained transmit counter so retries cannot reuse GCM nonces.
- Added BLE Secure Connections, application-session authentication and generated first-boot credentials.
- Added transactional runtime MQTT root-CA provisioning for generic gateway images.
- Added RTC retained-history metadata, bounds and CRC validation.
- Added pinned GitHub Actions CI/release builds, checksums, provenance, merged
  ESP Web Tools images and browser-flashing instructions.
- Added smart keyless repeater firmware with bounded multi-hop history/ACK forwarding.
- Fixed ACK AES-GCM nonce-domain reuse across tracker boot epochs.
- Added archive-confirmed SQLite delivery before gateway dedup or tracker ACK progression.
- Added a fail-closed NTP clock gate before certificate-validated gateway MQTT TLS.
- Enforced the Germany 868.0–868.6 MHz band-48 profile, 14 dBm conducted cap and 1% rolling-hour airtime limit on every transmitter.
- Bumped embedded configuration schema to 5; devices must be re-onboarded into the Germany-only radio profile after upgrade.
- Preserved unacknowledged points at daily rollover, stopped full-queue overwrite and made provisioning/key-counter persistence fail closed.
- Fixed repeater post-failure suppression and credential retention on factory reset.
- Hardened browser validation/rendering, reconnect framing, history pagination, local retention and service-worker updates.
- Anchored archiver topic parsing to nested configured base topics and documented remaining power-loss queue work.

## Initial implementation

- Added adaptive GNSS acquisition, movement filtering and deep-sleep policies.
- Added compressed offline history, ACK-based LoRa delivery and retry backoff.
- Added multi-tracker gateway routing, MQTT publishing and deduplication.
- Added transactional configuration, Wi-Fi/BLE onboarding and rollback.
- Added the SQLite archiver, browser PWA and deterministic system simulator.
