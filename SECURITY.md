# Security status

## Prototype warning

This release protects configuration integrity against accidental corruption,
but it does **not** provide end-to-end confidentiality or authenticity for
location data. Do not treat it as suitable for publishing real tracker data on
an anonymous public broker.

## Present protections

- CRC-protected, revisioned configuration with backup and rollback
- Gateway configuration write window gated by a physical button
- Broker username/password support
- MQTT TLS support in the Python archiver when configured
- Web app does not persist the MQTT password
- Version/magic/device checks on LoRa frames
- Stable deduplication IDs

These are useful operational controls, not a complete security model.

## Known gaps

1. LoRa telemetry is plaintext and unauthenticated.
2. ACK frames are unauthenticated and could be forged to clear a queue.
3. The current FNV-1a device hash is predictable and is only a routing ID.
4. The default onboarding AP password is shared: `EquineSetup!`.
5. BLE onboarding does not yet require cryptographic pairing or an authenticated
   application session.
6. HTTP onboarding is plaintext on the local AP/LAN.
7. MQTT data is visible to the broker and archiver.
8. Gateway firmware uses PubSubClient QoS-0 publishing.
9. OTA authentication/signing is not fully specified or enforced.
10. Gateway command and history-response authorization depend on broker ACLs.

## Minimum deployment rules

- Use a private broker or a dedicated account on a managed broker.
- Enable TLS and verify certificates.
- Disable anonymous access.
- Restrict ACLs by tracker, gateway, archiver and application role.
- Replace the onboarding AP password at build time.
- Keep OTA access on a trusted network.
- Do not expose the device HTTP interfaces to the internet.
- Treat location history as sensitive personal data.

## Planned cryptographic design

A future protocol revision should introduce:

- random per-device 128- or 256-bit provisioning secret;
- random public device identifier/routing hash, distinct from the secret;
- AEAD for history, ACK and command frames;
- nonces derived from a non-repeating boot/session counter, message type and
  sequence number;
- replay windows and authenticated ACK ranges;
- HKDF-based purpose-specific keys;
- key version and rotation support;
- QR provisioning payload containing public identity and protected key material;
- encrypted app-to-app export/import bundles;
- optional archiver decryption authorization separate from gateway routing.

Gateways and relays should be able to route ciphertext without tracker keys.
Whether the archiver stores ciphertext or plaintext should be a per-tracker
policy.

## Reporting issues

This is a private prototype bundle with no selected public license or formal
security reporting channel. Record discovered issues in the project tracker and
do not publish exploitable details before deployed devices are updated.
