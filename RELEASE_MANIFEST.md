# Release manifest

Release: `1.0.0-prototype`  
Date: 2026-07-15

## Current components

| Path | Version | Entry point |
|---|---|---|
| `components/tracker-firmware` | timestamp v4 | `equine_tracker_timestamp_v4.ino` |
| `components/gateway-firmware` | MQTT v6 | `equine_gateway_mqtt_v6.ino` |
| `components/archiver` | v2 | `python -m equine_archiver` |
| `components/web-app` | v1 | `index.html` |

## Canonical specifications

- LoRa transport/history: `docs/protocols/EQUINE_PROTOCOL_V1.md` and
  `EQUINE_PROTOCOL_V2.md`
- Persistent configuration: `EQUINE_CONFIG_V1.md`
- Onboarding/configuration API: `EQUINE_ONBOARDING_API_V1.md`
- MQTT topic API with point/history schemas: both schema-v1 and schema-v2
  documents are retained for rolling-upgrade consumers.

## Exclusions

- No real `secrets.h` or `.env` files
- No SQLite database
- No Python bytecode or pytest cache
- No compiled firmware binaries
- No selected software license
