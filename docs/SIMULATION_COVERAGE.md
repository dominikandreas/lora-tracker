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
from both firmware directories, but it does **not** build either `.ino` sketch.
Use the pinned PlatformIO environments in `components/tracker-firmware` and
`components/gateway-firmware` for ESP32 compilation.

| Boundary / behaviour | Covered by simulator |
|---|---|
| Tracker telemetry contract | Transport-v2 multi-tracker routes, stable IDs, sequence/boot IDs, battery, RSSI, valid GNSS time and explicit current-schema untimed points |
| Embedded tracker and gateway shared code | Native C++17 compilation and execution of each firmware copy of the protocol/configuration headers: frame layout, device hashes, ULEB128 boundaries/malformed data, CRC validation and tracker registry rules |
| Gateway to MQTT contract | Two independent gateway receptions, gateway metadata, duplicate delivery and archiver deduplication |
| Topic and payload validation | Canonical point topics, device-hash consistency, production JSON/protocol validation |
| Archiver storage | SQLite insertion, per-gateway reception aggregation, timestamp fallback, history filtering and chunking |
| Archiver MQTT service | Last will, credentials/TLS setup, subscriptions, retained availability/status, allowlist rejection, successful and invalid history requests |
| History API | Chunked responses, `final`, `has_more`, cursor output and structured error response |
| Browser point contract | Strict schema-2 normalization, GNSS time preference, receive-time fallback and incompatible-schema rejection |
| MQTT Web codec | Packet encoding/decoding tests in `components/web-app/tests` |

## Required physical/infrastructure validation

No software-only simulator can truthfully cover these behaviours. They require a
pinned ESP32 build and, where applicable, real devices and a broker:

- tracker and gateway firmware compilation, flash layout, NVS/RTC recovery,
  BLE/Wi-Fi onboarding, GPIO button handling, GNSS acquisition, motion/sleep
  policy and battery/power measurements;
- LoRa RF range, interference, packet loss, ACK collisions, timing, retry
  backoff, queue persistence, AES-GCM adversarial/fuzz vectors and the complete
  binary history-frame decoder behaviour;
- a real MQTT broker's TLS certificates, authentication, ACLs, retained-message
  persistence, reconnect behaviour and QoS delivery guarantees;
- browser rendering, IndexedDB quota/upgrade behaviour, service-worker caching
  and WebSocket behaviour in supported browsers.

Those checks should be automated in a hardware-in-the-loop and broker/browser
test environment before field deployment. The simulator makes all deterministic
cross-component contracts reproducible, but does not claim to replace those
tests.
