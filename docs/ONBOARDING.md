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
6. In **Reusable connection profiles**, create or select a Wi-Fi profile and,
   for a gateway, an MQTT profile. Choose **Use for this device** for each one,
   then configure the remaining role-specific settings and press **Save and
   verify**. Profiles are app-owned and can be reused for every later device.
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
are idempotent, so retrying after a timeout is safe. If mDNS is unavailable,
enter the gateway's current LAN IP beside the gateway selector.

A tracker may confirm more than one gateway. Changing either its canonical
device ID or LoRa AEAD key invalidates every existing gateway confirmation and
returns it to setup mode; register the new identity/key before pairing again.

Tracker button actions are page-specific:

- **Status:** reset the daily distance after the on-screen confirmation.
- **GPS:** acquire a fix; a longer hold requests a longer listen window. Live
  satellite count, HDOP, and remaining time are displayed before acceptance.
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

Repeaters do not need tracker LoRa keys and do not expose BLE. Claim and manage
them through their open, attended setup AP. Repeating remains disabled until a
valid radio/forwarding configuration has been saved.

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
