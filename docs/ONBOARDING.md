# Onboarding and configuration

## Prepare a device

1. Copy `secrets.example.h` to `secrets.h` in the relevant firmware directory,
   or install a generic release with the [browser flasher](FLASHING.md).
2. Leave `factory_admin_password` empty for a generic image. On erased flash,
   the device generates a unique 20-character credential. Trackers and gateways
   show it on their local display during attended setup; diagnostic serial also
   records it. A factory may inject a different 12+ character value into each
   device build instead. Record the final credential in the protected device
   inventory.
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
`LoRaTracker-<device_id>` and BLE name `LT-<device_id>`. Its display shows
the temporary AP credential, the current BLE pairing PIN and the setup address.
Connect a phone to the AP and open `http://192.168.4.1`, or connect over BLE
using the displayed PIN. The generated credential no longer has to be obtained
from a serial console or retained permanently. Replace it from **Device access**
before mounting the tracker; the replacement becomes both the HTTP credential
for user `admin` and the fallback AP password after reboot.

Read `GET /api/v1/config`, retain its `revision`, and submit a transactional
`POST /api/v1/config` with `expected_revision`. Configure identity, Wi-Fi, the
regional LoRa parameters, GNSS thresholds, transmission policy and sleep
intervals. Reboot, then verify normal GNSS fixes, LoRa ACKs and increasing point
sequences before mounting the device.

### Tracker button controls

- After a hard boot, release GPIO 0, then hold it for 1.5 seconds during the
  five-second on-screen setup window to start Wi-Fi onboarding.
- A short press wakes the display and advances through status, GNSS, radio and
  debug pages.
- Hold for at least 0.9 seconds and release to run the action printed at the
  bottom of the current page:
  - **Status:** request a distance reset, then short-press within ten seconds
    to confirm. Unacknowledged tracking history is retained.
  - **GPS:** acquire a position. One second requests a 15-second acquisition;
    longer holds increase the requested powered acquisition window up to three
    minutes. Release when the displayed duration is sufficient.
  - **Radio:** immediately try to transmit queued history and listen for an
    authenticated ACK. The ACK countdown is shown on this page. Manual sends
    bypass batching and retry backoff, but never the Germany airtime limiter.
  - **Debug:** toggle persistent BLE debug logging. Enabling BLE performs a
    clean one-second restart before opening the bounded connection window.

Distance confirmation prevents an accidental hold from clearing the daily
counter. Factory reset remains available through the authenticated onboarding
API. Never hold GPIO 0 while powering or resetting an ESP32-S3 because it can
enter the ROM downloader instead of the application.

BLE requires LE Secure Connections with MITM protection, then an application
session before any configuration or debug logs are exposed. Each bounded setup
window generates a new random six-digit BLE pairing PIN and shows it on the
tracker. On an unprovisioned tracker, `CLAIM <new-credential>` both replaces the
generated credential and authenticates that BLE session. A provisioned tracker
uses `AUTH <admin-password>`. `CLAIM` is rejected outside the physically opened
setup/onboarding window. The Pages-hosted Tracker Console provides a supported
Chromium Web Bluetooth client for these commands. BLE still lacks QR bootstrap,
owner-key enrollment, a purpose-separated provisioning key and fleet
key-rotation workflow. An authenticated, physically opened BLE session can use
`SET_CREDENTIAL` to replace the administrator credential. Disable BLE debug
after setup.

## Gateway first boot

An unprovisioned gateway derives its setup ID from its eFuse MAC and exposes
`LoRaGateway-<gateway_id>` with an empty tracker allowlist. Authenticate as
`admin` using the temporary credential shown on its OLED. Connect a phone to
that AP and open `http://192.168.4.1`; no serial console is needed. Configure
Wi-Fi, TLS MQTT settings, the same regional LoRa settings as the trackers, and a
registry entry for every allowed tracker. Replace the generated credential from
**Gateway access** while the physical write window is open. IDs must be unique
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

- Credential replacement is deliberately outside the revisioned device config;
  it is write-only, requires current authentication, and takes effect after reboot.

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
