# Hardware recommendations

## Development and field-trial hardware

The current tracker firmware directly supports two development configurations:

| Role | Recommended board | Why | Status |
|---|---|---|---|
| Tracker | Heltec Wireless Tracker | Integrated ESP32-S3, SX1262 and UC6580 GNSS; closest match to the current low-power tracker branch | Supported PlatformIO target |
| Gateway for existing prototypes | Heltec WiFi LoRa 32 V2 | Matches the checked-in SX1276 pin map | Supported, but not recommended for a new design |
| Gateway for new hardware work | Heltec WiFi LoRa 32 V3/V4 | ESP32-S3 and SX1262; a better common radio platform | Firmware port is a P0 TODO |
| Repeater | Heltec WiFi LoRa 32 V2 or Wireless Tracker | Reuses either supported radio path; tracker GNSS/display are unused | Supported PlatformIO targets |

The [Wireless Tracker documentation](https://docs.heltec.org/en/node/esp32/wireless_tracker/index.html)
identifies its ESP32-S3, SX1262 and integrated GNSS. The current
[WiFi LoRa 32 documentation](https://docs.heltec.org/en/node/esp32/wifi_lora_32/index.html)
describes the ESP32-S3/SX1262 V3 and V4 generations. Do not flash the V2 target
onto V3/V4: radio type and GPIO mappings differ.

Choose the radio variant and antenna for the legal band at the deployment
location. The checked-in configuration currently accepts only 863–870 MHz and
must be treated as an EU-region build. Supporting another region requires a
deliberate firmware validation-range change, a matching radio/antenna variant,
and a new regional compliance review; changing only the frequency is not enough.

### Checked-in pin assignments

| Board | GNSS UART/control | LoRa SPI/control | Battery/display |
|---|---|---|---|
| Wireless Tracker | RX 33, TX 34, reset 35, Vext 3, GNSS power 37, 115200 baud | SCK 9, MISO 11, MOSI 10, NSS 8, reset 12, busy 13, DIO1 14 | battery ADC 1, divider enable 2; TFT pins are in `platformio.ini` |
| WiFi LoRa 32 V2 tracker | RX 33, TX 32, 9600 baud | SCK 5, MISO 19, MOSI 27, NSS 18, reset 14, DIO0 26 | battery ADC 36; OLED SCL 15, SDA 4, reset 16 |
| WiFi LoRa 32 V2 gateway | no GNSS | SCK 5, MISO 19, MOSI 27, NSS 18, reset 14, DIO0 26 | USER button GPIO 0 |
| WiFi LoRa 32 V2 repeater | no GNSS | SCK 5, MISO 19, MOSI 27, NSS 18, reset 14, DIO0 26 | USER button GPIO 0 |
| Wireless Tracker repeater | GNSS disabled | SCK 9, MISO 11, MOSI 10, NSS 8, reset 12, busy 13, DIO1 14 | USER button GPIO 0 |

GPIO 0 is a boot-strapping pin. Do not hold it while applying power or reset;
wait for the firmware prompt before using a setup gesture.

## Observed 1000 mAh prototype runtime

This is an early field observation, not a battery specification. With a 1000 mAh
LiPo, BLE off, adaptive batching, a 60-second moving interval, poor GNSS
conditions and normal LoRa connectivity, the reported state of charge moved from
82% to 76% over 12 hours. That is approximately 0.5% per hour, 12% per day and
an eight-day linear extrapolation. Voltage-derived percentages can be noisy, so
controlled coulomb-counted tests across temperature and activity profiles are
still required.

| Operating condition | Preliminary estimate |
|---|---:|
| Current live tracking (observed conditions) | 7–9 days |
| Excellent GNSS conditions | 10–14 days |
| Very poor GNSS or frequent retries | 5–7 days |

Longer intervals are exploratory projections from the observation, not measured
claims: about 30–45 days at five minutes, 2–3 months at ten minutes, 5–8 months
at 30 minutes and 9–12 months at one hour. At intervals of four hours and above,
cell self-discharge, aging, temperature and quiescent current dominate, so
multi-year arithmetic is not a practical runtime promise.

A low-power wake-on-motion accelerometer remains the most promising next hardware
change: it could keep the tracker asleep while stationary and wake it promptly
on movement. Validate any expected 2×–5× improvement with measured animal-use
duty cycles before treating it as a product requirement.

## Production tracker direction

A custom tracker should be based on measured prototype results, not simply copy
a development board. Recommended building blocks are:

- ESP32-S3 while the current firmware depends on its Arduino, Wi-Fi and BLE stack.
- [Semtech SX1262](https://www.semtech.com/products/wireless-rf/lora-connect/sx1262)
  or a certified module using it, with a region-specific matched antenna.
- [u-blox MAX-M10 series](https://www.u-blox.com/en/product/max-m10-series)
  GNSS for a future low-power custom design; it is intended for battery asset
  tracking. A firmware driver and antenna evaluation are required.
- [Bosch BMA400](https://www.bosch-sensortec.com/en/products/motion-sensors/accelerometers/bma400)
  for wake-on-motion; it provides low-power activity detection and interrupt pins.
- [MAX17048](https://www.analog.com/en/products/max17048.html) for single-cell
  state-of-charge estimation; voltage-only percentages are not reliable across load and temperature.
- A protected, traceable single-cell Li-ion/LiPo pack, charger with temperature
  qualification, load sharing, undervoltage protection and an accessible service disconnect.
- Separate RF and GNSS antenna layouts with controlled ground clearance and no
  conductive enclosure or body placement directly over the radiators.

Do not select a secure element until the key hierarchy and provisioning line are
designed. Component lifecycle matters: for example Microchip currently marks
ATECC608B as not recommended for new designs. ESP32-S3 Secure Boot v2 and flash
encryption are still required even if a secure element is added.

## Mechanical and animal-safety requirements

- Use a rounded, snag-resistant, waterproof enclosure with strain relief and no exposed sharp edges.
- Mount so the tracker can break away under a hazardous load; never create a rigid entanglement point.
- Keep weight and pressure distributed and validate fit under movement, sweat, rain and mud.
- Evaluate RF performance on the actual mounting location because the body and wet materials attenuate UHF signals.
- Provide a non-destructive charging method or a sealed replaceable pack; inspect swelling and impact damage.
- Treat IP ratings, battery certification, EMC, radio approval and mechanical safety as product requirements, not enclosure marketing claims.

## Gateway direction

For a single private site, an ESP32-S3/SX1262 gateway with reliable mains power
and a wired backhaul option can be sufficient. For wide-area or high-device-count
coverage, evaluate a standards-based multi-channel LoRaWAN concentrator gateway
instead of scaling this single-channel custom protocol. That choice requires a
protocol/backend redesign and is not wire-compatible with the current firmware.

Use a UPS, watchdog, protected power input and an enclosure appropriate to the
installation. Ethernet is preferable to Wi-Fi where it is available. A gateway
must have local durable storage before it can claim loss-tolerant MQTT delivery.

## Repeater direction

A repeater is an always-on receiver and is not suitable for the tracker's small
1000 mAh battery/runtime assumptions. Use protected mains/UPS power or a solar
system sized from measured receive, transmit, Wi-Fi setup and cold-weather
loads. Mount a region-correct vertical antenna high and clear, use a sealed
enclosure and surge/lightning protection appropriate to the site, and document
conducted power plus antenna gain. See [repeaters](REPEATERS.md).

## Qualification before hardware freeze

Measure, at minimum, deep sleep, GNSS cold/warm acquisition, LoRa TX at every
configured power, Wi-Fi/BLE onboarding and failure/retry current. Then run
battery tests at temperature, RF range tests with the worn orientation,
accelerated ingress/impact tests and multi-day queue/reconnect tests. Size the
battery from measured duty-cycle distributions with aging and cold-temperature
margin; a nominal capacity calculation is not enough.
