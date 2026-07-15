# Configuration reference

The definitive field names and transaction semantics are in
[`protocols/ONBOARDING_API.md`](protocols/ONBOARDING_API.md). This page explains
the shipped defaults and the operational tradeoffs behind them.

## Identity and radio

`device_id` and `gateway_id` are stable lowercase identifiers used to derive
public routing hashes. Changing either creates a new MQTT identity and loses the
association with existing deduplication/history state. Names are display-only.

The factory radio profile is 868.0 MHz, 125 kHz bandwidth, spreading factor 10,
coding rate 4/5, preamble 8, sync word `0x12`, and 20 dBm requested TX power.
This is a development profile, not a declaration of legal operation. Set the
frequency, conducted power, antenna gain and airtime policy for the applicable
region and hardware. Tracker and gateway values must match exactly.

## Tracker defaults

| Policy | Default | Effect |
|---|---:|---|
| LoRa batch | 3 points or 300 s | Fewer transmissions versus delivery latency |
| ACK timeout | 800 ms | Time allowed for the gateway response |
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
| RTC history capacity | 448 points | Fits with retained state in 8 KiB RTC slow memory |

These values came from the earlier field prototype and are starting points,
not validated universal settings. Tune only after collecting fix quality,
missed movement, airtime and measured energy data together.

## Gateway defaults

- Verified MQTT TLS enabled, port 8883 and namespace `lora-tracker`.
- MQTT packet buffer 1024 bytes.
- Wi-Fi retry 10 seconds and MQTT retry 5 seconds.
- Deduplication cursor checkpoint after 10 updates.
- Empty tracker allowlist on first boot; every tracker must be registered.

The PEM broker root CA and plaintext-test opt-in remain build-time secrets.
Broker host, port, credentials, namespace and registry are transactional runtime
configuration. A TLS setting without a valid CA fails closed.

## Safe change procedure

Change one policy group at a time, record the old revision and export non-secret
configuration, then verify at least one complete fix/transmit/ACK/archive cycle.
Radio changes require coordinated tracker and gateway maintenance. Identity
changes also require broker ACL, archiver allowlist and application updates.
