# LoRa Tracker gateway firmware

This gateway accepts legacy, history-schema-v1 and history-schema-v2 LoRa
frames. Schema v2 reconstructs GNSS timestamps from one absolute root epoch and
ULEB128 per-point deltas.

## New behavior

- Publishes point JSON schema v2 with `fix_time_unix_ms`, `timestamp_valid` and
  `time_source`.
- Retains rolling compatibility with old trackers.
- Rejects malformed, truncated, overflowing or implausible timestamp chains.
- Publishes each decoded point to the non-retained event topic and retained
  latest-state topic.
- Keeps stable `point_id` values for cross-gateway deduplication.
- Retains the Step-5 gateway status and command API.

Compile `equine_gateway_mqtt_v6.ino` with the same ESP32, LoRa, Preferences,
PubSubClient and WebServer libraries used by the previous gateway.

## PlatformIO

`platformio.ini` pins the Heltec WiFi LoRa 32 V2 / SX1276 build and its Arduino
core, LoRa, and PubSubClient dependencies. Copy `secrets.example.h` to
`secrets.h`, then build from the repository root:

```bash
pio run -d components/gateway-firmware -e heltec_wifi_lora_32_v2
```

Append `-t upload` only after choosing the correct connected board. The host
simulator validates shared binary/configuration contracts, not gateway Wi-Fi,
MQTT, NVS, or radio hardware; see
[`docs/SIMULATION_COVERAGE.md`](../../docs/SIMULATION_COVERAGE.md).
