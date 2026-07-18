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

The gateway's broker root CA is stored in a dedicated active/backup NVS pair
because a PEM bundle does not fit the compact configuration blob. It is
validated and committed with the same revisioned transaction; rollback restores
the matching previous CA value.

Wi-Fi and MQTT passwords are never returned by the read API;
`wifi_password_set` and `mqtt.password_set` expose only whether a value exists.
The gateway similarly reports `mqtt.ca_certificate_set` without returning the
PEM contents. The certificate is public trust material, but omitting it from
routine reads keeps configuration responses compact.
The tracker returns its `lora_aead_key` through this authenticated local
interface because an operator must transfer it to the gateway. Treat the full
response as a provisioning secret and do not save it in logs.

For secret fields:

- omitted, empty, or `__KEEP__`: retain the existing secret;
- `__CLEAR__`: erase it;
- any other value: replace it.

`mqtt_ca_certificate` uses `__KEEP__` to retain, an empty value to clear, or a
PEM certificate bundle up to 4096 bytes to replace. Clearing it while TLS is
enabled fails validation.

## Wi-Fi HTTP API

All mutation bodies use `application/x-www-form-urlencoded`. Every HTTP route
requires Basic authentication with username `admin` and the device's unique
admin credential. The credential is also the WPA2 password of the fallback
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


### Replace the administrator credential

`POST /api/v1/credentials`

```text
new_password=<12-to-24-printable-non-space-characters>
confirm_password=<same-value>
```

This write-only operation requires the current HTTP authentication. A gateway
also requires its physical write window. The credential is stored separately
from the revisioned configuration because it protects access to that
configuration; it becomes both the HTTP Basic password and fallback AP WPA2
password after reboot. Factory reset removes it and generates a new value.

## Tracker onboarding

After factory reset, or after an explicit post-boot Wi-Fi setup gesture, the tracker provides both transports concurrently:

- Wi-Fi AP: `LoRaTracker-<device_id>`
- BLE name: `LT-<device_id>`

An erased generic release generates a unique 20-character admin credential and
shows it on the tracker display during onboarding. A factory may instead set a
unique `factory_admin_password` in each device's `secrets.h`. The firmware
refuses to start the AP or authorize HTTP with fewer than 12 characters. The
phone UI can replace it through `POST /api/v1/credentials`.

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
CLAIM <new-admin-password>
AUTH <admin-password>
HELLO
GET CONFIG
PATCH expected_revision=4&device_name=Wera&moving_sleep_s=45
SET_CREDENTIAL <new-admin-password>
ROLLBACK 5
FACTORY_RESET FACTORY_RESET
REBOOT
```

The RX characteristic requires LE Secure Connections with MITM protection. A
fresh random pairing PIN is generated for each bounded BLE session and shown on
the tracker display. While `onboarding_required=true`, `CLAIM` is allowed only
inside that physically opened provisioning window; it validates and stores the
new credential and authenticates the current session. Otherwise `AUTH` is
mandatory before responses or debug logs are exposed. The PATCH body uses the
same URL-encoded fields and secret semantics as HTTP. A claim alone does not
mark configuration complete; a successful transactional PATCH still does that.
After authentication, `SET_CREDENTIAL` replaces the write-only administrator
credential and reboots so the fallback AP adopts the new password.

## Gateway onboarding and write protection

An unprovisioned gateway automatically starts:

```text
SSID: LoRaGateway-<gateway_id>
Password: the gateway's unique onboarding password
```

For a provisioned gateway, configuration reads remain available on the local network, but writes are locked. Holding the gateway USER button for five seconds unlocks writes for ten minutes. If Wi-Fi is unavailable, the gateway exposes its fallback AP; it remains read-only until that physical hold unless it is unprovisioned.

The erased gateway generates its unique admin credential and displays it with
the setup address on the Heltec V2 OLED; a factory may inject
`factory_admin_password` per device before first flash. Replace it through the
phone UI while the physical write window is open.

## Tracker patch fields

Identity and local setup:

```text
device_id, device_name, wifi_ssid, wifi_password, lora_aead_key,
ble_debug_enabled, battery_sense_enabled
```

LoRa:

```text
lora_frequency_hz, lora_bandwidth_hz, lora_tx_power_dbm,
lora_sf, lora_coding_rate, lora_preamble_length, lora_sync_word,
lora_relay_hop_limit
```

Tracker communication policy:

```text
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
mqtt_password, mqtt_ca_certificate, mqtt_base_topic, mqtt_buffer_size,
dedup_save_interval, wifi_retry_interval_ms, mqtt_retry_interval_ms
```

Gateway LoRa fields are identical to the tracker radio field names; tracker
batching, ACK timeout and retry fields do not apply to the gateway.

Registry:

```text
tracker_count
tracker.0.id
tracker.0.name
tracker.0.lora_aead_key
tracker.0.enabled
...
tracker.11.*
```

The complete candidate is validated after all fields are applied, so `tracker_count` and the corresponding entries can be sent in any order.

## Security scope

HTTP has per-device password authentication and gateway mutations additionally
require a physical unlock. BLE uses Secure Connections/MITM and an authenticated
application session. A tracker may establish its first authenticated session
with `CLAIM` only during unprovisioned physical setup. Remaining provisioning
work includes:

- a random per-device cryptographic provisioning secret distinct from the AP password;
- purpose-separated provisioning/session keys rather than deriving pairing from the admin credential;
- QR transfer of public identity and provisioning material;
- encrypted export/import bundles;
- automated LoRa key rotation, revocation and recovery.
