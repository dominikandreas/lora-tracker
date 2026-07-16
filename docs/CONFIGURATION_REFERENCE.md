# Configuration reference

The definitive field names and transaction semantics are in
[`protocols/ONBOARDING_API.md`](protocols/ONBOARDING_API.md). This page explains
the shipped defaults and the operational tradeoffs behind them.

## Identity and radio

`device_id` and `gateway_id` are stable lowercase identifiers used to derive
public routing hashes. Changing either creates a new MQTT identity and loses the
association with existing deduplication/history state. Names are display-only.

`lora_aead_key` is a 32-byte per-tracker secret represented as exactly 64 hex
characters. The tracker generates one on erased first boot. The matching
gateway registry entry must contain the same key; zero, missing and malformed
keys fail configuration validation. A key change requires an attended
tracker/gateway maintenance window and creates a new cryptographic trust state.

The factory radio profile is 868.1 MHz, 125 kHz bandwidth, spreading factor 10,
coding rate 4/5, preamble 8, sync word `0x12`, and 14 dBm requested conducted
TX power. Firmware currently accepts only the Germany band-48 profile: the
complete channel must fit within 868.0–868.6 MHz, conducted power is capped at
14 dBm, and every transmitting role enforces at most 36 seconds per rolling
hour. Installed ERP still depends on antenna gain and feed-line loss. See the
[Germany radio profile](RADIO_COMPLIANCE_DE.md) before configuring hardware.
Tracker, gateway and repeater radio values must match exactly.

This policy is configuration schema 5. Upgrading from an earlier schema rejects
the old blob and requires attended re-onboarding; values are not silently
migrated into the Germany profile.
`lora_relay_hop_limit` is zero to four on trackers and gateways and one to four
on repeaters. The shipped origin limit is two.

## Tracker defaults

| Policy | Default | Effect |
|---|---:|---|
| LoRa batch | 3 points or 300 s | Fewer transmissions versus delivery latency |
| Relay hop limit | 2 | Maximum forward and reverse repeater hops originated by the endpoint |
| ACK timeout | 15,000 ms | Allows a two-hop data/ACK round trip at the default radio profile |
| Retry delays | 60, 120, 300, 600 s | Battery protection while out of coverage |
| GNSS quality | HDOP ≤ 2.0, ≥ 6 satellites | Rejects weak fixes |
| Maximum plausible speed | 20 m/s | Rejects teleport-like samples |
| Movement | >1.0 km/h or >10 m | Immediate movement evidence |
| Small-move evidence | 2 consistent observations, >4.5 m, step >2 m, direction within 60° | Suppresses stationary GNSS jitter |
| Stored route spacing | 15 m | Limits queue growth on dense tracks |
| Moving sleep | 60 s | Nominal active tracking interval |
| Stationary sleep | 300 s after 3 stationary fixes | Saves energy when stopped |
| Long stationary sleep | 600 s after 12 stationary fixes | Further stationary saving |
| No-fix acquisition | 30, 20, 10, 8 s | Progressively reduces GNSS effort |
| No-fix sleep | 120, 300, 600, 900 s | Progressively extends retry interval |
| Forced full GNSS attempt | 3600 s | Periodic recovery from quick-probe failures |
| RTC history capacity | 448 points | Fits with retained state plus metadata/CRC in 8 KiB RTC slow memory |

These values came from the earlier field prototype and are starting points,
not validated universal settings. Tune only after collecting fix quality,
missed movement, airtime and measured energy data together.

## Gateway defaults

- Verified MQTT TLS enabled, port 8883 and namespace `lora-tracker`.
- MQTT packet buffer 1024 bytes.
- Wi-Fi retry 10 seconds and MQTT retry 5 seconds.
- Deduplication cursor checkpoint after 10 updates.
- Empty tracker allowlist on first boot; every tracker must be registered.

The PEM broker root CA, broker host, port, credentials, namespace and registry
are transactional runtime configuration. The read API exposes only
`ca_certificate_set`; updates use `mqtt_ca_certificate` with a PEM value or
`__KEEP__`. `secrets.h` can seed a per-device factory certificate. The
plaintext-test opt-in remains build-time only. TLS without a valid CA or a
trusted NTP-synchronized clock fails closed.

## Repeater defaults

- Unique MAC-derived ID and generated 20-character administrator credential.
- Two-hop local forwarding cap; 40 ms base delay and eight deterministic 45 ms priority slots.
- Eight queued frames and a 120-second history duplicate cache; ACKs use five seconds.
- Rolling-hour 36,000 ms airtime ceiling with fail-safe cold start and 14 dBm maximum requested conducted TX power.
- Repeating disabled until the first valid configuration is saved.

The 15-second tracker ACK window is an energy/coverage tradeoff, not a universal
constant. Measure and increase it for higher spreading factors or longer paths;
reduce it only after verifying worst-case end-to-end timing. See
[repeaters](REPEATERS.md).

## Safe change procedure

Change one policy group at a time, record the old revision and export non-secret
configuration, then verify at least one complete fix/transmit/ACK/archive cycle.
Radio changes require coordinated tracker and gateway maintenance. Identity
changes also require broker ACL, archiver allowlist and application updates.
