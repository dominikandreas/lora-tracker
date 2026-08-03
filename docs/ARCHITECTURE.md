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
             | Archiver     |    | User app     |                      | Home/other  |
             | SQLite       |    | Web/Android  |                      | consumers   |
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
During acquisition it continuously drains a 4 KiB UART buffer. Full/cold
attempts run for at least 90 seconds, intermediate retries for at least 30
seconds, and recovery preserves receiver state whenever UART bytes are present.
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
state. It advances deduplication and sends a radio ACK after the authenticated
frame has been handed to MQTT; the SQLite archiver is an optional consumer and
does not participate in radio delivery. If the received link header still
permits a relay hop, it waits for the shared relay-clear guard before
transmitting the ACK so it cannot collide with a repeater forwarding the
HISTORY frame.

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
and stores GNSS fix time separately from broker receive time. It may publish a
gateway-specific QoS 1 archive receipt after commit; this optional receipt does
not affect radio ACKs. History responses are paginated and chunked over MQTT.

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

## Web and Android application

One web codebase is built as both a static PWA and a Capacitor Android app. It
implements MQTT 3.1.1 over WebSocket directly and restores cached tracker state
after an offline reload. Browsers store settings, MQTT credentials and device
owner keys in origin-scoped localStorage and bounded point history in IndexedDB. Android
uses Preferences for non-secrets, SQLite for bounded telemetry and opt-in
Keystore-backed secure storage for broker credentials and per-device owner
keys. Android owner keys use Keystore; the PWA uses persistent,
origin-scoped localStorage and therefore has a weaker local security boundary. Leaflet
renders an offline grid, an explicitly selected OpenStreetMap layer, or a local
raster PMTiles archive retained in OPFS.

The device manager supports no-bond custom BLE or setup-AP claiming and complete
configuration for trackers, gateways, and repeaters. It inventories saved
devices, tries authenticated local HTTP first, and falls back to GATT for BLE-
capable roles. Android
uses native HTTP to avoid browser CORS/mixed-content restrictions. Tracker
activation is a two-device transaction: register the tracker key on a gateway,
then persist that gateway confirmation on the tracker. Native notifications
replace the browser Notification API while a small platform adapter keeps the
application logic shared.

The gateway registration API is a narrow idempotent upsert, and the app keeps
the tracker transport alive between gateway registration and tracker-side
confirmation. For a tracker/gateway pair, the app first sends an
owner-key-encrypted MQTT registration command when both devices share the
configured broker; it then falls back to station HTTP/mDNS and nearby GATT.
Retained gateway status reports the live station address and configuration
revision, so the temporary setup-AP address is never treated as a durable
endpoint. Firmware GATT callbacks only frame incoming bytes. Complete commands
are copied and processed by the Arduino application task; JSON construction,
NVS writes and indication transmission must never run on the small ESP-IDF
`BTC_TASK` stack.

The Android WebView permits cleartext `ws://` solely to support private brokers
without a trusted certificate and displays an explicit warning before each such
connection. This is a transport-security compromise, not a consequence-free
use of LoRa end-to-end encryption: the gateway terminates LoRa encryption and
publishes decoded telemetry to MQTT, so plaintext MQTT exposes credentials,
locations, history and commands on the IP network. The hosted HTTPS PWA remains
subject to browser mixed-content blocking and therefore requires `wss://`.

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
- Repeater: CRC-protected NVS forwarding/radio configuration and owner key.
- Archiver: SQLite point and reception tables.
- Browser app: IndexedDB point cache and localStorage connection preferences,
  MQTT credentials and owner keys.
- Android app: SQLite point cache, Preferences and opt-in secure credential storage.

## Versioning

Transport, message schema, configuration, onboarding API and MQTT JSON schema
are versioned independently. This release intentionally supports only its
current schema set; incompatible devices must be upgraded before deployment.
