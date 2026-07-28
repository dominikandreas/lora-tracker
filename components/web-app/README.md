# LoRa Tracker application

The shared LoRa Tracker user interface is shipped both as a static progressive
web app and as a Capacitor-based Android application. It uses MQTT 3.1.1 over
WebSocket and bundles Leaflet, PMTiles and the complete Network Lab locally.

## Current features

- MQTT WebSocket connection with username/password
- Automatic discovery of tracker point/state messages
- Strict point-schema 2 validation
- GNSS timestamp display with receive-time fallback
- MQTT history-schema v2 requests and chunk collection
- IndexedDB local point cache in browsers and bounded SQLite storage on Android
- Multi-tracker selection and status cards
- Leaflet route map with an offline grid, opt-in OpenStreetMap, or an imported
  raster PMTiles archive stored in OPFS
- No-bond BLE/Wi-Fi claim and complete tracker/gateway/repeater configuration,
  with automatic nearby-device discovery, 256-bit owner keys, local-IP
  reconnect, BLE fallback, and QR access transfer
- Coordinated gateway registration and tracker-side pairing confirmation; a
  tracker cannot leave setup until both Wi-Fi configuration and pairing succeed.
  Registration uses a dedicated idempotent gateway operation and does not tear
  down the active tracker session
- Open-app staleness, battery and unusual-movement notifications, using native
  notification channels on Android
- Installable/offline application shell through a service worker
- Automatic MQTT password storage: Android Keystore-backed storage in the
  native app and origin-scoped localStorage in the browser

## Run

```bash
npm ci
npm run build
npm run serve
```

Open `http://localhost:8080`. The broker must expose an MQTT WebSocket endpoint,
preferably `wss://` with a trusted certificate.
When the URL omits a port, the application supplies the project defaults:
`1884` for `ws://` and `8884` for `wss://`. An explicitly entered port always
wins because broker listener layouts are configurable.

The browser stores broker URL, base topic, username, MQTT password and device
owner keys in origin-scoped localStorage. Android stores non-secret preferences
using Capacitor Preferences and retains passwords and owner keys in platform
secure storage. Use only a trusted
`wss://` endpoint whenever possible. The Android app also permits `ws://` for
private brokers after an explicit warning. Unlike the encrypted LoRa hop,
plaintext MQTT exposes broker credentials, locations, history and commands to
the local network.

## Build Android

Install Android Studio/SDK, Java 21 and Node.js, then run:

```bash
npm ci
npm run native:build
```

The cross-platform command invokes `gradlew` or `gradlew.bat` as appropriate.
The APK is written to `android/app/build/outputs/apk/debug/app-debug.apk`.
`native:sync` always rebuilds the web assets before copying them into the native
project. On Android, open **Manage devices**, select a discovered factory-reset
device, let the app claim it without an OS bond or PIN, configure Wi-Fi, then
complete the role-specific settings. Saved gateways are maintained over their
local IP or custom BLE service. When pairing a tracker, the app fills the saved
gateway address automatically; enter its current LAN IP in the visible pairing
field if the network cannot resolve its `.local` hostname. The application ID is
`de.dominikandreas.loratracker`; its version is
derived from the repository-level `VERSION` file. Android CI performs the same
sync and Gradle build and uploads a debug APK artifact.

## Deliberate limitations

- Android is the only native target currently maintained; iOS and store
  publication are intentionally deferred
- Owner-key QR transfer grants full authority and has no server-side revocation;
  factory reset is required to rotate a leaked key
- The local management bearer protocol assumes physical proximity/a trusted
  LAN; authorization is not a replacement for link confidentiality
- Browser owner keys are retained in origin-scoped localStorage and are
  therefore less protected than Android's Keystore-backed owner keys
- The browser MQTT password has the same localStorage exposure; use a dedicated
  broker account with narrowly scoped topic permissions
- Browser BLE setup requires Chromium; Android uses the native BLE stack
- PMTiles support is raster-only
- Alerts are evaluated by the running application; reliable closed-app alerts
  still require a server-backed push service

## Notifications

Staleness, battery hysteresis and unusual-movement checks run while the
application process is alive. Android posts them through a native notification
channel; the PWA uses the browser Notification API.

True closed-app delivery requires a server-side push/heartbeat service. It is
not implemented in this offline-first release.
