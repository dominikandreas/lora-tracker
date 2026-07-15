# Persistent configuration schema

The tracker and gateway use a versioned, CRC-protected persistent
configuration. The current schema version is 2.

## Storage envelope

Both roles store a fixed-size configuration blob in ESP32 Preferences namespace
`eqcfg` under two keys:

- `active`: the configuration used at boot
- `backup`: the last independently validated active configuration

Every blob begins with `ConfigHeaderV1`:

| Field | Purpose |
|---|---|
| `magic` | Distinguishes tracker configuration from unrelated NVS data |
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

When no valid configuration exists, first boot creates a fresh schema-2 blob
from the provisioning seeds in `secrets.h`. Older configuration layouts are
not imported. Runtime distance, boot ID, history and transmission state remain
separate from the configuration blob.

## GatewayConfigV1

The gateway configuration contains:

- gateway ID and display name
- Wi-Fi credentials
- MQTT host, port, username, password, PEM root CA and base topic
- LoRa parameters
- MQTT buffer and reconnect settings
- deduplication checkpoint interval
- up to 12 tracker registrations, each with identity, 256-bit radio key and
  enabled state

First boot seeds the configuration from `secrets.h`. Per-tracker deduplication
namespaces are based on the device hash.

`mqtt_tls_enabled` selects verified TLS. The gateway requires a PEM root CA from
the runtime `mqtt_ca_certificate` field; `secrets.h` is an optional factory
seed. Plaintext MQTT is blocked unless its separate test-only build seed
explicitly allows it.

## Validation

Configuration is rejected unless, among other constraints:

- canonical IDs contain lowercase letters, digits, `_` or `-`
- device hashes are unique in the gateway registry
- LoRa values are within the supported EU-band and modem ranges
- timeout, sleep and retry sequences are internally consistent
- strings are terminated and all floating-point thresholds are finite
- CRC, role, schema and structure size match

## Read-only API

Both devices expose `GET /api/v1/config` while their web server is active. The
response deliberately omits Wi-Fi and MQTT passwords. The gateway's existing
`GET /api/v1/trackers` runtime endpoint remains available.

Mutation uses the transactional onboarding API described in
[`ONBOARDING_API.md`](ONBOARDING_API.md).
