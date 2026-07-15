# Security

## Deployment status

Do not expose real location data to an anonymous or public broker. This release
authenticates radio and BLE configuration traffic, but firmware trust, durable
delivery, lifecycle key management and hardware qualification remain blockers.

## Implemented controls

- Unique admin credential generated on erased first boot (or injected per device)
- Per-tracker AES-256-GCM history and ACK frames with authenticated routing headers
- Monotonic boot/sequence replay rejection, random per-boot nonce prefixes and
  a distinct transmit counter for every encryption attempt
- BLE Secure Connections with MITM protection plus application-session authentication
- Gateway configuration mutations additionally gated by a physical button
- OTA disabled unless a separate 64-character SHA-256 password hash is supplied
- Gateway MQTT uses certificate-verified TLS by default
- Plain MQTT requires an explicit `allow_insecure_mqtt=true` test override
- Unauthenticated telnet logging disabled by default
- Archiver refuses plaintext MQTT unless explicitly opted in
- Revisioned configuration, CRC validation, backup and rollback
- RTC history metadata, bounds and CRC validation after every deep-sleep wake
- Strict current-schema parsing, registered-device routing and stable dedup IDs
- Secrets omitted from configuration reads and MQTT passwords not persisted by the PWA

## Open release blockers

1. Gateway MQTT publishing uses QoS 0 and has no durable outage queue or
   archiver-confirmed receipt before tracker acknowledgement.
2. ArduinoOTA password authentication and physical USB flashing do not provide
   a signed firmware trust chain.
3. ESP32 Secure Boot v2, flash/NVS encryption and production eFuse/key-custody
   policy are not configured.
4. The public FNV-1a device hash is predictable and provides routing only.
5. Gateways currently hold tracker traffic keys; there is no secure element,
   QR bootstrap, purpose-separated key derivation, rotation or revocation flow.
6. BLE pairing uses an attended PIN derived from the admin credential; fleet
   onboarding still needs a purpose-built client and recovery policy.
7. Broker and archiver can see plaintext locations; authorization relies on broker ACLs.

## Minimum field-trial rules

- Use a private broker with trusted TLS certificates, named accounts and least-privilege ACLs.
- Give every physical device a different admin credential and LoRa key.
- Keep provisioning and device HTTP endpoints on an isolated management network.
- Leave BLE debug disabled except during attended setup.
- Do not enable the test-only plaintext MQTT or telnet build switches.
- Treat location history, backups and logs as sensitive personal data.
- Rotate credentials after a lost device or an exposed provisioning record.

## Required production design

The radio protocol now uses a random per-device key, AES-256-GCM, non-repeating
per-boot nonce prefixes, per-encryption counters and authenticated ACK ranges. The
production design still needs purpose-separated derived keys, a random public
identifier, key rotation/revocation and a gateway architecture that can route
ciphertext without holding tracker keys. Production firmware should use
ESP32-S3 Secure Boot v2 together with flash and NVS encryption, signed OTA and
per-device key material. These changes are tracked in [ROADMAP.md](ROADMAP.md).

## Reporting

Report suspected vulnerabilities privately through the repository's GitHub
security advisory interface. Do not publish working exploits before deployed
devices can be updated.
