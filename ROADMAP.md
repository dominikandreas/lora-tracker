# Roadmap and future TODOs

Flutter development is intentionally deferred. The order below focuses first on
security, recoverability and protocol stability.

## P0 — before public or unattended deployment

- [ ] Compile tracker and gateway with a pinned ESP32/Arduino or PlatformIO
      toolchain and record exact library versions.
- [ ] Add CI builds for both tracker hardware branches and the gateway.
- [ ] Run multi-day battery, GNSS, deep-sleep, queue-overflow and reconnect tests
      on real hardware.
- [ ] Add authenticated encryption to LoRa history, ACK and command frames.
- [ ] Replace FNV routing identity with a random public device identifier while
      preserving migration aliases.
- [ ] Replace the shared onboarding password with a unique provisioning secret.
- [ ] Add authenticated BLE/HTTP provisioning sessions and replay protection.
- [ ] Add signed firmware/version metadata and authenticated OTA updates.
- [ ] Move gateway MQTT publishing to QoS 1 or add an application-level durable
      receipt before advancing delivery state.
- [ ] Define EU duty-cycle accounting and enforce airtime budgets across retries.
- [ ] Add crash-safe RTC history metadata with magic/version/CRC and migration.
- [ ] Test factory reset, rollback, power loss during writes and downgrade paths.

## P1 — multi-node product architecture

- [ ] Define relay message type with immutable message ID, hop limit, previous
      hop, deduplication cache, randomized forwarding delay and ACK policy.
- [ ] Prevent ACK collisions when several gateways hear the same tracker.
- [ ] Add gateway-to-gateway reception metadata and route-quality analytics.
- [ ] Decide whether relays use the custom protocol, Meshtastic transport, or a
      bridge. Keep tracker payload encryption independent of the relay network.
- [ ] Add MQTT broker discovery/profile support for private and public brokers.
- [ ] Add separate permissions for route consumers, configuration managers and
      archivers.
- [ ] Add tracker command/downlink schemas only after authentication exists.
- [ ] Add key rotation, revocation and lost-device recovery.

## P1 — applications and onboarding

- [ ] Add BLE and Wi-Fi onboarding/configuration UI to the web app.
- [ ] Generate/import QR provisioning records with device ID, public routing ID,
      key version and encrypted secret material.
- [ ] Add encrypted app-to-app transfer via QR and MQTT mailbox topics.
- [ ] Add configuration diff/merge UI using `expected_revision` conflict rules.
- [ ] Add automatic history pagination, background synchronization and export.
- [ ] Add a geographical map with optional offline/local tiles.
- [ ] Add alerts for stale tracker, low battery, missing gateway and unusual
      movement.
- [ ] Add local biometric/PIN protection for stored tracker secrets.
- [ ] Build the deferred Flutter app using the same schemas and test vectors.

## P1 — storage

- [ ] Define per-tracker storage policy: tracker RTC/NVS, gateway cache, app,
      archiver, or ciphertext-only cloud.
- [ ] Add optional gateway/archiver buffering during MQTT outages.
- [ ] Add an archiver-node protocol for subscribe/store/replay by tracker ID.
- [ ] Add encrypted-at-rest secrets and optionally encrypted track history.
- [ ] Add backup, restore, compaction and database-health tooling.
- [ ] Add retention policies beyond the fixed ten-day default.

## P2 — protocol and maintainability

- [ ] Move duplicated shared headers into a reusable Arduino/PlatformIO library.
- [ ] Add a machine-readable schema source and generated C++, Python, Dart and
      JavaScript models/test vectors.
- [ ] Define capability negotiation and minimum-compatible-version fields.
- [ ] Add explicit firmware/API version reporting to all status endpoints.
- [ ] Add per-point altitude, speed, heading, HDOP and fix-quality fields with
      optional delta encoding.
- [ ] Add RSSI and SNR reception metadata.
- [ ] Revisit 32-bit Unix timestamps before 2106 and document leap-second rules.
- [ ] Define queue overflow behavior and priorities for critical points.
- [ ] Add fuzzing for LoRa/MQTT parsers and malformed timestamp chains.
- [ ] Add integration tests with a real broker, multiple gateways and packet loss.
- [ ] Add observability metrics, structured logs and remote diagnostics.
- [ ] Select a software/hardware license and contribution policy.

## Hardware opportunities

- [ ] Add a low-power accelerometer with wake-on-motion interrupt.
- [ ] Measure actual current draw in every tracker state.
- [ ] Evaluate antenna, enclosure and horse-mounted orientation across terrain.
- [ ] Add secure-element support if key extraction becomes a material threat.
- [ ] Evaluate gateway SX1262 hardware and diversity/multiple receivers.
