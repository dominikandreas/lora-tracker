# Flash firmware from a browser

Release tags build merged ESP32 images that can be installed over USB with
[ESP Web Tools](https://esphome.github.io/esp-web-tools/). The flasher uses the
browser's Web Serial API; use a current desktop Chrome or Edge browser, serve
the page over HTTPS, and close PlatformIO serial monitors before connecting.
Firefox and Safari support varies, and iOS cannot expose Web Serial.

## Choose the exact image

Never select an image only because its connector fits.

| Hardware and role | Release target | Chip family |
|---|---|---|
| Heltec WiFi LoRa 32 V2 tracker with external BN-220 | `tracker-v2` | ESP32 |
| Heltec Wireless Tracker with UC6580/SX1262/TFT | `tracker-wireless-tracker` | ESP32-S3 |
| Heltec WiFi LoRa 32 V2 gateway | `gateway-v2` | ESP32 |
| Heltec WiFi LoRa 32 V2 repeater | `repeater-heltec-lora32` | ESP32 |
| Heltec Wireless Tracker used as repeater | `repeater-wireless-tracker` | ESP32-S3 |

The V2 tracker and gateway use the same ESP32 board but different firmware.
ESP Web Tools can detect the chip family, not the role, so this choice remains
the operator's responsibility.

## Install

1. Open the LoRa Tracker documentation site's **Web flasher**, or download the
   matching `.manifest.json` and `.factory.bin` assets from a GitHub release
   and serve a local ESP Web Tools install page.
2. Disconnect the battery and peripherals that could back-power the board.
   Attach one board with a known data-capable USB cable.
3. Select the exact target above, choose **Connect**, and select its serial port.
4. For a new device, a role change, or recovery, choose the erase option. Erase
   deliberately deletes configuration, radio keys, retained history and Wi-Fi
   credentials. For an ordinary same-role update, keep data unless the release
   notes explicitly require an erase.
5. Start the install and do not unplug the board until verification and reset
   complete. If automatic bootloader entry fails, hold BOOT, tap RESET, release
   BOOT after the connection starts, and retry.
6. Open the LoRa Tracker app and claim a nearby tracker or gateway over the
   custom BLE service. Do not pair it in Android Bluetooth settings: there is no
   PIN or operating-system bond. The app generates and stores the owner key.
7. Enter Wi-Fi and role configuration in the app. A repeater currently uses its
   open, attended setup AP (`lora-repeater-<suffix>`): connect to it, add
   `192.168.4.1` in the app, and claim it there. Complete
   [onboarding](ONBOARDING.md) before leaving setup mode.
8. For a tracker, first finish gateway Wi-Fi/MQTT configuration and connect the
   app to the same MQTT profile. Then configure the tracker and use **Register
   and finish pairing**. The pairing panel shows each stage and uses encrypted
   MQTT when the gateway is remote, falling back to LAN/mDNS or nearby BLE. A
   gateway's setup-AP address is not its normal management address.

The official [esptool-js project](https://github.com/espressif/esptool-js)
documents the underlying Web Serial transport. ESP Web Tools requires a merged
ESP32 image; the release workflow creates it from the bootloader, partition
table, boot application and firmware at the PlatformIO-defined offsets.

## Verify a release

Every release contains `SHA256SUMS`, app binaries, merged factory binaries,
ELF files for crash decoding and ESP Web Tools manifests. Verify a downloaded
asset before manual flashing:

```bash
sha256sum --check SHA256SUMS
```

Tagged builds also receive a GitHub build-provenance attestation. With GitHub
CLI installed, verify an asset against this repository:

```bash
gh attestation verify lora-tracker-tracker-wireless-tracker-*.factory.bin \
  --repo dominikandreas/lora-tracker
```

Checksums and provenance show which source produced a binary; they do not make
the currently unsigned boot process trustworthy. Secure Boot v2, flash/NVS
encryption and signed OTA remain production requirements.

## Command-line fallback

PlatformIO remains the supported recovery path. Copy `secrets.example.h` to
`secrets.h`, build the correct environment, identify the port, then append
`-t upload`. Do not use an `.app.bin` as a factory image: it belongs at the app
partition offset and omits the bootloader and partition table.
