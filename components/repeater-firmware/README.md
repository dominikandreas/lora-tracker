# Repeater firmware

This firmware turns a supported Heltec ESP32 LoRa board into a keyless,
always-listening repeater for tracker history frames and receiver ACKs. It
forwards the encrypted frame unchanged inside a small mutable link header.

Build both supported targets:

```text
pio run -d components/repeater-firmware -e heltec_wifi_lora_32_v2
pio run -d components/repeater-firmware -e heltec_wireless_tracker
```

On first boot the repeater starts an open, temporary configuration AP and
prints its name and URL to serial at 115200 baud. The app claims the device by
installing a random owner key; no PIN, password or operating-system Bluetooth
bond is used. To reopen configuration later, hold USER for at least 1.5 seconds
during boot. Radio settings must exactly match the trackers and gateways.

The default two-hop cap, deterministic priority slots, post-success duplicate
cache, bounded eight-frame queue and rolling-hour airtime limiter are safety
mechanisms. This build accepts only the Germany band-48 profile; installed ERP
and product compliance still require hardware verification. See
[`docs/RADIO_COMPLIANCE_DE.md`](../../docs/RADIO_COMPLIANCE_DE.md).
