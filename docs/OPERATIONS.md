# LoRa Tracker operations and troubleshooting

## Normal tracker behavior

The tracker wakes, acquires GNSS, stores a point when appropriate, optionally
transmits a batch, and returns to deep sleep. The moving interval, stationary
intervals, quality thresholds and LoRa policy are configuration values.

A missing ACK does not delete data. Retries use exponential backoff and the
queued sequence remains available for later delivery. Daily distance rollover
never clears telemetry, and a full RTC queue rejects new samples instead of
overwriting unacknowledged points. The queue is still RTC-only and is not
preserved across power loss or a hard reset; this remains a release blocker.

## Useful MQTT topics

```text
lora-tracker/v1/trackers/<device_hash>/events/point
lora-tracker/v1/trackers/<device_hash>/state
lora-tracker/v1/trackers/<device_hash>/history/request
lora-tracker/v1/trackers/<device_hash>/history/response/<request_id>
lora-tracker/v1/gateways/<gateway_hash>/availability
lora-tracker/v1/gateways/<gateway_hash>/status
lora-tracker/v1/gateways/<gateway_hash>/commands/request
lora-tracker/v1/gateways/<gateway_hash>/commands/response/<request_id>
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

During normal operation, tap to change display pages and hold for the action
printed on the selected page. The status, GPS, radio and debug actions reset
distance, request a GNSS acquisition, transmit queued history with an ACK
window, and toggle BLE debug logs respectively. See
[Tracker button controls](ONBOARDING.md#tracker-button-controls) for timing and
safety details.

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

## MQTT TLS waits for time

The gateway deliberately refuses certificate-validated MQTT until its UTC clock
is at least 2024-01-01. Allow DNS and outbound NTP (UDP 123) to
`pool.ntp.org` or `time.cloudflare.com`, then check for the log message that the
MQTT connection follows NTP synchronization. A CA chain without a trusted clock
is not treated as sufficient TLS validation.

## Missing LoRa ACKs

Confirm tracker, gateway and repeaters use the same supported Germany radio
profile. Check gateway RSSI/logs and repeater forwarded, suppressed, queue-drop
and airtime-deferral counters. Failed ACKs retain the queue and use
1/2/5/10-minute retry backoff by default.

A gateway sends a radio ACK only after the archiver confirms every new point on
its `archive/ack` topic. If the archiver, broker, ACL or confirmation route is
unavailable, the gateway deliberately withholds the ACK and the tracker retries.
Broker ACLs must allow only the archiver to publish gateway archive-confirmation
topics. This converts QoS-0 point publication into application-confirmed durable
SQLite delivery; duplicate events and confirmations are expected and idempotent.

The link uses deterministic repeater priority slots and peer suppression, but
hidden repeaters and multiple receivers can still produce duplicate ACKs or
collisions. Use the smallest useful hop limit, keep repeater IDs unique and
measure the installed topology. See [repeaters](REPEATERS.md).

## Database operation

The archiver uses SQLite. Keep the database on persistent storage, back it up,
and avoid copying it while a write transaction is active unless using SQLite’s
backup facilities. Retention is based on GNSS fix time when valid and receive
time otherwise.

## Web application

The app needs an MQTT WebSocket listener. Browser mixed-content rules block
`ws://` from an `https://` page, so use `wss://` in production. History pages
are requested automatically with a 100-page safety bound. Local history is
pruned after 180 days and capped at 250,000 points. Clear site data to reset
IndexedDB and saved broker preferences.
