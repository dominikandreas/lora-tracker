# Germany radio profile

This project currently supports one regulatory operating profile: non-specific
short-range devices in Germany, using **band 48 at 868.0–868.6 MHz**. The
firmware does not implement or claim a listen-before-talk/adaptive-frequency
agility mitigation technique, so it uses the alternative **1% duty-cycle**
condition.

The controlling German allocation is Bundesnetzagentur
[Allgemeinzuteilung Vfg. 91/2025](https://www.bundesnetzagentur.de/DE/Fachthemen/Telekommunikation/Frequenzen/Allgemeinzuteilungen/_DL/vfg91_2025.pdf),
published in November 2025 and currently limited through 31 December 2035.
Table 2, band 48 specifies:

- 868.0–868.6 MHz for non-specific SRD;
- no more than 25 mW ERP;
- an applicable access/mitigation technique, or alternatively no more than a
  1% duty cycle.

Vfg. 91/2025 defines duty cycle as the sum of transmitter on-times divided by a
**continuous one-hour observation period** in the applicable band. Tracker
history, gateway ACK and repeater forwarding transmissions all count. Receiving
does not count.

## Firmware enforcement

The tracker, gateway and repeater reject a configuration unless:

- the complete configured LoRa bandwidth fits inside 868.0–868.6 MHz;
- requested conducted transmitter power is 2–14 dBm;
- every transmitting role uses at most 36,000 ms in a rolling hour.

The shipped center frequency is 868.1 MHz with 125 kHz bandwidth. Airtime is
calculated with the Semtech LoRa time-on-air equation. The limiter starts empty
after a cold boot, survives tracker deep sleep in CRC-protected RTC state, and
uses a maximum-frame-sized bucket whose refill is reduced by that capacity.
This bounds an idle-time burst plus all refill during any hour to the legal
36-second ceiling. Gateway ACKs are deferred when no budget is available;
trackers retain their data and retry. Repeaters retain a due frame instead of
silently suppressing its later retry.

The repeater exposes a lower configurable airtime budget, but never accepts a
value above 36,000 ms/hour or below the airtime required for one maximum-size
frame at the selected radio profile.

## ERP, EIRP and antenna selection

The allocation states **25 mW ERP**, not conducted radio power. This is
14 dBm ERP, approximately **16.15 dBm EIRP**. Check the installed system, not
only the radio setting:

```text
EIRP dBm = conducted TX dBm - feed-line/connector loss dB + antenna gain dBi
ERP dBm  = EIRP dBm - 2.15 dB
```

The firmware cap of 14 dBm conducted power is conservative only when net
antenna gain is no more than 0 dBd, equivalent to about 2.15 dBi after cable and
connector losses. Reduce configured power for a higher-gain antenna. Verify the
actual radio output, antenna gain, feed-line loss and enclosure effects with
calibrated equipment; firmware configuration alone does not demonstrate ERP
compliance.

## Product compliance

Frequency allocation is only one part of placing or operating a product in
Germany. Vfg. 91/2025 requires compliant radio equipment under the German
Funkanlagengesetz. The relevant current harmonised radio standard for
non-specific equipment is
[ETSI EN 300 220-2 V3.3.1 (2025-03)](https://www.etsi.org/deliver/etsi_en/300200_300299/30022002/03.03.01_60/en_30022002v030301p.pdf).
A production device still needs an applicable RED/FuAG conformity assessment,
RF and EMC testing, safety review, technical file, declaration of conformity,
marking, and review of battery, environmental, privacy and installation
obligations. Obtain qualified compliance advice before sale or unattended
deployment.

Re-check the current Bundesnetzagentur allocation and harmonised standards
before every release; regulatory rules and cited standard versions can change.
