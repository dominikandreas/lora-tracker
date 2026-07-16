# Onboarding and configuration

## Prepare a device

1. Copy `secrets.example.h` to `secrets.h` in the relevant firmware directory,
   or install a generic release with the [browser flasher](FLASHING.md).
2. Leave `factory_admin_password` empty for a generic image. On erased flash,
   the device generates a unique 20-character credential and prints it while
   the unprovisioned setup AP starts in the attended serial log. A factory may inject a different 12+
   character value into each device build instead. Record it in the protected
   device inventory.
3. Set `ota_password_hash` to the 64-character lowercase or uppercase SHA-256
   hash of a separate password. Leave it empty to keep OTA disabled.
4. For a gateway, set a TLS broker port (normally 8883) and paste the broker root
   CA into the runtime `mqtt_ca_certificate` field. A source build may seed the
   same field from `secrets.h`. Leave `allow_insecure_mqtt=false`.
5. Build, flash and label the device ID, hardware revision, firmware commit,
   region/frequency and provisioning-record identifier.

Never commit `secrets.h`, `.env`, broker CA private keys or provisioning exports.
The CA certificate is public; client and signing keys are not.

## Tracker first boot

An unprovisioned tracker derives a setup ID from the low 24 bits of its eFuse
MAC, generates a random 256-bit LoRa AEAD key, then exposes
`LoRaTracker-<device_id>` and BLE name `EqTrk-<device_id>`. Record the generated
admin credential from the serial console. Connect to the AP with that password,
then open the AP address shown on serial output. HTTP authentication uses user
`admin` and the same password.

Read `GET /api/v1/config`, retain its `revision`, and submit a transactional
`POST /api/v1/config` with `expected_revision`. Configure identity, Wi-Fi, the
regional LoRa parameters, GNSS thresholds, transmission policy and sleep
intervals. Reboot, then verify normal GNSS fixes, LoRa ACKs and increasing point
sequences before mounting the device.

### Tracker button controls

- After a hard boot, release GPIO 0, then hold it for 1.5 seconds during the
  five-second on-screen setup window to start Wi-Fi onboarding.
- A short press wakes the display and advances through status, GNSS, radio and debug pages.
- Hold 4–8 seconds, release, then short-press within ten seconds to reset distance and queued history.
- Hold 8–12 seconds, release, then confirm to toggle the bounded BLE debug session.
- Hold at least 12 seconds, release, then confirm to factory-reset configuration and runtime state.

The confirmation step prevents a single accidental long press from erasing
state. Never hold GPIO 0 while powering or resetting an ESP32-S3 because it can
enter the ROM downloader instead of the application.

BLE requires LE Secure Connections with MITM protection, then an application
session command `AUTH <admin-password>` before any configuration or debug logs
are exposed. The six-digit pairing PIN is shown in the attended serial log when
the bounded BLE window opens. BLE still lacks the planned QR bootstrap,
purpose-separated provisioning key and fleet key-rotation workflow; disable BLE
debug after setup.

## Gateway first boot

An unprovisioned gateway derives its setup ID from its eFuse MAC and exposes
`LoRaGateway-<gateway_id>` with an empty tracker allowlist. Authenticate as
`admin`, configure Wi-Fi, TLS MQTT settings, the same regional LoRa settings as
the trackers, and a registry entry for every allowed tracker. IDs must be unique
canonical lowercase strings.

For each tracker registry entry, copy `lora_aead_key` from the tracker's
authenticated `GET /api/v1/config` response into
`tracker.<index>.lora_aead_key` on the gateway. The value is exactly 64 hex
characters. Treat it as a secret: it authenticates and decrypts that tracker's
history and ACK traffic. Never reuse a key between trackers.

On a provisioned gateway, hold the USER button for five seconds to open the
ten-minute write window. Authentication is still required. Read-only HTTP routes
also require authentication so status, logs and device inventory are not
available anonymously on the LAN.

## Transaction rules

- Always read immediately before writing and send `expected_revision`.
- On HTTP 409, reload, merge and retry; never blindly increment a guessed revision.
- Omit a secret, send an empty value, or send `__KEEP__` to retain it.
- Send `__CLEAR__` only when intentionally erasing a secret.
- A valid update backs up the previous configuration and increments the revision.
- Rollback restores the backup as a new revision; it never moves the counter backwards.
- Factory reset requires `confirm=FACTORY_RESET` and returns the device to onboarding.

The complete endpoint and field reference is in
[`protocols/ONBOARDING_API.md`](protocols/ONBOARDING_API.md).

## Repeater first boot

An erased repeater exposes `lora-repeater-<suffix>` and prints its generated
administrator password and AP address at 115200 baud. Authenticate as `admin`,
then configure a unique ID and the exact radio settings used by the tracker and
gateway. Configure the local hop cap, priority delay/slots, duplicate lifetime,
airtime budget and heartbeat interval. Repeating remains disabled until the
configuration validates and is saved.

To reopen the portal, hold USER for at least 1.5 seconds during boot. A
configured repeater closes it after ten minutes. Repeaters do not receive any
tracker AEAD key. Placement, power and end-to-end acceptance requirements are
in [repeaters](REPEATERS.md).

## Post-onboarding acceptance checklist

- Device has a unique generated or factory-injected admin credential; any
  first-boot log containing it has been closed and handled as a secret.
- Gateway reports verified TLS and rejects plaintext when the test override is false.
- Tracker and gateway have identical frequency, bandwidth, spreading factor,
  coding rate, preamble and sync word; every repeater matches them.
- Relay hop limits are the minimum needed, the tracker ACK window covers the
  measured round trip, and repeater queue-drop/airtime-deferral counters remain acceptable.
- Only registered tracker hashes with matching per-device AEAD keys are accepted.
- `timestamp_valid`, location, battery and sequence values are plausible.
- Archiver availability is online, gateway archive-confirmation ACLs are correct,
  and a history request ends with `final=true`.
- Factory-reset and rollback procedure has been tested on a spare unit.
- Provisioning record, recovery credentials and firmware commit are stored securely.
