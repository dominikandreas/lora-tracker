# LoRa Tracker MQTT API v1

This API separates routing, latest state, event history, gateway management and
history retrieval. MQTT is an untrusted transport; payload encryption and
authentication are added in a later protocol step.

## Common rules

- Base topic defaults to `equine` and is configurable on the gateway/archiver.
- API version is encoded in the topic as `v1` and in JSON as `api_version: 1`.
- Device and gateway hashes are 16 lowercase hexadecimal characters.
- `request_id` is 1–48 characters from `A-Z a-z 0-9 _ . -`.
- JSON schemas are independently versioned.
- Secrets never appear in topics or telemetry JSON.

## Tracker telemetry topics

### Point event

```text
equine/v1/trackers/<device_hash>/events/point
```

Non-retained. One message per newly decoded point. `point_id` is globally stable
for the tracker boot/sequence and lets consumers deduplicate packets received by
several gateways.

```json
{
  "api_version": 1,
  "point_schema_version": 1,
  "transport_version": 1,
  "schema_version": 1,
  "device_id": "wera",
  "device_name": "Wera",
  "device_hash": "3db3edf61a18fac0",
  "gateway_id": "home",
  "gateway_hash": "0123456789abcdef",
  "point_id": "3db3edf61a18fac0:17:42",
  "latitude": 50.228470,
  "longitude": 8.564520,
  "dist_m": 1230,
  "battery_level": 79,
  "rssi": -104,
  "seq": 42,
  "boot_id": 17,
  "gateway_uptime_ms": 812345
}
```

The current LoRa history schema has no timestamp for each point. The archiver
therefore stores server receive time as `received_at_ms`. A future LoRa schema
should add GNSS epoch time or a packet-relative time delta.

The ESP32 gateway currently uses PubSubClient, whose publishes are MQTT QoS 0.
A successful `publish()` means the payload was accepted by the local MQTT client
connection, not that a broker or archiver durably stored it. Stable `point_id`
prevents duplicate storage, but broker-confirmed QoS 1 requires a later MQTT
client/library upgrade or an application-level receipt.

### Latest state

```text
equine/v1/trackers/<device_hash>/state
```

Retained. Contains the same point JSON as the event topic and is intended for
apps, dashboards and Home Assistant that only need the most recent point.

### Availability

```text
equine/v1/trackers/<device_hash>/availability
```

Retained string `online` or `offline`. In v1 this represents gateway
availability, not proof that the battery tracker itself is currently reachable.
Use point age for tracker freshness.

## Gateway management

### Availability and status

```text
equine/v1/gateways/<gateway_hash>/availability
equine/v1/gateways/<gateway_hash>/status
```

Availability is retained `online`/`offline`. Status is retained JSON and is
published at connection and periodically.

### Command request

```text
equine/v1/gateways/<gateway_hash>/commands/request
```

```json
{
  "api_version": 1,
  "schema_version": 1,
  "request_id": "app-20260715-1",
  "command": "status.get"
}
```

Supported v1 commands:

- `ping`
- `status.get`
- `registry.get`

Responses are non-retained:

```text
equine/v1/gateways/<gateway_hash>/commands/response/<request_id>
```

`registry.get` emits one chunk per registered tracker. Each response includes
`chunk_index` and `final`.

This is gateway downlink only. Tracker configuration over MQTT is deliberately
not implemented before authenticated encryption and a versioned LoRa command
schema exist.

## History archiver

The archiver subscribes to:

```text
equine/v1/trackers/+/events/point
equine/v1/trackers/+/history/request
```

It stores unique points in SQLite and separately records receptions from
multiple gateways. Default retention is ten days.

### History request

```text
equine/v1/trackers/<device_hash>/history/request
```

```json
{
  "api_version": 1,
  "schema_version": 1,
  "request_id": "phone-42",
  "from_unix_ms": 1784000000000,
  "to_unix_ms": 1784100000000,
  "limit": 250,
  "cursor": 0
}
```

Fields other than version and `request_id` are optional. `cursor` is an opaque,
archiver-local pagination cursor and must only be reused with the same archiver.

### History response

```text
equine/v1/trackers/<device_hash>/history/response/<request_id>
```

Responses are chunked to stay below practical broker/client payload limits:

```json
{
  "api_version": 1,
  "schema_version": 1,
  "request_id": "phone-42",
  "device_hash": "3db3edf61a18fac0",
  "ok": true,
  "chunk_index": 0,
  "final": true,
  "has_more": false,
  "next_cursor": 123,
  "points": [
    {
      "point_id": "3db3edf61a18fac0:17:42",
      "received_at_ms": 1784050000000,
      "reception_gateway_count": 2,
      "best_rssi": -91,
      "latitude": 50.228470,
      "longitude": 8.564520
    }
  ]
}
```

Consumers must collect chunks until `final: true`. When `has_more` is true,
repeat the request using `next_cursor`.

## Archiver service topics

```text
equine/v1/archivers/<archiver_id>/availability
equine/v1/archivers/<archiver_id>/status
```

Both are retained. Status reports counters, retention and current database size.

## Compatibility and evolution

- Topic API, point JSON, command JSON and history JSON have separate versions.
- Unknown fields must be ignored.
- Unsupported major versions or schema versions must be rejected explicitly.
- New optional fields may be added without changing a schema version.
- Meaning or encoding changes require a new schema version.
- End-to-end encryption will set encrypted payload schemas without exposing the
  device secret or plaintext telemetry to the broker.
