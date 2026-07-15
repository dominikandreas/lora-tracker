# LoRa Tracker firmware

This tracker extends the onboarding/configuration firmware with LoRa history
schema v2.

Each stored route point now carries a GNSS UTC timestamp. On wire, the first
point uses a 32-bit Unix epoch and every later point uses an unsigned LEB128
seconds delta from the previous point. Normal tracking intervals add one byte
per point.

The tracker and gateway must use the same current protocol schema. Unsupported
packet and history schemas are deliberately not accepted.

## PlatformIO

The included `platformio.ini` provides two pinned ESP32 Arduino builds:

- `heltec_wifi_lora_32_v2` for the BN-220/SX1276/OLED branch;
- `heltec_wireless_tracker` for the UC6580/SX1262/TFT branch.

Copy `secrets.example.h` to `secrets.h`, set only your factory seed values, and
build from the repository root:

```bash
pio run -d components/tracker-firmware -e heltec_wifi_lora_32_v2
pio run -d components/tracker-firmware -e heltec_wireless_tracker
```

Use `-t upload` only after selecting the physical board and checking its port.
The host simulator does not compile this sketch or exercise its peripherals;
see [`docs/SIMULATION_COVERAGE.md`](../../docs/SIMULATION_COVERAGE.md).
