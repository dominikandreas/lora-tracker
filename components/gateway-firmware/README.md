# LoRa Tracker gateway firmware

The gateway accepts only AES-256-GCM-authenticated transport-v2 history frames
from registry entries with a matching per-tracker key. It
reconstructs GNSS timestamps from one absolute root epoch and ULEB128
per-point deltas; unsupported schemas are rejected.

## New behavior

- Publishes point JSON schema v2 with `fix_time_unix_ms`, `timestamp_valid` and
  `time_source`.
- Rejects packets from unregistered trackers and unsupported schemas.
- Rejects malformed, truncated, overflowing or implausible timestamp chains.
- Publishes each decoded point to the non-retained event topic and retained
  latest-state topic.
- Keeps stable `point_id` values for cross-gateway deduplication.
- Retains the Step-5 gateway status and command API.

PlatformIO compiles the gateway sketch with the pinned ESP32, LoRa,
Preferences, PubSubClient and WebServer dependencies.

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

Tagged releases contain merged browser-flashable images and provenance; see
[`docs/FLASHING.md`](../../docs/FLASHING.md).

The default MQTT transport verifies the broker with a PEM root CA provisioned
through the authenticated configuration API. `secrets.h` may provide a factory
seed, but generic release images leave it empty. Plain MQTT requires the
explicit `allow_insecure_mqtt` test-only build override. Unauthenticated telnet
logging is disabled by default.
