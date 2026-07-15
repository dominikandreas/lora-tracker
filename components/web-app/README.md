# LoRa Tracker web app

A dependency-free progressive web app for the LoRa Tracker MQTT API. It runs as static
files and uses MQTT 3.1.1 over WebSocket directly in the browser.

## Current features

- MQTT WebSocket connection with username/password
- Automatic discovery of tracker point/state messages
- Strict point-schema 2 validation
- GNSS timestamp display with receive-time fallback
- MQTT history-schema v2 requests and chunk collection
- IndexedDB local point cache
- Multi-tracker selection and status cards
- Lightweight SVG route view with no external map or tile dependency
- Installable/offline application shell through a service worker

## Run

```bash
python3 -m http.server 8080
```

Open `http://localhost:8080`. The broker must expose an MQTT WebSocket endpoint,
preferably `wss://` with a trusted certificate.

The app stores broker URL, base topic and username in localStorage. It never
persists the MQTT password. MQTT remains an untrusted transport until the later
end-to-end encryption step is implemented.

## Deliberate limitations of this first app

- No BLE or Wi-Fi onboarding UI yet
- No QR provisioning or app-to-app transfer yet
- No end-to-end encryption yet
- No geographical base map; the route is plotted in local coordinates
- History pagination beyond the first 500 points is not automated yet
