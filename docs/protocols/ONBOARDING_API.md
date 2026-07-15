# Onboarding and configuration API

This document defines the provisioning transport over the versioned,
CRC-protected configuration blobs in [`CONFIGURATION.md`](CONFIGURATION.md).

## Transaction model

Configuration changes never mutate the live blob field-by-field. The device:

1. copies the current configuration into a candidate;
2. checks `expected_revision` against the active revision;
3. applies all supplied fields to the candidate;
4. finalizes its header and CRC with revision + 1;
5. validates all individual and cross-field constraints;
6. copies the previous active blob to the backup slot;
7. writes the candidate as the new active blob.

Invalid or stale requests leave the active configuration unchanged. A successful rollback is stored as a new revision rather than moving the revision backwards.

Secrets are never returned by the read API. `wifi_password_set` and `mqtt.password_set` expose only whether a value exists.

For secret fields:

- omitted, empty, or `__KEEP__`: retain the existing secret;
- `__CLEAR__`: erase it;
- any other value: replace it.

## Wi-Fi HTTP API

All mutation bodies use `application/x-www-form-urlencoded`. Every HTTP route
requires Basic authentication with username `admin` and the device's unique
onboarding password. The password is also the WPA2 credential of the fallback
access point and must be at least 12 characters.

### Discovery

`GET /api/v1/onboarding`

Returns the role, API version, current revision, onboarding state, available transports, and whether gateway write access is physically unlocked.

### Read configuration

`GET /api/v1/config`

Returns all non-secret configuration values in JSON.

### Transactional patch

`POST /api/v1/config`

Required field:

- `expected_revision=<current revision>`

Optional control field:

- `reboot=1`: reboot even if the changed fields do not inherently require it.

A `409` response means the app must reload the configuration and merge/retry. A successful response reports the new revision and `reboot_required`.

Example:

```text
expected_revision=4&device_name=Wera&moving_sleep_s=45&max_hdop=1.8&reboot=1
```

### Rollback

`POST /api/v1/config/rollback`

```text
expected_revision=5
```

Restores the validated backup as a new revision and requires reboot.

### Factory reset

`POST /api/v1/factory-reset`

```text
confirm=FACTORY_RESET
```

The next boot enters onboarding automatically.

### Reboot

`POST /api/v1/reboot`

## Tracker onboarding

After factory reset, or after an explicit post-boot Wi-Fi setup gesture, the tracker provides both transports concurrently:

- Wi-Fi AP: `LoRaTracker-<device_id>`
- BLE name: `EqTrk-<device_id>`

Set a different `onboarding_ap_password` in each device's `secrets.h`. The
firmware refuses to start the AP or authorize HTTP if it is shorter than 12
characters, and never prints it to logs.

The tracker also tries station mode during an explicit setup session when it is already provisioned. Timer wake-ups never expose configuration services.

## Tracker BLE protocol

The tracker reuses the Nordic UART-style BLE service:

```text
Service: 6E400001-B5A3-F393-E0A9-E50E24DCCA9E
RX/write: 6E400002-B5A3-F393-E0A9-E50E24DCCA9E
TX/notify: 6E400003-B5A3-F393-E0A9-E50E24DCCA9E
```

Commands are UTF-8 text terminated by `\n`. Responses are JSON terminated by `\n`. The tracker emits responses in 18-byte notifications, so clients concatenate notifications until the newline.

Commands:

```text
HELLO
GET CONFIG
PATCH expected_revision=4&device_name=Wera&moving_sleep_s=45
ROLLBACK 5
FACTORY_RESET FACTORY_RESET
REBOOT
```

The PATCH body uses the same URL-encoded fields and secret semantics as HTTP.

## Gateway onboarding and write protection

An unprovisioned gateway automatically starts:

```text
SSID: LoRaGateway-<gateway_id>
Password: the gateway's unique onboarding password
```

For a provisioned gateway, configuration reads remain available on the local network, but writes are locked. Holding the gateway USER button for five seconds unlocks writes for ten minutes. If Wi-Fi is unavailable, the gateway exposes its fallback AP; it remains read-only until that physical hold unless it is unprovisioned.

Provision `onboarding_ap_password` in `secrets.h` before the first flash.

## Tracker patch fields

Identity and local setup:

```text
device_id, device_name, wifi_ssid, wifi_password,
ble_debug_enabled, battery_sense_enabled
```

LoRa:

```text
lora_frequency_hz, lora_bandwidth_hz, lora_tx_power_dbm,
lora_sf, lora_coding_rate, lora_preamble_length, lora_sync_word,
lora_tx_interval_s, lora_tx_min_points, lora_ack_timeout_ms,
lora_retry_backoff_1_s ... lora_retry_backoff_4_s
```

GNSS and movement:

```text
min_distance_m, min_speed_kmph, max_hdop, min_satellites,
max_speed_mps, max_fix_age_s,
gps_timeout_1_ms ... gps_timeout_4_ms,
gps_full_retry_interval_s, gps_initial_listen_ms,
gps_light_sleep_chunk_ms, gps_listen_window_ms,
movement_speed_threshold_kmph, movement_displacement_threshold_m,
movement_evidence_distance_m, movement_evidence_step_m,
movement_direction_tolerance_deg, movement_evidence_required
```

Storage and sleep:

```text
history_point_spacing_m, save_distance_threshold_m, nvs_save_interval_s,
moving_sleep_s, stationary_sleep_s, long_stationary_sleep_s,
no_fix_sleep_1_s ... no_fix_sleep_4_s,
stationary_fixes_for_long_sleep, stationary_fixes_for_max_sleep
```

## Gateway patch fields

Gateway and network:

```text
gateway_id, gateway_name, wifi_ssid, wifi_password,
mqtt_host, mqtt_port, mqtt_tls_enabled, mqtt_username,
mqtt_password, mqtt_base_topic, mqtt_buffer_size,
dedup_save_interval, wifi_retry_interval_ms, mqtt_retry_interval_ms
```

LoRa fields are identical to the tracker LoRa field names.

Registry:

```text
tracker_count
tracker.0.id
tracker.0.name
tracker.0.enabled
...
tracker.11.*
```

The complete candidate is validated after all fields are applied, so `tracker_count` and the corresponding entries can be sent in any order.

## Security scope

HTTP has per-device password authentication and gateway mutations additionally
require a physical unlock. BLE commands are still unauthenticated beyond radio
proximity and are therefore a release blocker. A future protocol revision must
add:

- a random per-device cryptographic provisioning secret distinct from the AP password;
- authenticated app/device sessions;
- QR transfer of public identity and provisioning material;
- encrypted export/import bundles;
- authenticated telemetry and ACK frames.
