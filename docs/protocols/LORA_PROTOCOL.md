# LoRa protocol

The transport envelope is version 1 and the only supported `HISTORY` message
schema is 2. A gateway rejects every other transport or history schema.

All integers are packed little-endian.

## Frame and history header

```text
FrameHeaderV1
  uint16 magic                0x5145 (wire bytes "EQ")
  uint8  transport_version    1
  uint8  message_type         1 = HISTORY
  uint8  schema_version       2
  uint8  flags                bit 2 = timestamps present
  uint64 device_id_hash

HistoryHeaderV2
  FrameHeaderV1 frame
  uint32 boot_id
  uint32 first_seq
  uint32 root_unix_time_s
  uint16 total_dist_dam
  uint8  batt_pct
```

`root_unix_time_s` is UTC from GNSS and is valid only when
`FLAG_HAS_TIMESTAMPS` is set. A zero value with the flag clear represents an
untimed packet.

## Spatial and temporal body

The existing spatial compression is retained:

1. Absolute root latitude and longitude as signed microdegrees.
2. A list of signed 16-bit anchors relative to the root in 10-microdegree units.
3. Batches grouped by anchor.
4. Signed 8-bit latitude and longitude deltas relative to that anchor.

History schema v2 appends one unsigned LEB128 value after every non-root point:

```text
int8   relative_latitude
int8   relative_longitude
ULEB128 seconds_since_previous_point
```

The time delta is relative to the previous point in sequence order, including
across batch boundaries. It is not relative to the batch anchor.

Typical cost:

- 0–127 seconds: 1 byte
- 128–16,383 seconds: 2 bytes
- 16,384–2,097,151 seconds: 3 bytes
- Maximum uint32 delta: 5 bytes

This is more efficient than storing a 32-bit timestamp per point while still
supporting multi-day gaps.

## Timestamp rules

- Points in one timed packet must be monotonic.
- The tracker stops packing before a missing or backwards timestamp.
- Gateways reject malformed, overflowing, truncated or implausible timestamps.
- A current-schema packet may explicitly omit timestamps by clearing the flag.
- The gateway publishes milliseconds in MQTT as `fix_time_unix_ms`.

## Supported schema set

Tracker and gateway firmware must be deployed as a matched release. The ACK
message schema is 1, the history schema is 2, and no older packet layout is
decoded.
