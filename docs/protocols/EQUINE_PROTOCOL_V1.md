# LoRa Tracker telemetry protocol v1

This is the first explicit, versioned protocol shared by trackers, gateways and future applications.

## Identity

Each device has a canonical string ID such as `wera`. Protocol v1 derives a 64-bit `device_id_hash` with FNV-1a and places it in every frame. The hash is a compact routing identifier only. It is public and is **not** an encryption key or authenticator.

A later security step will provision a random per-device secret and use authenticated encryption. The routing hash will remain useful for selecting the correct key without revealing the friendly name.

## Common frame header

All integer fields use little-endian wire order.

| Field | Size | Meaning |
|---|---:|---|
| `magic` | 2 | `0x5145`, encoded as bytes `EQ` on ESP32 |
| `transport_version` | 1 | Envelope version, currently `1` |
| `message_type` | 1 | `1=history`, `2=ack`, reserved types for config and relay |
| `schema_version` | 1 | Version of the selected message body |
| `flags` | 1 | Reserved bits including `encrypted` and `relayed` |
| `device_id_hash` | 8 | Stable public routing identifier |

## History schema v1

The common frame is followed by:

- `boot_id` (`uint32`)
- `first_seq` (`uint32`)
- `total_dist_dam` (`uint16`)
- `batt_pct` (`uint8`)
- root latitude and longitude (`int32`, microdegrees)
- anchor and delta batches using the existing compressed history encoding

## ACK schema v1

The common frame is followed by:

- `boot_id` (`uint32`)
- `acked_seq` (`uint32`)

The ACK must contain the same `device_id_hash` as the uplink. A tracker rejects frames with the wrong magic, versions, type, device hash or boot ID.

## MQTT API v1

Canonical state topic:

```text
equine/v1/trackers/<16-character-device-hash>/state
```

Canonical availability topic:

```text
equine/v1/trackers/<16-character-device-hash>/availability
```

The gateway currently also publishes the previous `horse/<tracker-id>/...` topics as a migration aid. Canonical JSON includes:

- `api_version`
- `transport_version`
- `schema_version`
- `device_hash`
- location, distance, battery, RSSI, sequence and boot ID

## Upgrade order

1. Update the gateway first. It accepts both legacy packets and protocol-v1 packets.
2. Update the tracker second. It emits protocol v1 and requires protocol-v1 ACKs.

The next step is a multi-device registry in the gateway, replacing the current single configured tracker and single deduplication cursor.
