# Production readiness

## Decision

Current status: **not production ready**. The repository now fails closed on
unsupported schemas and insecure default network settings, but the remaining
P0 work changes the end-to-end security and delivery model and cannot be safely
papered over with configuration.

## Completed in this hardening pass

- Removed packet, history, point and history-request schema compatibility paths.
- Changed the default MQTT namespace to `lora-tracker` across all components.
- Added certificate-verified TLS to gateway MQTT; plaintext requires explicit test opt-in.
- Made archiver TLS the default and added CA/config validation.
- Required unique 12+ character onboarding credentials and HTTP authentication.
- Disabled OTA unless a password hash is provisioned and disabled telnet by default.
- Strengthened SQLite foreign-key, timeout and durability settings.
- Added strict browser schema handling and complete service-worker asset caching.
- Added pinned PlatformIO projects and native tracker/gateway contract simulation.
- Added onboarding, hardware, operations and simulation documentation.

## Release blockers

| Area | Missing production behavior | Acceptance evidence |
|---|---|---|
| LoRa security | AEAD, replay prevention and authenticated ACK ranges | Adversarial vectors, fuzzing and over-the-air replay/forgery tests |
| BLE provisioning | Authenticated session and protected key bootstrap | Pairing/replay tests and device-recovery procedure |
| Firmware trust | Signed OTA, Secure Boot v2, flash/NVS encryption and key custody | Factory provisioning record and failed unsigned-image tests |
| Gateway delivery | Durable outage queue and confirmed broker delivery | Power-loss/reconnect test with zero silent point loss |
| Hardware | Current gateway board port and qualified tracker design | HIL CI, RF/power/environmental reports and factory tests |
| Operations | Monitoring, backup/restore, alerting and credential rotation | Runbooks exercised in a staging deployment |
| Compliance | Region-specific radio, privacy, battery and product obligations | Review for every market and deployment context |

## Field-trial gate

A controlled field trial may proceed only on a private isolated broker/network,
with unique credentials, TLS, no public HTTP/BLE exposure, attended devices and
an explicit expectation that LoRa traffic can be observed or forged. Do not use
the system as a safety device or as the sole means of locating an animal.

The implementation sequence for large changes is maintained in
[`ROADMAP.md`](../ROADMAP.md).
