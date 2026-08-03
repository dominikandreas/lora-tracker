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

## Archive backup, verification and restore

Use the archiver's SQLite backup command for online backups; copying only the
main database file can omit committed WAL transactions. Verify every backup
before moving it off-host:

```bash
python -m lora_tracker_archiver backup \
  --database /data/lora-tracker-history.sqlite3 \
  --output /backups/history-$(date -u +%F).sqlite3
python -m lora_tracker_archiver check \
  --database /backups/history-$(date -u +%F).sqlite3
```

Schedule backups to storage with independent credentials and retention. Alert
on a nonzero exit status, rotate backups only after a new backup passes both
SQLite quick-check and foreign-key validation, and periodically restore one in
staging.

Pruning previews its match count by default. Add `--apply` only after checking
the reported cutoff and count:

```bash
python -m lora_tracker_archiver prune --retention-days 30
python -m lora_tracker_archiver prune --retention-days 30 --apply
```

For restore, stop every archiver process that can access the database, retain
the source backup, then use `restore --force`. The command validates the source,
creates a timestamped pre-restore backup of the current archive, atomically
replaces the database and removes stale WAL sidecars. Run `check` and a history
query before restarting MQTT ingestion.

## BLE behavior

BLE debugging is intentionally bounded after onboarding. A tracker with
incomplete configuration or no confirmed gateway keeps configuration BLE
available without a deadline so setup cannot expire halfway through. Lifecycle
changes are deferred outside the button handler, and enabling BLE uses a clean
restart path. After disabling BLE on a configured tracker, verify that wake
count and fix age continue to change after the next normal tracking cycle.

## Gateway logs without USB

After authenticating a gateway with its owner key, the app's **Read recent
logs** action retrieves the bounded in-memory log buffer over BLE or HTTP. BLE
returns the newest eight lines to avoid blocking LoRa polling with hundreds of
acknowledged GATT indications; HTTP returns all 25 buffered lines. The HTTP API
is:

```bash
curl -H "Authorization: Bearer <64-hex-owner-key>" \
  http://<gateway-ip>/api/v1/logs
```

The response contains a `lines` array. Logs are intentionally not persisted;
rebooting clears the buffer. Use the endpoint immediately before and after a
radio test. `Received packet`, `MQTT publish failed`, `ACK deferred by Germany
1% airtime limiter`, and `Sent ACK` identify
the main receive-to-ACK stages.

## Gateway network recovery

The gateway keeps its configured station Wi-Fi active while it starts a
fallback setup AP after a prolonged disconnect. It continues retrying the
station connection instead of becoming AP-only. Once station Wi-Fi returns,
the reported `network_ip` and the mDNS name are updated automatically. Prefer
the stable name `http://lora-gateway-<gateway-id>.local/` when the local network
supports mDNS; DHCP addresses can change after a router restart.

The gateway keeps Wi-Fi modem sleep enabled while BLE is active. The classic
ESP32 Wi-Fi driver aborts when Wi-Fi and BLE are both active with modem sleep
disabled, so `WIFI_PS_NONE` must not be used during that coexistence window.
On an already configured gateway, BLE closes after the 60-second startup
management window and releases its controller memory; the gateway then
reassociates with station Wi-Fi with modem sleep disabled. This avoids stale
Wi-Fi links seen on classic ESP32 boards during indefinite BLE advertising.
MQTT socket reads are bounded so an unavailable broker cannot starve device
management. During a LoRa relay/ACK guard, read-only HTTP requests such as log
retrieval continue to be served; configuration writes are briefly rejected
with `gateway_busy` and should be retried.

If both station and HTTP access disappear, verify the gateway still has power,
then hold USER for five seconds while it is running or hold it for 1.5 seconds
inside the post-boot configuration window. If BLE controller memory has already
been released, the five-second hold performs a controlled restart into a
ten-minute authenticated BLE/configuration window. The recovery AP remains
available while station reconnect is attempted, allowing the app to restore
Wi-Fi or enable OTA without a USB cable.

## No GNSS fixes

The tracker gives a cold or periodic recovery attempt at least 90 seconds of
continuous NMEA processing. Intermediate retries receive at least 30 seconds,
and their sleep interval grows after repeated failures. A manual GPS-page hold
provides 60–180 seconds. Typical no-fix causes include indoor storage, metal
roofs, antenna orientation or insufficient sky view.

The GPS page distinguishes `Vis` (satellites reported by GSV) from `Used`
(satellites in the GGA solution). `GPS WAIT NMEA` with zero UART bytes for 12
seconds triggers one receiver power/UART recovery. Bytes without any valid
sentences for 15 seconds trigger UART-only resynchronization, preserving the
receiver's acquisition state. BLE debugging must not change acquisition
success; if it does, capture the `GNSS acquisition policy`, `GNSS recovery
condition`, and `GNSS listen ended` log lines.

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

A gateway sends a radio ACK after it authenticates the complete frame and hands
every new point to its configured MQTT client. The SQLite archiver is optional:
an unavailable archive must not delay or prevent tracker ACKs. If MQTT is
unavailable or a point event cannot be handed off, the gateway withholds the
ACK and the tracker retries. MQTT point events use QoS 0, so a production
deployment that needs durable history should operate the optional archiver and
monitor its retained availability/status independently.

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

The Android application can connect to a private `ws://` broker after an
explicit warning. This enables certificate-free home deployments, but it sends
the MQTT username/password and gateway-decoded tracker data without transport
encryption. Use an isolated trusted LAN or VPN, and migrate to `wss://` when
practical.
MQTT connection failures remain visible alongside the retry countdown. Verify
that the configured port is an MQTT-over-WebSocket listener, not the broker's
raw MQTT listener on port 1883. If omitted, the application supplies port 1884
for `ws://` or 8884 for `wss://`; enter a port explicitly when the broker uses a
different listener.
Android saves the MQTT password automatically in Keystore-backed encrypted
storage. The hosted browser app saves it in origin-scoped localStorage; use a
trusted browser profile and a dedicated least-privilege MQTT account.

## Tracker-to-gateway pairing recovery

Pairing always commits the gateway registry first and the tracker confirmation
second. If the app reports that gateway registration was saved but tracker
confirmation failed, reconnect the tracker and press **Register and finish
pairing** again; the gateway upsert is idempotent. Watch the four pairing stages
to see whether MQTT, LAN, or Bluetooth was selected. A connected MQTT app can
register remotely through the broker; otherwise the app tries known station
addresses, mDNS, and nearby Bluetooth. If none work, enter the LAN IP shown by
the router or gateway display. The obsolete setup-AP address `192.168.4.1` is
discarded automatically. Do not manually copy LoRa keys.

The app stores a device owner key under the canonical device ID and local BLE
identifier. Export a full-authority access QR before clearing app data. If no
stored/exported copy remains, recovery requires factory reset and a new attended
claim; firmware never returns the owner key.
