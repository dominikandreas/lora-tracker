# Security

## Deployment status

Do not expose real location data to an anonymous or public broker. This release
authenticates radio frames and authorizes local device management, but firmware
trust, durable delivery, lifecycle key management and hardware qualification
remain blockers for an unattended production deployment.

## Device ownership

A factory-reset device is deliberately open for attended claim. The app
generates a random 256-bit owner key and installs it once over the custom BLE
GATT protocol or the temporary setup Wi-Fi network. There is no device
password, PIN, operating-system Bluetooth pairing/bond, or separate OTA key.

After claim, the same owner key authorizes BLE commands, local HTTP management
and configuration-mode ArduinoOTA. Anyone with the key has full device
authority. Android stores it in Keystore-backed storage; the PWA uses
origin-scoped `localStorage`; an explicit QR export transfers it to another
installation. Firmware never returns it. Factory reset is the current
revocation mechanism.

The custom BLE and local HTTP transports provide authorization, not transport
confidentiality. Claim devices while physically attended and manage them only
at close range or on a trusted local network. A nearby/on-path attacker during
factory-reset claim can observe or race an unauthenticated claim; this is an
accepted consequence of passwordless, confirmation-free factory onboarding.

## Implemented controls

- Per-device random owner key with fail-closed one-time claim and constant-time
  comparison in firmware.
- Per-tracker AES-256-GCM history and ACK frames with authenticated routing
  headers.
- Monotonic boot/sequence replay rejection, random per-boot nonce prefixes and
  a distinct transmit counter for every encryption attempt.
- Gateway MQTT certificate verification by default; plaintext MQTT requires an
  explicit test override.
- OTA available only in a bounded configuration window and authenticated by
  the owner key.
- Unauthenticated telnet logging disabled by default.
- Revisioned configuration, CRC validation, backup and rollback.
- RTC history metadata, bounds and CRC validation after deep-sleep wake.
- Strict current-schema parsing, registered-device routing and stable dedup IDs.
- Wi-Fi/MQTT passwords omitted from device configuration reads; Android stores
  the MQTT password in Keystore-backed storage, while the PWA stores it in
  origin-scoped localStorage when the operator chooses browser persistence.

## Open release blockers

1. ArduinoOTA authentication and physical USB flashing do not establish a
   signed firmware trust chain.
2. ESP32 Secure Boot v2, flash/NVS encryption and production eFuse/key-custody
   policy are not configured.
3. Passwordless factory claim has no out-of-band device confirmation and is
   vulnerable to a nearby first-claimer or active claim race.
4. BLE management and HTTP on the setup/LAN network do not encrypt the owner
   key in transit; application challenge-response and protected local transport
   are future hardening work.
5. The public FNV-1a device hash is predictable and provides routing only.
6. Gateways hold tracker traffic keys; there is no secure element,
   purpose-separated key derivation, rotation, or selective revocation flow.
7. Broker and archiver can see plaintext locations; authorization relies on
   broker ACLs.

## Minimum field-trial rules

- Keep a factory-reset device physically attended until claim completes.
- Use a private broker with trusted TLS certificates, named accounts and
  least-privilege ACLs.
- Keep setup APs and device HTTP endpoints on a trusted management network.
- Protect exported owner-key QR codes like physical master keys.
- Leave BLE debug disabled except during attended setup and use bounded
  configuration windows for OTA.
- Do not enable test-only plaintext MQTT or telnet build switches outside an
  isolated network.
- Treat location history, backups and logs as sensitive personal data.
- Factory-reset and reclaim a device after an owner key or QR export is exposed.

## Required production design

The radio protocol uses a random per-device key, AES-256-GCM, non-repeating
per-boot nonce prefixes, per-encryption counters and authenticated ACK ranges.
The production design still needs signed updates, Secure Boot v2, flash/NVS
encryption, owner-key rotation/revocation, protected management transport,
purpose-separated derived keys and a gateway architecture that can route
ciphertext without holding tracker keys. These items are tracked in
[ROADMAP.md](ROADMAP.md).

## Reporting

Report suspected vulnerabilities privately through the repository's GitHub
security advisory interface. Do not publish working exploits before deployed
devices can be updated.
