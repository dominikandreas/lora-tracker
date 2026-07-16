# Repeater firmware

This firmware turns a supported Heltec ESP32 LoRa board into a keyless,
always-listening repeater for tracker history frames and receiver ACKs. It
forwards the encrypted frame unchanged inside a small mutable link header.

Build both supported targets:

```text
pio run -d components/repeater-firmware -e heltec_wifi_lora_32_v2
pio run -d components/repeater-firmware -e heltec_wireless_tracker
```

On first boot the repeater starts a WPA2 configuration AP and prints the AP
name, URL and generated administrator password to serial at 115200 baud. To
reopen configuration later, hold USER for at least 1.5 seconds during boot.
Radio settings must exactly match the trackers and gateways.

The default two-hop cap, deterministic priority slots, duplicate cache, bounded
eight-frame queue and one-percent airtime token bucket are safety mechanisms,
not a substitute for checking the legal frequency, EIRP and duty-cycle rules
for the deployment country and antenna.
