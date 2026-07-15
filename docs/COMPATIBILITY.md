# LoRa Tracker compatibility and migrations

## Current versions

| Layer | Current | Compatibility behavior |
|---|---:|---|
| LoRa transport envelope | 1 | Gateway recognizes versioned frames and legacy unversioned packets |
| History message schema | 2 | Gateway accepts history schemas 1 and 2; tracker emits 2 |
| ACK schema | 1 | Unchanged between history schemas |
| Persistent configuration | 1 | Active/backup CRC-protected fixed-size blobs |
| Onboarding API | 1 | HTTP form encoding and newline-delimited BLE commands |
| MQTT topic API | 1 | Stable `equine/v1/...` hierarchy |
| Point JSON schema | 2 | Archiver and web app also accept schema 1 |
| History request/response schema | 2 | Archiver accepts requests 1 and 2 during migration |

## Upgrade order

Upgrade consumers before producers:

1. Archiver
2. Gateway
3. Tracker
4. Web/app clients

This preserves ACK and storage behavior throughout the rollout.

## Legacy packet mapping

Unversioned LoRa packets contain no device identity. The gateway configuration
therefore permits only one registered tracker to accept legacy packets. Remove
legacy support after every tracker has moved to the versioned envelope.

## Deduplication

The stable identifier is `<device_hash>:<boot_id>:<seq>`. The archiver safely
merges duplicate publications from several gateways while preserving reception
metadata.

`boot_id` and sequence behavior must never be reset casually. Factory-reset and
firmware migration procedures should ensure a new boot identity when old
sequence values could be reused.

## Timestamp migration

History schema v2 has one absolute UTC timestamp and ULEB128 deltas. Legacy and
history-v1 points have no GNSS observation time; the gateway emits them as
untimed and the archiver uses receive time as `effective_time_unix_ms`.

The root timestamp is unsigned 32-bit Unix seconds, valid through 2106. A future
protocol revision must define the post-2106 strategy well before it matters.

## Configuration migration

On first boot without a valid `eqcfg` blob:

- tracker firmware imports legacy `ssid`, `pw` and `ble_log` values;
- gateway firmware seeds from `secrets.h` and the former Wera registry entry.

A valid backup configuration repairs a corrupt active slot. Rollback writes a
new revision instead of reducing the revision number.

## Downgrades

Downgrading firmware is not guaranteed to preserve newer configuration fields,
RTC layouts or history schemas. Export or document the active configuration,
clear queues safely, and test on a spare device before downgrading deployed
hardware.
