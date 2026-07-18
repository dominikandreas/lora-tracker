# LoRa Tracker architecture

## System roles

```text
+------------------+   encrypted LoRa   +------------------+   encrypted LoRa   +------------------+
| Battery tracker  | <----------------> | Keyless repeater | <----------------> | Wi-Fi gateway    |
| GNSS + RTC queue |   HISTORY / ACK     | bounded flooding |   HISTORY / ACK     | multi-tracker RX |
+------------------+                    +------------------+                    +--------+---------+
                                                            |
                                                      MQTT over TLS*
                                                            |
                    +-------------------+-------------------+------------------+
                    |                   |                                      |
             +------+-------+    +------+--------+                      +------+------+
             | Archiver     |    | Web app      |                      | Home/other  |
             | SQLite       |    | MQTT WS/PWA  |                      | consumers   |
             +--------------+    +---------------+                      +-------------+

* LoRa payloads are end-to-end AES-256-GCM encrypted between tracker and
  gateway. MQTT uses independently configured TLS.
```

Repeaters are optional. Direct tracker/gateway traffic uses the same link header
with hop count zero and may set the hop limit to zero.

## Tracker

The tracker wakes on a timer or user action, acquires a GNSS fix under an
adaptive timeout policy, classifies motion, stores route points in RTC memory,
and transmits batches when either enough points or enough time has accumulated.
Missing ACKs retain the queue and trigger exponential retry backoff.

The tracker keeps its radio open for the configured ACK window and ignores its
own relayed history or unrelated traffic while waiting for a matching ACK. The
whole ACK packet must complete before the deadline; timeout closes the receive
window and puts the radio to sleep.

History v2 combines:

- absolute root latitude/longitude;
- coordinate anchors and signed byte deltas;
- one absolute 32-bit GNSS Unix timestamp;
- unsigned LEB128 second deltas for later points.

The tracker configuration is a CRC-protected, revisioned NVS blob with an
independent backup. BLE and Wi-Fi onboarding use the same transactional patch
model.

## Gateway

The gateway accepts only the current versioned LoRa history packet. Frames are
routed by a public 64-bit device hash. It maintains independent
per-tracker deduplication state and publishes point events plus retained latest
state. It advances deduplication and sends a radio ACK only after the archiver
returns a durable SQLite receipt for every new point in the batch. If the
received link header still permits a relay hop, it also waits for the shared
relay-clear guard before transmitting the ACK so a fast archive cannot collide
with a repeater forwarding the HISTORY frame.

The gateway can register up to 12 trackers. Unknown identities and unsupported
schemas are rejected.

## Repeater

The repeater wraps no new application data and holds no tracker keys. It advances a 28-byte mutable link-v2 header while preserving the authenticated
secure frame byte-for-byte. HISTORY records the selected route; ACK follows it
in reverse. Packet-airtime-sized priority slots, peer suppression, atomic
HISTORY+ACK airtime reservations, a bounded queue and the Germany rolling-hour
limiter constrain traffic in both directions. See [repeaters](REPEATERS.md).

## MQTT and archiver

Canonical topics are rooted at `lora-tracker/v1`. Point events are non-retained;
latest state and availability are retained. The stable point identifier is:

```text
<device_hash>:<boot_id>:<sequence>
```

The archiver deduplicates on that ID, records receptions from multiple gateways,
and stores GNSS fix time separately from broker receive time. After commit it
publishes a gateway-specific archive receipt at QoS 1. History responses are
paginated and chunked over MQTT.

## Portable firmware core and Network Lab

Hardware-independent policy lives in `components/firmware-core`: Germany radio
validation, LoRa airtime/sensitivity, tracker sleep/retry/batching, deterministic
relay timing and the relay/ACK collision guard. PlatformIO links that C++17
library into all transmitting firmware roles. Emscripten compiles the exact
source to a small standalone WASM module.

The Network Lab loads that module in a Web Worker. A seeded discrete-event
engine supplies virtual time and hardware adapters for movement/GNSS, radio
propagation and shared-channel collisions, power, repeaters, receiver and one
in-memory MQTT/archive service. The canvas and inspector are presentation only;
versioned scenario JSON can reproduce an engine run without the UI.

This boundary is intentional: Arduino/ESP32, RadioLib, Wi-Fi, GNSS, NVS and sleep
APIs are not emulated in a browser. Their decisions move into the portable core
when practical, while hardware effects remain explicit simulator adapters or
physical tests.

## Web application

The web app is a static PWA. It implements MQTT 3.1.1 over WebSocket directly,
stores non-secret broker settings in localStorage, keeps bounded point history
in IndexedDB, and restores cached tracker state after an offline reload. Leaflet
renders an offline grid, an explicitly selected OpenStreetMap layer, or a local
raster PMTiles archive retained in OPFS. MQTT passwords remain session-only.
An attended Web Bluetooth client supports authenticated tracker claiming,
configuration, rollback, reboot and factory reset. Gateway configuration stays
on the gateway's own captive portal because a Pages origin cannot safely reach
an arbitrary local HTTP gateway.

## Identity and trust

Three concepts must remain separate:

1. `device_id`: human-managed canonical identifier such as `wera`.
2. `device_hash`: current FNV-1a routing identifier; public and non-secret.
3. `lora_aead_key`: random per-tracker 256-bit secret shared only with authorized
   gateways; repeaters never receive it.

The routing hash must never be treated as a password or encryption key.

## Persistence

- Tracker: unacknowledged history/state in RTC plus selected NVS checkpoints and configuration. Power-loss-safe history journalling remains a release blocker.
- Gateway: NVS configuration and per-tracker deduplication cursors.
- Repeater: CRC-protected NVS forwarding/radio configuration and admin credential.
- Archiver: SQLite point and reception tables.
- Web app: IndexedDB point cache and localStorage connection preferences.

## Versioning

Transport, message schema, configuration, onboarding API and MQTT JSON schema
are versioned independently. This release intentionally supports only its
current schema set; incompatible devices must be upgraded before deployment.
