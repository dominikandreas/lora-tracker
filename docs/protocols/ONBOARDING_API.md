# Device ownership and configuration API

## Owner-key model

Every claimed device stores one random 256-bit owner key encoded as 64
hexadecimal characters. There is no PIN, password, OS Bluetooth bond, or
separate OTA credential.

- Factory reset erases the owner key and opens attended claim.
- `CLAIM` succeeds exactly once while an unclaimed device is in configuration
  mode.
- BLE and HTTP use the same key.
- Firmware never returns the key.
- QR transfer is an application record, not a firmware endpoint.

Discovery is public so the app can decide whether to claim or authenticate.
Configuration remains secret-bearing and requires authorization.

## HTTP transport

Bodies use `application/x-www-form-urlencoded`.

### Public routes

`GET /enable-config` serves a local, dependency-free gateway page that stores
the entered owner key in that browser's local storage and sends it only to that
gateway. It can open the authenticated dashboard (including read-only settings
and live logs) without changing device state, or enable the ten-minute
configuration/OTA window. Use **Forget key on this browser** to remove the
stored value on shared devices.

`POST /api/v1/session` accepts `Authorization: Bearer <owner-key>` and issues a
random, memory-only, HttpOnly session cookie lasting at most ten minutes. The
dashboard's normal form submissions and log polling use that cookie. The
cookie and configuration/OTA window expire independently.

`GET /api/v1/onboarding` returns:

- `api_version`, `role`, `device_id`, and `revision`;
- `onboarding_required` and role-specific completion state;
- `config_mode` where applicable;
- `owner_key_configured`;
- supported transports.

`POST /api/v1/claim` accepts `owner_key=<64-hex>`. It returns `409` if the
device is already claimed or not in an attended claim window.

### Owner-authorized routes

Send `Authorization: Bearer <owner-key>`:

- `GET /api/v1/config`
- `POST /api/v1/config`
- `POST /api/v1/config/rollback`
- `POST /api/v1/config-mode`
- `POST /api/v1/reboot`
- `POST /api/v1/factory-reset`
- tracker `POST`/`DELETE /api/v1/gateway-pairing`
- gateway `GET`/`POST /api/v1/trackers`

`POST /api/v1/config-mode` enables configuration OTA for ten minutes on an
already configured device. A tracker whose configuration or gateway pairing is
incomplete has no setup deadline, so it cannot become unreachable halfway
through onboarding. Gateway and repeater responses include `duration_s=600`
after onboarding.

Factory reset requires `confirm=FACTORY_RESET`. Rollback and configuration
patches require `expected_revision=<current revision>`.

## BLE transport

Trackers and gateways expose the Nordic-UART-style service:

```text
Service: 6E400001-B5A3-F393-E0A9-E50E24DCCA9E
RX/write: 6E400002-B5A3-F393-E0A9-E50E24DCCA9E
TX/indicate: 6E400003-B5A3-F393-E0A9-E50E24DCCA9E
```

The RX characteristic is a normal GATT write characteristic. It deliberately
does not request encryption, MITM, pairing, or bonding from the OS. Commands
are UTF-8 terminated by `\n`; JSON responses are newline-terminated and may be
split into 18-byte acknowledged indications. Indications are required because
unacknowledged notification bursts can silently lose middle chunks on Android,
leaving a complete-looking but invalid JSON frame.

```text
INFO
CLAIM <64-hex-owner-key>
AUTH <64-hex-owner-key>
HELLO
GET CONFIG
GET LOGS
PATCH expected_revision=4&device_name=Wera&moving_sleep_s=45
ENTER_CONFIG_MODE
PAIR_GATEWAY <gateway-id>             # tracker
UNPAIR_GATEWAY                        # tracker
REGISTER_TRACKER device_id=...&device_name=...&lora_aead_key=... # gateway
ROLLBACK 5
FACTORY_RESET FACTORY_RESET
REBOOT
```

`INFO` is public. `CLAIM` is allowed only for an unclaimed device during the
attended setup window. Every other command except `AUTH` requires the current
BLE session to be authenticated. Disconnect clears session authentication.

`GET LOGS` returns the eight most recent gateway log lines over BLE, with
`count`, `total`, and `truncated` metadata. Limiting the response avoids a long
burst of acknowledged GATT indications blocking the gateway's polling LoRa
receiver. It is intended for diagnostics and does not persist logs or keep the
BLE connection busy with a continuous stream. Authenticated HTTP returns the
full bounded 25-line snapshot.

Provisioned gateways expose the same snapshot over authenticated HTTP:

```text
GET /api/v1/logs
Authorization: Bearer <64-hex-owner-key>
```

The legacy authenticated `GET /logs` endpoint remains available as plain text.

An incomplete tracker advertises this service without a deadline. Completing
both tracker configuration and gateway pairing returns it to the bounded BLE
window and normal low-power policy. An already configured gateway advertises
BLE for 60 seconds after a normal boot, or for the ten-minute authenticated
configuration window after an explicit request. It then releases Bluetooth to
prioritize station Wi-Fi reliability. Holding USER for five seconds after that
release performs a controlled restart into a fresh ten-minute BLE/configuration
window. The setup AP and OTA service remain independently time-bounded.

The key is a bearer secret on local HTTP and the custom BLE protocol. Operate
configuration networks in physical proximity/on a trusted LAN. QR exports and
PWA local storage must be protected accordingly.

## Transaction model

Configuration changes never mutate the live blob field-by-field. Firmware:

1. copies active configuration to a candidate;
2. checks `expected_revision`;
3. applies all fields;
4. finalizes CRC and revision + 1;
5. validates individual and cross-field constraints;
6. stores the previous active value in the backup slot;
7. atomically activates the candidate.

A rollback restores the validated backup as a new revision. A stale request
returns HTTP `409` or BLE `revision_conflict` and leaves active state unchanged.

Secret fields use these semantics:

- omitted, empty, or `__KEEP__`: retain;
- `__CLEAR__`: erase;
- any other value: replace.

Wi-Fi/MQTT passwords are never returned. The tracker LoRa AEAD key is returned
only through an owner-authorized configuration session because gateway
registration needs it.

## Role completion rules

A tracker is not operational until configuration is complete and at least one
gateway confirmation exists. Register the tracker on the gateway first, then
send `PAIR_GATEWAY`; this two-device transaction is idempotent and retryable.
The response identifies the gateway requested by that operation, while a
configuration read returns the complete confirmed-gateway list. Changing the
tracker ID or LoRa AEAD key clears that list and requires registration again.

A gateway is operational after validated Wi-Fi/MQTT/radio configuration. A
repeater is operational after validated radio/forwarding configuration and is
managed through Wi-Fi because it has no BLE service.

## OTA

ArduinoOTA starts only in configuration mode and uses the owner key through its
challenge/response mechanism. PlatformIO OTA environments read
`LORA_TRACKER_OWNER_KEY` and require `--upload-port <device-ip>`. OTA
authentication does not replace signed firmware/Secure Boot.

## Configuration field groups

Tracker fields cover identity, station Wi-Fi, LoRa AEAD/radio settings, GNSS
quality/timeouts, movement policy, batching/ACK/retry policy, history storage,
sleep intervals, battery sensing, and BLE debug enablement.

Gateway fields cover identity, station Wi-Fi, MQTT/TLS/CA settings, Germany
LoRa profile, retry intervals, dedup persistence, and up to twelve tracker
registry entries.

Repeater fields cover identity, Germany LoRa profile, hop cap, priority delay
and slots, duplicate-cache lifetime, continuous-hour airtime budget, and
heartbeat interval.
