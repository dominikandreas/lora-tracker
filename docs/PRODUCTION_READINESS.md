# Production readiness

## Decision

Current status: **not production ready**. The repository now fails closed on
unsupported schemas, invalid radio authentication and insecure default network
settings. Remaining P0 work still changes firmware trust, key lifecycle,
delivery guarantees and hardware qualification and cannot be papered over.

## Completed in this hardening pass

- Removed packet, history, point and history-request schema compatibility paths.
- Changed the default MQTT namespace to `lora-tracker` across all components.
- Added certificate-verified TLS and transactional runtime root-CA provisioning
  to gateway MQTT; plaintext requires explicit test opt-in.
- Made archiver TLS the default and added CA/config validation.
- Required unique 12+ character onboarding credentials and HTTP authentication.
- Disabled OTA unless a password hash is provisioned and disabled telnet by default.
- Strengthened SQLite foreign-key, timeout and durability settings.
- Added strict browser schema handling and complete service-worker asset caching.
- Added pinned PlatformIO projects and native tracker/gateway contract simulation.
- Added onboarding, hardware, operations and simulation documentation.
- Added per-tracker AES-256-GCM history/ACK protection, per-encryption nonce
  counters and monotonic point replay rejection.
- Added BLE Secure Connections plus application-session authentication.
- Added generated first-boot credentials and random per-tracker radio keys.
- Added RTC history metadata/bounds/CRC validation.
- Added CI, tagged release packaging, checksums, provenance and browser-flash images.

## Release blockers

| Area | Missing production behavior | Acceptance evidence |
|---|---|---|
| Key lifecycle | Random public IDs, purpose-separated keys, QR bootstrap, rotation/revocation and reduced gateway key exposure | Provision/rotate/revoke/recover tests and key-custody records |
| BLE provisioning | Supported onboarding client and QR/recovery workflow beyond the authenticated firmware session | Pairing/replay/recovery tests on supported phones |
| Firmware trust | Signed OTA, Secure Boot v2, flash/NVS encryption and key custody | Factory provisioning record and failed unsigned-image tests |
| Gateway delivery | Durable outage queue and confirmed broker delivery | Power-loss/reconnect test with zero silent point loss |
| Hardware | Current gateway board port and qualified tracker design | HIL CI, RF/power/environmental reports and factory tests |
| Operations | Monitoring, backup/restore, alerting and credential rotation | Runbooks exercised in a staging deployment |
| Compliance | Region-specific radio, privacy, battery and product obligations | Review for every market and deployment context |

## Field-trial gate

A controlled field trial may proceed only on a private isolated broker/network,
with unique credentials/keys, TLS, no public HTTP exposure and attended BLE
onboarding. Radio traffic is encrypted and authenticated, but traffic analysis
remains possible and a compromised gateway exposes its registered tracker keys.
Do not use the system as a safety device or sole means of locating an animal.

The implementation sequence for large changes is maintained in
[`ROADMAP.md`](../ROADMAP.md).
