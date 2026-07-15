# Security

## Deployment status

Do not expose real location data to an anonymous or public broker. This release
has materially safer defaults, but unauthenticated LoRa and BLE paths remain
production release blockers.

## Implemented controls

- Unique per-device onboarding password required for WPA2 and HTTP Basic auth
- Gateway configuration mutations additionally gated by a physical button
- OTA disabled unless a separate 64-character SHA-256 password hash is supplied
- Gateway MQTT uses certificate-verified TLS by default
- Plain MQTT requires an explicit `allow_insecure_mqtt=true` test override
- Unauthenticated telnet logging disabled by default
- Archiver refuses plaintext MQTT unless explicitly opted in
- Revisioned configuration, CRC validation, backup and rollback
- Strict current-schema parsing, registered-device routing and stable dedup IDs
- Secrets omitted from configuration reads and MQTT passwords not persisted by the PWA

## Open release blockers

1. LoRa history and ACK frames are plaintext and unauthenticated. A forged ACK
   can clear the tracker queue.
2. BLE provisioning commands do not authenticate the client or protect against replay.
3. Gateway MQTT publishing uses QoS 0 and has no durable outage queue.
4. ArduinoOTA password authentication does not provide a signed firmware trust chain.
5. The public FNV-1a device hash is predictable and provides routing only.
6. Broker and archiver can see plaintext locations; authorization relies on broker ACLs.
7. ESP32 secure boot, flash/NVS encryption and production eFuse policy are not configured.

## Minimum field-trial rules

- Use a private broker with trusted TLS certificates, named accounts and least-privilege ACLs.
- Give every physical device a different onboarding and OTA password.
- Keep provisioning and device HTTP endpoints on an isolated management network.
- Leave BLE debug disabled except during attended setup.
- Do not enable the test-only plaintext MQTT or telnet build switches.
- Treat location history, backups and logs as sensitive personal data.
- Rotate credentials after a lost device or an exposed provisioning record.

## Required production design

The planned protocol must use a random per-device secret, purpose-separated
keys, AEAD for history/ACK/command frames, non-repeating counters, replay
windows, authenticated ACK ranges, key rotation and revocation. Gateways should
route ciphertext without needing tracker keys. Production firmware should use
ESP32-S3 Secure Boot v2 together with flash and NVS encryption, signed OTA and
per-device key material. These changes are tracked in [ROADMAP.md](ROADMAP.md).

## Reporting

Report suspected vulnerabilities privately through the repository's GitHub
security advisory interface. Do not publish working exploits before deployed
devices can be updated.
