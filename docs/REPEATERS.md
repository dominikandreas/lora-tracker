# Repeaters

The repeater extends the private single-channel LoRa link in both directions:
tracker `HISTORY` frames travel toward any receiver, and authenticated receiver
`ACK` frames travel back toward the tracker. It is not a LoRaWAN repeater and
does not interoperate with LoRaWAN gateways.

## Design

Every radio packet starts with a six-byte link header containing a magic value,
link version, current hop count, origin-selected hop limit and reserved flags.
The existing AES-256-GCM frame follows unchanged. A repeater can validate the
public secure-frame header and increment the link hop, but it has no tracker
key and cannot read or alter coordinates, timestamps, battery state or ACK
content. The tracker and receiver still authenticate end to end.

Forwarding is bounded and opportunistic:

- the origin sets a hop limit from zero to four; every repeater also applies
  its own local cap, and the lower value wins;
- a frame identity is the message type, schema, device hash, boot ID and secure
  counter; recent identities are not forwarded twice;
- each repeater derives a stable priority slot from the frame identity and its
  own ID, then waits a short deterministic delay;
- hearing a peer forward the same frame at the planned hop cancels the pending
  copy, reducing duplicate airtime while allowing a more distant repeater that
  heard only the new hop to continue the chain;
- the queue holds at most eight full packets; overload drops new work rather
  than exhausting memory or blocking radio reception;
- a token bucket charges the estimated and measured transmit airtime. The
  shipped ceiling is 36,000 ms per hour (one percent), but the legal limit must
  be configured for the region, sub-band, EIRP and installation.

ACK identities use a five-second duplicate cache instead of the ordinary
120-second cache. This suppresses an ACK flood but permits the receiver's ACK
to be forwarded again after a later tracker retry. The tracker keeps listening
through echoed history and unrelated radio frames until the ACK deadline.

## Supported hardware and build

The checked-in firmware supports:

- Heltec WiFi LoRa 32 V2 with SX1276;
- Heltec Wireless Tracker with ESP32-S3/SX1262 (GNSS and display are unused).

```bash
pio run -d components/repeater-firmware -e heltec_wifi_lora_32_v2
pio run -d components/repeater-firmware -e heltec_wireless_tracker
```

A repeater is an always-listening infrastructure device. Prefer protected mains
power with a UPS, or a properly sized solar system, rather than a small tracker
battery. Use a weatherproof enclosure, lightning/surge protection where
applicable, strain relief and a correctly matched vertical antenna mounted
clear of metal, wet foliage, people and animals.

## Onboarding

On erased flash, the repeater derives a unique ID, generates and persists a
20-character administrator password, and starts
`lora-repeater-<suffix>`. Serial output at 115200 baud shows the AP address and
credential. Authenticate as `admin`, configure the radio and forwarding
policy, save, and let the device reboot. Repeating remains disabled until a
valid configuration has been saved.

To reopen configuration, hold USER for at least 1.5 seconds during boot. A
configured device closes the AP after ten minutes. The AP is WPA2 protected and
the HTTP interface also uses Basic authentication; configure it only during an
attended maintenance session and power-cycle afterward.

Radio frequency, bandwidth, spreading factor, coding rate, preamble and sync
word must exactly match every tracker and receiver in the radio cell. The
tracker and receiver `lora_relay_hop_limit` values originate the forward and
reverse limits. The repeater hop value is only a local maximum.

## Timing and topology

The default tracker ACK window is 15 seconds for up to two repeater hops at the
default SF10/125 kHz profile. Larger packets, higher spreading factors, MQTT
processing and four-hop paths can require up to the configurable 30-second
maximum. Measure the worst observed round trip before shortening the window.
Longer windows keep the tracker radio awake and reduce battery life.

Place one repeater where it has reliable margin to both sides of a weak link.
Use the smallest hop limit that covers the site. Adding overlapping repeaters
can improve resilience, but hidden nodes that cannot hear one another can still
transmit duplicate copies or collide at a receiver. Deterministic jitter and
suppression reduce that risk; they cannot eliminate RF collisions.

## Acceptance test

Before field use:

1. Verify identical radio settings and a unique repeater ID.
2. With the repeater off, confirm the intended tracker/receiver link fails or
   has insufficient margin; with it on, confirm history delivery and queue
   clearing through an authenticated ACK.
3. Observe serial counters for forwarded, suppressed, invalid, queue-dropped
   and airtime-dropped frames under simultaneous tracker traffic.
4. Test every intended hop, then remove one repeater at a time to document the
   actual fault tolerance.
5. Measure duty cycle, packet error rate and tracker energy at the worst radio
   profile and traffic load.
6. Repeat with the installed antennas, enclosures, power supplies, mounting
   height, weather and vegetation.

The software simulator covers frame parsing, hop bounds, deterministic delay,
suppression policy, ACK cache lifetime, time-on-air calculation and all three
embedded header copies. It does not model propagation, capture effect, hidden
nodes, regulatory compliance or real radio timing; those remain hardware tests.
