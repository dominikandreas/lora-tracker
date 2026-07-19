# LoRa Tracker firmware

This tracker implements transport v2 with AES-256-GCM-protected LoRa history
schema 2 and authenticated ACK schema 1.

Each stored route point now carries a GNSS UTC timestamp. On wire, the first
point uses a 32-bit Unix epoch and every later point uses an unsigned LEB128
seconds delta from the previous point. Normal tracking intervals add one byte
per point.

The tracker and gateway must use the same current protocol schema. Unsupported
packet and history schemas are deliberately not accepted.

## Display controls

Tap the user button to move through the status, GPS, radio and debug pages.
Hold for at least 0.9 seconds and release to run the action named on that page:
reset distance (with confirmation), acquire GPS for the displayed duration,
send queued history and wait for an ACK, or toggle BLE debug logs. Manual radio
sends remain constrained by the configured Germany airtime budget. See the
[onboarding guide](../../docs/ONBOARDING.md#tracker-button-controls) for the
complete interaction and recovery notes.

## PlatformIO

The included `platformio.ini` provides two pinned ESP32 Arduino builds:

- `heltec_wifi_lora_32_v2` for the BN-220/SX1276/OLED branch;
- `heltec_wireless_tracker` for the UC6580/SX1262/TFT branch.

Copy `secrets.example.h` to `secrets.h`, set only optional factory seed values, and
build from the repository root:

```bash
pio run -d components/tracker-firmware -e heltec_wifi_lora_32_v2
pio run -d components/tracker-firmware -e heltec_wireless_tracker
```

Use `-t upload` only after selecting the physical board and checking its port.
Tagged releases also contain merged browser-flashable images; see
[`docs/FLASHING.md`](../../docs/FLASHING.md).
The host simulator does not compile this sketch or exercise its peripherals;
see [`docs/SIMULATION_COVERAGE.md`](../../docs/SIMULATION_COVERAGE.md).
