# LoRa Tracker simulation coverage

Run the deterministic suite from `components/archiver`:

```text
python -m lora_tracker_archiver.simulator --trackers 2 --points 12 --service-suite --embedded-suite
```

It is intentionally brokerless: the in-memory MQTT client exercises the real
archiver service callbacks without requiring credentials or a network service.
The selected SQLite output path is reset on each run so repeated simulations
remain deterministic; use a dedicated simulation database path.

`--embedded-suite` is a host C++17 contract test. It compiles the shared headers
from tracker, gateway and repeater firmware directories, but it does **not**
build the `.ino` sketches.
Use the pinned PlatformIO environments in `components/tracker-firmware` and
`components/gateway-firmware` and `components/repeater-firmware` for ESP32 compilation.

| Boundary / behaviour | Covered by simulator |
|---|---|
| Tracker telemetry contract | Transport-v2 multi-tracker routes, stable IDs, sequence/boot IDs, battery, RSSI, valid GNSS time and explicit current-schema untimed points |
| Embedded tracker and gateway shared code | Native C++17 compilation and execution of each firmware copy of the protocol/configuration headers: frame layout, device hashes, ULEB128 boundaries/malformed data, CRC validation and tracker registry rules |
| Repeater link policy | All three header copies: link parsing, hop advancement/caps, deterministic priority delay, peer suppression, ACK/history cache lifetimes, LoRa time-on-air calculation and repeater configuration validation |
| Gateway to MQTT contract | Two independent gateway receptions, gateway metadata, duplicate delivery, archiver deduplication and QoS-1 archive confirmations after SQLite commit |
| Topic and payload validation | Configured-base-anchored topics including nested `v1`, device-hash consistency and production JSON/protocol validation |
| Archiver storage | SQLite insertion, per-gateway reception aggregation, timestamp fallback, history filtering and chunking |
| Archiver MQTT service | Last will, credentials/TLS setup, subscriptions, retained availability/status, allowlist rejection, successful and invalid history requests |
| History API | Chunked responses, `final`, `has_more`, cursor output and structured error response |
| Browser point contract | Strict schema-2 identity/range/timestamp validation, GNSS time preference, receive-time fallback and malicious-field rejection |
| MQTT Web codec | Packet encoding/decoding tests in `components/web-app/tests` |

## Required physical/infrastructure validation

No software-only simulator can truthfully cover these behaviours. They require a
pinned ESP32 build and, where applicable, real devices and a broker:

- tracker and gateway firmware compilation, flash layout, NVS/RTC recovery,
  BLE/Wi-Fi onboarding, GPIO button handling, GNSS acquisition, motion/sleep
  policy and battery/power measurements;
- LoRa RF range, interference, capture/hidden-node effects, packet loss, ACK
  collisions, real multi-hop timing, retry backoff, repeater queue/airtime
  behaviour, calibrated verification of the Germany rolling-hour airtime and
  installed ERP limits, AES-GCM adversarial/fuzz vectors and the complete binary
  history-frame decoder behaviour;
- a real MQTT broker's TLS certificates, authentication, ACLs, retained-message
  persistence, reconnect behaviour and QoS delivery guarantees;
- browser rendering/XSS checks, automatic multi-page history, IndexedDB
  quota/upgrade/pruning behaviour, service-worker updates and WebSocket reconnects
  in supported browsers.

Those checks should be automated in a hardware-in-the-loop and broker/browser
test environment before field deployment. The simulator makes all deterministic
cross-component contracts reproducible, but does not claim to replace those
tests.
