# LoRa Tracker LoRa protocol — history schema v2

The transport envelope remains version 1. Only the `HISTORY` message schema is
incremented from 1 to 2. Gateways should accept both history schemas during a
rolling upgrade; trackers emit schema 2 after this update.

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
- Schema-v1 and legacy points remain accepted but have no GNSS timestamp.
- The gateway publishes milliseconds in MQTT as `fix_time_unix_ms`.

## Compatibility

Safe update order:

1. Flash the schema-v2 gateway, which accepts legacy, history-v1 and history-v2.
2. Verify existing trackers still receive ACKs.
3. Flash trackers with schema-v2 firmware.
4. Update archivers/apps to point schema v2; the supplied archiver also accepts
   point schema v1 during migration.

ACK schema remains version 1 and is unchanged.
