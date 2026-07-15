# MQTT API

The topic API remains `v1`. Point JSON and history response schemas are now
version 2 to carry tracker fix time. MQTT remains an untrusted transport until
end-to-end authenticated encryption is added.

## Point events and latest state

```text
lora-tracker/v1/trackers/<device_hash>/events/point   non-retained
lora-tracker/v1/trackers/<device_hash>/state          retained
```

```json
{
  "api_version": 1,
  "point_schema_version": 2,
  "transport_version": 1,
  "schema_version": 2,
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
  "timestamp_valid": true,
  "fix_time_unix_ms": 1784123456000,
  "time_source": "gnss",
  "gateway_uptime_ms": 812345
}
```

When a current history packet explicitly has no valid GNSS timestamp, the
gateway emits:

```json
{
  "timestamp_valid": false,
  "fix_time_unix_ms": 0,
  "time_source": "unavailable"
}
```

Consumers must prefer `fix_time_unix_ms` only when `timestamp_valid` is true.

## History request

```text
lora-tracker/v1/trackers/<device_hash>/history/request
```

```json
{
  "api_version": 1,
  "schema_version": 2,
  "request_id": "phone-42",
  "from_unix_ms": 1784000000000,
  "to_unix_ms": 1784100000000,
  "limit": 250,
  "cursor": 0
}
```

The archiver accepts only history request schema 2. Time filtering uses GNSS
fix time when available and server receive time for explicitly untimed points.

## History response

```text
lora-tracker/v1/trackers/<device_hash>/history/response/<request_id>
```

Every returned point includes:

- `fix_time_unix_ms`
- `timestamp_valid`
- `time_source`
- `received_at_ms`
- `effective_time_unix_ms`
- `reception_gateway_count`
- `best_rssi`

`effective_time_unix_ms` is GNSS fix time when valid, otherwise receive time.
It is the recommended display and filtering field.

Responses remain chunked and use `final`, `has_more`, and `next_cursor`.

## Stable identity and deduplication

`point_id` remains `<device_hash>:<boot_id>:<seq>`. Several gateways may publish
the same tracker point; archivers must deduplicate by `point_id` and may preserve
separate reception metadata.

## Gateway management and archiver status

All Step-5 gateway command, availability, status, and archiver status topics are
unchanged. Only point and history JSON schema versions change.
