# LoRa Tracker build and deployment

## 1. Tracker firmware

Project: `components/tracker-firmware/`

### Supported source branches

- `BOARD_WIRELESS_TRACKER`: Heltec Wireless Tracker / ESP32-S3, UC6580 GNSS,
  SX1262 through RadioLib, TFT through TFT_eSPI.
- Default branch: older Heltec WiFi LoRa 32 V2 style hardware, BN-220 GNSS,
  SX1276 through the LoRa library, OLED through U8g2.

### Required Arduino libraries

- TinyGPS++
- Preferences, WiFi, WebServer, ArduinoOTA and ESP32 BLE from the ESP32 core
- RadioLib and TFT_eSPI for `BOARD_WIRELESS_TRACKER`
- LoRa and U8g2 for the legacy branch

Copy `secrets.example.h` to `secrets.h`. Values are only migration/factory
seeds; onboarding writes the active configuration to NVS.

The included `platformio.ini` pins PlatformIO's Espressif32 6.7.0 platform and
the library versions inherited from the v1 projects. Install PlatformIO Core,
then build one of the explicit targets:

```bash
pio run -d components/tracker-firmware -e heltec_wifi_lora_32_v2
pio run -d components/tracker-firmware -e heltec_wireless_tracker
```

The Wireless Tracker target defines `BOARD_WIRELESS_TRACKER` and its TFT_eSPI
display settings. Append `-t upload` only after selecting the correct physical
board/port.

### First boot and onboarding

An unprovisioned tracker starts:

```text
Wi-Fi AP: EquineTracker-<device_id>
BLE name: EqTrk-<device_id>
Prototype AP password: EquineSetup!
```

The password can be overridden at build time with
`EQUINE_ONBOARDING_AP_PASSWORD`. Configuration can be changed transactionally
through the HTTP or BLE API described in `docs/protocols/EQUINE_ONBOARDING_API_V1.md`.

## 2. Gateway firmware

Project: `components/gateway-firmware/`

The current pin mapping targets the original Heltec V2/SX1276 receiver:

```text
SCK=5, MISO=19, MOSI=27, SS=18, RST=14, DIO0=26
```

Required libraries:

- PubSubClient
- LoRa
- Preferences, WiFi, WebServer and ArduinoOTA from the ESP32 core

Copy `secrets.example.h` to `secrets.h`, set migration defaults, and build the
pinned target:

```bash
pio run -d components/gateway-firmware -e heltec_wifi_lora_32_v2
```

Append `-t upload` only after selecting the correct physical board/port.

An unprovisioned gateway starts `EquineGateway-<gateway_id>`. On provisioned
gateways, writes require holding the USER button for five seconds, opening a
ten-minute administration window.

## 3. Archiver

Project: `components/archiver/`

### Local Python

```bash
cd components/archiver
python -m venv .venv
. .venv/bin/activate
pip install .
cp .env.example .env
set -a; . ./.env; set +a
python -m equine_archiver
```

### Docker

```bash
cd components/archiver
cp .env.example .env
docker compose -f docker-compose.example.yml up --build -d
```

Persist `/data/equine-history.sqlite3` and back it up like any other SQLite
database. The default retention window is ten days.

## 4. Web app

Project: `components/web-app/`

```bash
cd components/web-app
python3 -m http.server 8080
```

Open `http://localhost:8080`. The MQTT broker must expose a WebSocket listener,
preferably `wss://` with a trusted certificate.

The app remembers the broker URL, base topic and username but not the password.

## 5. Broker

Use a broker with:

- TLS for TCP and WebSocket listeners;
- authentication;
- topic ACLs;
- retained-message limits and sensible session limits;
- persistence if latest-state continuity matters.

Do not use an anonymous public broker for real location data. MQTT payloads are
currently plaintext to the broker.

## 6. Safe deployment order

1. Back up the gateway configuration and archiver database.
2. Deploy archiver v2; it accepts old and new point schemas.
3. Flash gateway v6; it accepts legacy, history-v1 and history-v2 frames.
4. Confirm old trackers still receive ACKs and points reach MQTT.
5. Flash tracker timestamp v4.
6. Confirm `timestamp_valid=true` and plausible `fix_time_unix_ms` values.
7. Deploy the web app and request history.

## 7. Tests

Archiver:

```bash
cd components/archiver
python -m pytest
```

Web app MQTT codec:

```bash
cd components/web-app
npm test
```

Firmware targets are pinned in the two `platformio.ini` files. Open
`equine-tracker-v2.code-workspace` in VS Code to work with both PlatformIO
projects together. Board builds still require the physical-board validation
described in the simulation coverage document.

The brokerless simulator can also compile and run the shared firmware protocol
and configuration contracts on a WSL host with a C++17 compiler:

```bash
python -m equine_archiver.simulator --embedded-suite
```

This is not an ESP32 board build; it does not cover peripherals or RF behavior.
