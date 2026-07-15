# Equine persistent configuration schema v1

This step adds a versioned, CRC-protected persistent configuration to the
tracker and gateway. It is the storage foundation for BLE/Wi-Fi onboarding in
the next implementation step.

## Storage envelope

Both roles store a fixed-size configuration blob in ESP32 Preferences namespace
`eqcfg` under two keys:

- `active`: the configuration used at boot
- `backup`: the last independently validated active configuration

Every blob begins with `ConfigHeaderV1`:

| Field | Purpose |
|---|---|
| `magic` | Distinguishes Equine configuration from unrelated NVS data |
| `schema_version` | Configuration data-model version |
| `struct_size` | Rejects incompatible layouts before decoding |
| `revision` | Monotonic revision for synchronization and conflict handling |
| `role` | Tracker or gateway |
| `crc32` | Detects corruption and incomplete writes |

On load, firmware tries `active`, then `backup`, then factory defaults. A valid
backup repairs the active slot. Saving first copies the previous valid active
blob to backup and then writes the newly validated active blob.

## TrackerConfigV1

The tracker configuration contains:

- canonical device ID, display name and derived public device hash
- Wi-Fi setup credentials and bounded BLE-debug preference
- LoRa frequency, bandwidth, spreading factor, coding rate, preamble, sync word
  and TX power
- normal batching, ACK timeout and retry-backoff policy
- GPS quality and anti-teleport thresholds
- acquisition timeout/backoff policy
- movement classifier thresholds
- history spacing and NVS checkpoint policy
- moving, stationary, long-stationary and no-fix sleep durations

The history buffer size and wire delta unit remain firmware-layout constants;
changing them requires a state/protocol migration rather than a normal setting.

### Tracker migration

When no valid configuration blob exists, the first boot creates schema v1 from
factory defaults and imports the old `tracker` namespace values:

- `ssid`
- `pw`
- `ble_log`

Existing distance, boot ID, daily age, history-in-RTC and dedup/transmission
state are not part of the configuration blob and remain handled separately.

Factory reset clears both the old tracker state namespace and `eqcfg`.

## GatewayConfigV1

The gateway configuration contains:

- gateway ID and display name
- Wi-Fi credentials
- MQTT host, port, username, password and base topic
- LoRa parameters
- MQTT buffer and reconnect settings
- deduplication checkpoint interval
- up to 12 tracker registrations, each with identity, enabled state, legacy
  LoRa mapping and legacy MQTT alias policy

The first boot seeds the configuration from `secrets.h` and migrates the former
compile-time Wera registry entry. Per-tracker deduplication namespaces are based
on the device hash, so existing cursors remain compatible.

`mqtt_tls_enabled` is reserved in schema v1 but validation currently requires it
to remain false until the TLS transport step is implemented.

## Validation

Configuration is rejected unless, among other constraints:

- canonical IDs contain lowercase letters, digits, `_` or `-`
- device hashes are unique in the gateway registry
- no more than one tracker accepts unversioned legacy LoRa
- LoRa values are within the supported EU-band and modem ranges
- timeout, sleep and retry sequences are internally consistent
- strings are terminated and all floating-point thresholds are finite
- CRC, role, schema and structure size match

## Read-only API

Both devices expose `GET /api/v1/config` while their web server is active. The
response deliberately omits Wi-Fi and MQTT passwords. The gateway's existing
`GET /api/v1/trackers` runtime endpoint remains available.

Configuration mutation is intentionally not exposed yet. The next step adds the
onboarding/configuration API, validation responses, transactional updates and
restart/apply semantics using these storage functions.
