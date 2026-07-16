# LoRa protocol

The transport envelope is version 2. The only supported `HISTORY` schema is 2
and the only supported `ACK` schema is 1. Tracker and gateway reject every
other transport/schema combination and every frame without the encrypted flag.
All integers are packed little-endian. Every secure frame is carried inside the
current link envelope; raw transport-v2 frames without it are rejected.

## Repeater link envelope

```text
LinkHeaderV1
  uint16 magic        0x524c (wire bytes "LR")
  uint8  version      1
  uint8  hop_count    number of repeaters already traversed
  uint8  hop_limit    origin-selected maximum, 0..4
  uint8  flags        zero in link version 1
```

The link header is mutable and is not AES-GCM associated data. The secure frame
that follows remains byte-for-byte end-to-end authenticated. Each repeater
increments `hop_count` only when it is below both `hop_limit` and its configured
local cap. Lowering a visible hop limit can deny forwarding, but raising it
cannot bypass a repeater's local maximum. LoRa itself remains susceptible to
jamming and denial of service.

The six-byte header reduces the maximum secure frame to 249 bytes. Tracker
batch packing receives the reduced plaintext capacity and stops cleanly before
that limit. Repeaters accept only current `HISTORY` and `ACK` secure headers and
never decrypt either payload.

Forwarding identity is `(message_type, schema_version, device_id_hash, boot_id,
counter)`. This intentionally treats equivalent ACKs for one tracker sequence
as duplicates even if different receivers used different nonce prefixes.
History duplicates use the ordinary configured cache lifetime; ACK duplicates
use five seconds so later tracker retries can receive a fresh forwarded ACK.

## Authenticated envelope

```text
FrameHeaderV1
  uint16 magic                0x5145 (wire bytes "EQ")
  uint8  transport_version    2
  uint8  message_type         1 = HISTORY, 2 = ACK
  uint8  schema_version       2 for HISTORY, 1 for ACK
  uint8  flags                bit 0 encrypted, bit 2 timestamps present
  uint64 device_id_hash       public routing identifier

SecureFrameHeaderV2
  FrameHeaderV1 frame
  uint64 nonce_prefix         random for every sender boot
  uint32 boot_id              monotonic tracker boot generation
  uint32 counter              unique transmit counter, or acked_seq for ACK
```

`SecureFrameHeaderV2` is visible for routing but supplied to AES-256-GCM as
additional authenticated data. The 96-bit GCM nonce is the little-endian
concatenation of `nonce_prefix` and `counter`. Every frame ends with the
16-byte GCM tag. A tracker has a randomly generated 32-byte key; its gateway
registry entry must contain the same key.

A sender creates a fresh 64-bit nonce prefix at every boot and advances a
separate transmit counter for every history encryption, including retries.
Changing battery, distance or batch contents therefore cannot reuse a GCM
nonce while an older point remains at the queue head. The transmit counter is
covered by RTC integrity metadata; corruption advances the boot epoch and
rotates the nonce prefix before the queue is reset. ACK senders use their own
independently generated prefix and can safely repeat the same authenticated
ACK content for the same acknowledged sequence. Gateways reject boot IDs older
than the stored point cursor and sequences at or below the cursor as duplicates.
If the tracker loses or wraps
its monotonic boot counter, it rotates the LoRa key and returns to onboarding
instead of risking nonce/cursor reuse.

## Encrypted history plaintext

```text
HistoryPayloadV2
  uint32 first_seq
  uint32 root_unix_time_s
  uint16 total_dist_dam
  uint8  batt_pct

int32 root_latitude_microdegrees
int32 root_longitude_microdegrees
uint8 anchor_count
AnchorPointV1 anchors[anchor_count]
uint8 batch_count
... batches ...
```

`root_unix_time_s` is GNSS UTC and is valid only when
`FLAG_HAS_TIMESTAMPS` is set. A zero value with the flag clear represents an
untimed packet. `first_seq` is inside the authenticated ciphertext and is
independent from the envelope's per-encryption counter.

The spatial compression is:

1. Absolute root latitude and longitude as signed microdegrees.
2. Signed 16-bit anchors relative to the root in 10-microdegree units.
3. Batches grouped by anchor.
4. Signed 8-bit latitude/longitude deltas relative to that anchor.

Every non-root point appends an unsigned LEB128 time delta:

```text
uint8  anchor_index
uint8  point_count
repeat point_count:
  int8   relative_latitude
  int8   relative_longitude
  ULEB128 seconds_since_previous_point
```

The delta is relative to the previous point in sequence order, including across
batch boundaries. The tracker stops before a missing/backwards timestamp;
gateways reject malformed, overflowing, truncated or implausible values.

## Encrypted ACK plaintext

```text
AckPayloadV1
  uint32 acked_seq
```

The payload must equal the authenticated envelope counter, the device hash and
boot ID must match the outstanding tracker batch, and the sequence must clear
at least one queued point. A missing, stale, inconsistent or invalid-tag ACK
never clears tracker history.

The tracker ignores echoed `HISTORY`, ACKs for other trackers and malformed
packets while keeping the ACK receive window open. The default window is 15
seconds for the default two-hop/SF10 profile and is configurable up to 30
seconds; deployment timing must be measured.

## Key handling limits

The implementation protects LoRa confidentiality and integrity against passive
observation, forgery and simple replay. It does not yet provide automated key
rotation/revocation, a secure-element boundary, a random public routing ID or a
gateway design that routes ciphertext without holding tracker keys. Those
lifecycle and blast-radius improvements remain production work.
