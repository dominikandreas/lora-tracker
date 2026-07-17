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
- Added strict browser point validation, text-only rendering, automatic history pagination, bounded IndexedDB retention and update-first offline caching.
- Added pinned PlatformIO projects and native tracker/gateway/repeater contract simulation.
- Added onboarding, hardware, operations and simulation documentation.
- Added per-tracker AES-256-GCM history/ACK protection, per-encryption nonce
  counters and monotonic point replay rejection.
- Added BLE Secure Connections plus application-session authentication.
- Added generated first-boot credentials and random per-tracker radio keys.
- Added on-device first-boot credentials, random per-session BLE PINs, bounded BLE claiming and authenticated credential replacement.
- Fresh tracker and gateway onboarding no longer requires an attached serial console.
- Added RTC history metadata/bounds/CRC validation.
- Added CI, tagged release packaging, checksums, provenance and browser-flash images.
- Added keyless bounded repeater forwarding for encrypted history and ACKs with
  deterministic suppression, queue bounds, post-success duplicate caching and retries.
- Added archiver-confirmed gateway delivery: dedup state and tracker ACKs advance
  only after every new point has a durable SQLite receipt.
- Enforced the Germany band-48 channel, 14 dBm conducted-power cap and 1% rolling-hour airtime ceiling on every transmitter.
- Preserved unacknowledged points across daily rollover and stopped queue-full overwrite; new samples now fail closed when RTC storage is full.

## Release blockers

| Area | Missing production behavior | Acceptance evidence |
|---|---|---|
| Key lifecycle | Random public IDs, purpose-separated keys, QR bootstrap, rotation/revocation and reduced gateway key exposure | Provision/rotate/revoke/recover tests and key-custody records |
| BLE provisioning | Supported onboarding client and QR/recovery workflow beyond the authenticated firmware session | Pairing/replay/recovery tests on supported phones |
| Firmware trust | Signed OTA, Secure Boot v2, flash/NVS encryption and key custody | Factory provisioning record and failed unsigned-image tests |
| Tracker queue durability | Unacknowledged telemetry currently survives deep sleep only, not power loss or hard reset | Wear-levelled flash-journal fault injection with zero sequence reuse or silent point loss |
| Hardware | Current gateway board port, qualified tracker design and installed repeater power/RF design | HIL CI, RF/power/environmental reports and factory tests |
| Repeater RF | Hidden-node/capture behavior, multi-hop ACK timing and regulatory airtime remain unqualified | Multi-node installed-topology tests at maximum traffic and worst radio profile |
| Operations | Monitoring, backup/restore, alerting and credential rotation | Runbooks exercised in a staging deployment |
| Compliance | Germany radio limits are enforced in software, but installed ERP, RED/FuAG, privacy, battery and product obligations remain unqualified | Accredited RF/EMC/safety evidence and legal review for the exact product and installation |

## Field-trial gate

A controlled field trial may proceed only on a private isolated broker/network,
with unique credentials/keys, TLS, no public HTTP exposure and attended BLE
onboarding. Radio traffic is encrypted and authenticated, but traffic analysis
remains possible and a compromised gateway exposes its registered tracker keys.
Repeaters are keyless but can still amplify structurally valid hostile traffic
until their local airtime budget is exhausted; the protocol is not jam-resistant.
The field-trial installation must follow the
[Germany radio profile](RADIO_COMPLIANCE_DE.md), including its installed-antenna ERP calculation.
Do not use the system as a safety device or sole means of locating an animal.

The implementation sequence for large changes is maintained in
[`ROADMAP.md`](../ROADMAP.md).
