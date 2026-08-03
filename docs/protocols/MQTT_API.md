# MQTT API

The topic API remains `v1`. Point JSON and history response schemas are now
version 2 to carry tracker fix time. MQTT telemetry remains an untrusted
transport unless TLS is used. The tracker-registration command is the narrow
exception: its secret-bearing fields have an owner-key AES-GCM envelope even
when the broker connection itself is plaintext.

## Point events and latest state

```text
lora-tracker/v1/trackers/<device_hash>/events/point   non-retained
lora-tracker/v1/trackers/<device_hash>/state          retained
```

```json
{
  "api_version": 1,
  "point_schema_version": 2,
  "transport_version": 2,
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

## Optional archive receipt

```text
lora-tracker/v1/gateways/<gateway_hash>/archive/ack
```

After a point transaction commits, an archiver may publish the exact `point_id`
as a non-retained QoS 1 receipt to the receiving gateway topic. It sends a
receipt for both a first insert and an idempotent duplicate. Receipts are
useful for archive monitoring, but the gateway does not subscribe to or depend
on them for deduplication or LoRa ACK progression. Broker ACLs may restrict
publication on this route to the archiver role.

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

```text
lora-tracker/v1/gateways/<gateway_hash>/availability
lora-tracker/v1/gateways/<gateway_hash>/status
lora-tracker/v1/gateways/<gateway_hash>/commands/request
lora-tracker/v1/gateways/<gateway_hash>/commands/response/<request_id>
```

Status is retained and includes `gateway_id`, `gateway_hash`,
`config_revision`, the current station `network_ip`, stable mDNS `hostname`,
Wi-Fi/MQTT state, uptime, free heap, and tracker counts. `network_ip` is never
the setup-AP address in this status.

Read-only command schema 1 operations are `ping`, `status.get`, and
`registry.get`. `registry.upsert` is the narrow idempotent tracker-registration
operation used by the app. Its request metadata is visible, but its registration
fields are an AES-256-GCM envelope encrypted with the gateway owner key:

```json
{
  "api_version": 1,
  "schema_version": 1,
  "request_id": "random-request-id",
  "command": "registry.upsert",
  "nonce": "24-hex-characters",
  "ciphertext": "base64-ciphertext-and-16-byte-tag"
}
```

Authenticated plaintext contains `device_id`, `device_name`,
`lora_aead_key`, and `expected_revision`. The additional authenticated data is
`lora-tracker|1|1|<request_id>|registry.upsert`. A revision mismatch fails
closed and causes the gateway to republish current status. This makes a captured
registration envelope non-replayable after its original configuration commit.
Broker ACLs must still restrict gateway command topics; encryption does not
replace least-privilege MQTT accounts.

The optional archive-receipt route above does not affect tracker ACK progression.
