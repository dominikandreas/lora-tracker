# LoRa Tracker web app

A static progressive web app for the LoRa Tracker MQTT API. It uses MQTT 3.1.1
over WebSocket directly in the browser and bundles Leaflet and PMTiles locally.

## Current features

- MQTT WebSocket connection with username/password
- Automatic discovery of tracker point/state messages
- Strict point-schema 2 validation
- GNSS timestamp display with receive-time fallback
- MQTT history-schema v2 requests and chunk collection
- IndexedDB local point cache
- Multi-tracker selection and status cards
- Leaflet route map with an offline grid, opt-in OpenStreetMap, or an imported
  raster PMTiles archive stored in OPFS
- Authenticated Web Bluetooth tracker configuration
- Open-app staleness, battery and unusual-movement notifications
- Installable/offline application shell through a service worker

## Run

```bash
npm ci
npm run build
python3 -m http.server 8080
```

Open `http://localhost:8080`. The broker must expose an MQTT WebSocket endpoint,
preferably `wss://` with a trusted certificate.

The app stores broker URL, base topic and username in localStorage. It never
persists the MQTT password. MQTT remains an untrusted transport until the later
end-to-end encryption step is implemented.

## Deliberate limitations

- No QR provisioning or app-to-app transfer yet
- No end-to-end encryption yet
- MQTT passwords are session-only; reconnecting after a reload requires entry
- BLE tracker setup requires Chromium on a platform with Web Bluetooth
- PMTiles support is raster-only

## Notifications

The alerts implemented in this PWA (staleness, battery hysteresis, unusual movement) run via a \setInterval\ loop while the application is open. These are **open-PWA notifications**. They require the browser tab to be active or running in the background.

True **closed-app push notifications** require server-side integration with Web Push (VAPID) and a Service Worker push event listener. This is not currently implemented in this offline-first release.
