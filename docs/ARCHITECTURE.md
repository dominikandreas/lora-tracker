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
own relayed history or unrelated traffic while waiting for a matching ACK.

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
returns a durable SQLite receipt for every new point in the batch.

The gateway can register up to 12 trackers. Unknown identities and unsupported
schemas are rejected.

## Repeater

The repeater wraps no new application data and holds no tracker keys. It
increments a six-byte mutable link header while preserving the authenticated
secure frame byte-for-byte. Hop limits, deterministic priority jitter, duplicate
suppression, a bounded queue and a Germany rolling-hour airtime limiter constrain flooding in
both directions. See [repeaters](REPEATERS.md).

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

## Web application

The web app is a static PWA. It implements MQTT 3.1.1 over WebSocket directly,
stores non-secret broker settings in localStorage, keeps points in IndexedDB,
and displays a local-coordinate SVG route. It currently has no BLE onboarding
UI and no geographical tile layer.

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
