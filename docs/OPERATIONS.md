# LoRa Tracker operations and troubleshooting

## Normal tracker behavior

The tracker wakes, acquires GNSS, stores a point when appropriate, optionally
transmits a batch, and returns to deep sleep. The moving interval, stationary
intervals, quality thresholds and LoRa policy are configuration values.

A missing ACK does not delete data. Retries use exponential backoff and the
queued sequence remains available for later delivery.

## Useful MQTT topics

```text
equine/v1/trackers/<device_hash>/events/point
equine/v1/trackers/<device_hash>/state
equine/v1/trackers/<device_hash>/history/request
equine/v1/trackers/<device_hash>/history/response/<request_id>
equine/v1/gateways/<gateway_hash>/availability
equine/v1/gateways/<gateway_hash>/status
equine/v1/gateways/<gateway_hash>/commands/request
equine/v1/gateways/<gateway_hash>/commands/response/<request_id>
```

## Health checks

Check:

- gateway availability and status are retained and current;
- tracker latest state advances after movement;
- point sequence numbers increase;
- `timestamp_valid` is true for timestamp-v4 trackers;
- archiver status is online and its database grows;
- history requests finish with `final=true`;
- duplicate gateway receptions do not create duplicate points.

## Factory reset and configuration

Configuration writes are revision-checked. On HTTP `409`, reload the current
configuration and merge/retry. Factory reset clears the versioned configuration
and returns the device to onboarding.

The tracker’s GPIO0 user button is a boot-strapping pin. Do not hold it while
applying power or pressing reset; the ESP32-S3 may enter the ROM downloader
instead of running the firmware. Use the post-boot interaction window.

## BLE behavior

BLE debugging is intentionally bounded. Lifecycle changes are deferred outside
the button handler, and enabling BLE uses a clean restart path. After disabling
BLE, verify that wake count and fix age continue to change after the next normal
tracking cycle.

## No GNSS fixes

The tracker progressively reduces acquisition effort and lengthens sleep after
repeated no-fix cycles. Typical causes include indoor storage, metal roofs,
antenna orientation or insufficient sky view. A full acquisition is retried
periodically.

## Missing LoRa ACKs

Confirm tracker and gateway share frequency, bandwidth, spreading factor,
coding rate, sync word and preamble. Check gateway RSSI/logs. Failed ACKs retain
the queue and use 1/2/5/10-minute retry backoff by default.

For multiple gateways or future relays, ACK collision avoidance remains future
work.

## Database operation

The archiver uses SQLite. Keep the database on persistent storage, back it up,
and avoid copying it while a write transaction is active unless using SQLite’s
backup facilities. Retention is based on GNSS fix time when valid and receive
time otherwise.

## Web application

The app needs an MQTT WebSocket listener. Browser mixed-content rules block
`ws://` from an `https://` page, so use `wss://` in production. Clear site data
to reset IndexedDB and saved broker preferences.
