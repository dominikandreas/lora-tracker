# LoRa Tracker architecture

## System roles

```text
+------------------+       LoRa history/ACK       +------------------+
| Battery tracker  | <--------------------------> | Wi-Fi gateway     |
| GNSS + RTC queue |                               | multi-tracker RX  |
+------------------+                               +--------+---------+
                                                            |
                                                      MQTT over TLS*
                                                            |
                    +-------------------+-------------------+------------------+
                    |                   |                                      |
             +------+-------+    +------+--------+                      +------+------+
             | Archiver     |    | Web app      |                      | Home/other  |
             | SQLite       |    | MQTT WS/PWA  |                      | consumers   |
             +--------------+    +---------------+                      +-------------+

* TLS is recommended but end-to-end payload encryption is not implemented yet.
```

Future relay nodes may forward encrypted LoRa frames without knowing tracker
keys. They are not implemented in this release.

## Tracker

The tracker wakes on a timer or user action, acquires a GNSS fix under an
adaptive timeout policy, classifies motion, stores route points in RTC memory,
and transmits batches when either enough points or enough time has accumulated.
Missing ACKs retain the queue and trigger exponential retry backoff.

History v2 combines:

- absolute root latitude/longitude;
- coordinate anchors and signed byte deltas;
- one absolute 32-bit GNSS Unix timestamp;
- unsigned LEB128 second deltas for later points.

The tracker configuration is a CRC-protected, revisioned NVS blob with an
independent backup. BLE and Wi-Fi onboarding use the same transactional patch
model.

## Gateway

The gateway accepts legacy, history-v1 and history-v2 LoRa packets. Versioned
frames are routed by a public 64-bit device hash. It maintains independent
per-tracker deduplication state, publishes point events and retained latest
state, and ACKs only after successful packet processing.

The gateway can register up to 12 trackers in configuration v1. One tracker may
be designated as the recipient of identity-less legacy packets.

## MQTT and archiver

Canonical topics are rooted at `equine/v1`. Point events are non-retained;
latest state and availability are retained. The stable point identifier is:

```text
<device_hash>:<boot_id>:<sequence>
```

The archiver deduplicates on that ID, records receptions from multiple gateways,
and stores GNSS fix time separately from broker receive time. History responses
are paginated and chunked over MQTT.

## Web application

The web app is a static PWA. It implements MQTT 3.1.1 over WebSocket directly,
stores non-secret broker settings in localStorage, keeps points in IndexedDB,
and displays a local-coordinate SVG route. It currently has no BLE onboarding
UI and no geographical tile layer.

## Identity and trust

Three concepts must remain separate:

1. `device_id`: human-managed canonical identifier such as `wera`.
2. `device_hash`: current FNV-1a routing identifier; public and non-secret.
3. Future device secret/key: random cryptographic material used for AEAD and
   authentication. It does not exist yet.

The routing hash must never be treated as a password or encryption key.

## Persistence

- Tracker: RTC history/state plus selected NVS checkpoints and configuration.
- Gateway: NVS configuration and per-tracker deduplication cursors.
- Archiver: SQLite point and reception tables.
- Web app: IndexedDB point cache and localStorage connection preferences.

## Versioning

Transport, message schema, configuration, onboarding API and MQTT JSON schema
are versioned independently. This lets consumers support rolling upgrades
without changing all layers simultaneously.
