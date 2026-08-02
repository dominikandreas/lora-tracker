# Onboarding and device ownership

LoRa Tracker devices do not use a Bluetooth PIN, OS Bluetooth bond,
administrator password, setup password, or separate OTA password. The app owns
devices with a random 256-bit **owner key**. The same key authorizes the custom
BLE protocol, local HTTP API, and configuration-mode OTA.

Anyone who obtains an owner key has full device authority. Android stores keys
in Keystore-backed secure storage. The PWA stores them in origin-scoped
`localStorage`, which is convenient but less protected. Exported access QR codes
must be handled like physical master keys.

## Factory-reset claim

A factory-reset device is intentionally claimable without authentication. Keep
it physically attended until the app confirms the claim.

1. Flash the correct release image and boot the device.
2. Open **Manage devices** in the Android app.
3. For a tracker or gateway, select it under **Nearby devices**. The app reads
   public device state before deciding whether to claim or authenticate. Do not
   pair it in Android Settings; the app uses GATT directly and Android must not
   show a PIN or bonding dialog.
4. Alternatively, join the device's open setup network and enter its IP in the
   app. Trackers use `LoRaTracker-<suffix>`, gateways use
   `LoRaGateway-<suffix>`, and repeaters use `lora-repeater-<suffix>`.
5. The app generates a 64-hex-character owner key, stores it securely, and
   sends `CLAIM`. If that one-way operation is interrupted, reconnecting reuses
   the staged key and retries `CLAIM` while the device still reports itself as
   unclaimed. Android also retries transient GATT connection failures three
   times. Claim itself does not start OTA or other memory-heavy services before
   acknowledging the app.
6. In **Reusable connection profiles**, create a Wi-Fi profile and an MQTT
   profile once. When a tracker or gateway is connected, select the appropriate
   profile directly under **Connection settings**; selection immediately fills
   the complete form. Trackers use Wi-Fi profiles, while gateways use both.
   Configure the remaining role-specific settings and press **Save and verify**.
   The MQTT profile can also be selected in the app's MQTT connection panel, so
   its topic and credentials are not entered a second time.
   With an empty SSID,
   firmware skips station association entirely; it does not call
   `WiFi.begin()` or enter a reconnect loop.

Android stores complete connection profiles, including Wi-Fi and MQTT secrets,
in Keystore-backed encrypted storage. The browser build uses origin-scoped
`localStorage`, so use it only from a trusted browser profile. Applying a
profile only fills the editable device form; it does not transmit anything
until **Save and verify** is pressed. Firmware writes the validated candidate
and backup from fixed scratch storage, so a normal BLE save does not depend on
allocating another large contiguous heap block.

The setup AP has no password because factory-reset claim is already open by
design. After claim, all management API mutations require the owner key even
while the open configuration AP is active.

## Tracker completion

Configure the tracker first, but it remains in setup and does not enter tracking
mode until all of these are true:

- its transactional configuration validates and includes station Wi-Fi;
- the selected gateway has registered the tracker ID, name, and LoRa AEAD key;
- the tracker has recorded the gateway confirmation.

Use **Register and finish pairing**. The app commits the gateway registry first
and confirms the gateway on the still-connected tracker second. Both operations
are idempotent, so retrying after a timeout is safe. The pairing panel shows
four live stages: gateway discovery/authentication, gateway registration,
tracker confirmation, and completion. Transport attempts and failures remain
visible instead of being hidden in the diagnostic log.

Gateway registration tries these transports in order:

1. **MQTT**, when the app is connected to the gateway's broker. This is the
   normal remote path and works when the phone and tracker are away from the
   gateway's Wi-Fi. The registration fields are AES-256-GCM encrypted with the
   gateway owner key and bound to the gateway configuration revision.
2. **LAN**, using a manually entered current address, learned station addresses,
   then `lora-gateway-<gateway-id>.local`.
3. **Bluetooth**, when the saved gateway is nearby.

The gateway publishes its current station IP and configuration revision in its
retained MQTT status. The app uses that status to repair its saved device record.
`192.168.4.1` is only the temporary setup AP and is deliberately never retained
as a gateway LAN address. If MQTT and mDNS are unavailable, enter the current
LAN IP beside the gateway selector.

There is no unauthenticated LoRa enrollment fallback. Before registration, a
gateway intentionally cannot authenticate a new tracker and the tracker cannot
safely send its LoRa key over the air. Remote pairing therefore uses the
owner-key-encrypted MQTT command. A future LoRa-only flow would require an
explicit, expiring gateway invitation transferred to the app/tracker first;
proximity alone must not grant registry access.

A tracker may confirm more than one gateway. Changing either its canonical
device ID or LoRa AEAD key invalidates every existing gateway confirmation and
returns it to setup mode; register the new identity/key before pairing again.

Tracker button actions are page-specific:

- **Status:** reset the daily distance after the on-screen confirmation.
- **GPS:** acquire a fix; a normal hold requests at least 60 seconds and a
  six-second hold requests 180 seconds. Live visible/used satellite counts,
  HDOP, and remaining time are displayed before acceptance.
- **Radio:** transmit queued history and show the live ACK countdown.
- **Debug:** toggle bounded BLE debug logging.

After hard boot, release GPIO 0 and hold it for 1.5 seconds during the displayed
five-second window to enter configuration mode. Never hold GPIO 0 while applying
power or reset to an ESP32-S3, because that can select the ROM downloader.

## Gateway and repeater

The gateway's custom BLE management service remains discoverable so a saved
owner key can authenticate without an OS bond. Station Wi-Fi is preferred for
normal management. Holding USER for five seconds opens configuration mode and
the setup AP for ten minutes.

An incomplete tracker keeps its custom BLE configuration service available
without a timeout, including the interval after Wi-Fi has been saved but before
gateway registration is confirmed. Once onboarding is complete, its normal
bounded BLE/low-power policy resumes. Gateway BLE management remains available
after onboarding and always requires the stored owner key.

Repeaters do not need tracker LoRa keys and do not expose BLE. Claim and manage
them through their open, attended setup AP. They do not run a station Wi-Fi or
MQTT client in normal operation, so Wi-Fi/MQTT profiles do not apply. Repeating
remains disabled until a valid radio/forwarding configuration has been saved.

## Transfer access by QR

Open a saved device and choose **Show access QR**. On another phone choose
**Import access QR** and scan it or select its image. The versioned payload
contains the device ID, role, optional LAN address, and owner key. BLE IDs are
not exported because they are platform-local identifiers.

There is no server-side recovery and firmware never returns the owner key. If
every stored/exported copy is lost, physically factory-reset and claim the
device again. To revoke a transferred key, factory-reset and reclaim the device.

## Configuration mode and remote OTA

OTA is disabled during ordinary operation. Enable it for ten minutes using one
of these paths:

- **Enable config + OTA** in the app over authenticated BLE or Wi-Fi;
- open `http://<device-ip>/enable-config`, paste the owner key, and choose
  **Enable for 10 minutes**;
- `POST /api/v1/config-mode` with the owner key;
- `ENTER_CONFIG_MODE` in an authenticated BLE session;
- the documented boot/button action.

The dependency-free device page exchanges a valid Bearer key for a random,
memory-only, HttpOnly browser session cookie lasting at most ten minutes. Its
forms and live-log requests use that cookie; the owner key is not placed in a
URL or form field.

Set the owner key in the shell, then select the matching OTA environment and
device IP. The app exposes **Copy owner key for PlatformIO** inside the access
QR panel; the clipboard grants full device control, so clear it after use:

```powershell
$env:LORA_TRACKER_OWNER_KEY = "<64-hex-owner-key>"
pio run -d components/tracker-firmware -e heltec_wireless_tracker_ota `
  -t upload --upload-port 192.168.1.42
```

Gateway example:

```powershell
$env:LORA_TRACKER_OWNER_KEY = "<64-hex-owner-key>"
pio run -d components/gateway-firmware -e heltec_wifi_lora_32_v2_ota `
  -t upload --upload-port 192.168.1.103
```

Use the corresponding repeater OTA environment for repeaters. ArduinoOTA
performs its challenge/response using the owner key. OTA is authenticated but
images are not yet cryptographically signed; Secure Boot, flash encryption, and
signed update enforcement remain production-hardening tasks.

## Acceptance checklist

- Android Settings shows no bond/PIN flow for the device.
- The device can be reclaimed only after factory reset.
- A wrong/missing owner key cannot read configuration or mutate state.
- An empty SSID produces no station association attempts.
- Tracker, gateway, and repeaters have identical legal Germany radio settings.
- Tracker setup cannot finish before gateway registration and confirmation.
- QR import can manage the device from a second test installation.
- OTA is rejected outside configuration mode and succeeds inside it with the
  owner key.

See [the onboarding API](protocols/ONBOARDING_API.md) for protocol details and
[Germany radio compliance](RADIO_COMPLIANCE_DE.md) before deployment.
