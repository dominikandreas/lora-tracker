# LoRa Tracker

LoRa Tracker is a low-power GNSS tracking system with battery-powered tracker
firmware, a Wi-Fi/MQTT gateway, a SQLite history service, a browser PWA and a
deterministic cross-component simulator.

## Release status

The repository is suitable for development and controlled field trials, but
is not approved for unattended production deployment. LoRa telemetry/ACKs now
use per-tracker AES-256-GCM and tracker BLE configuration requires an encrypted,
authenticated session. The remaining release blockers include signed firmware
with ESP32 secure boot, durable gateway delivery, key custody/rotation and
hardware-in-the-loop qualification. See [production readiness](docs/PRODUCTION_READINESS.md)
and [security](SECURITY.md).

## Components

| Component | Purpose |
|---|---|
| `components/tracker-firmware` | GNSS acquisition, motion-aware sleep, offline history and LoRa transport |
| `components/gateway-firmware` | Multi-tracker LoRa reception, ACKs, deduplication and MQTT routing |
| `components/archiver` | Validated MQTT ingestion, SQLite retention and paginated history |
| `components/web-app` | Installable MQTT-over-WebSocket monitoring PWA |
| `components/firmware-simulator` | Native contract tests for shared embedded headers |

Only the current protocol and JSON schemas are accepted. Older packet, history,
point and request schemas are deliberately rejected.

## Quick start

Run the brokerless system and embedded contract simulation:

```bash
cd components/archiver
python -m lora_tracker_archiver.simulator \
  --trackers 2 --points 12 --service-suite --embedded-suite
```

Build every firmware target with the pinned PlatformIO toolchain:

```bash
pio run -d components/tracker-firmware -e heltec_wifi_lora_32_v2
pio run -d components/tracker-firmware -e heltec_wireless_tracker
pio run -d components/gateway-firmware -e heltec_wifi_lora_32_v2
```

Copy each firmware component's `secrets.example.h` to the git-ignored
`secrets.h` first. Generic builds generate their admin credential on erased
first boot; a factory seed and OTA password hash are optional. Provision the
gateway broker's PEM root CA through the authenticated setup API or a per-device
factory build. Plain MQTT is disabled by default.

## Documentation

- [Architecture](docs/ARCHITECTURE.md)
- [Onboarding and configuration](docs/ONBOARDING.md)
- [Configuration reference](docs/CONFIGURATION_REFERENCE.md)
- [Build and deployment](docs/BUILD_AND_DEPLOY.md)
- [Browser flashing](docs/FLASHING.md)
- [Hardware recommendations](docs/HARDWARE.md)
- [Operations](docs/OPERATIONS.md)
- [Simulation coverage](docs/SIMULATION_COVERAGE.md)
- [Production readiness](docs/PRODUCTION_READINESS.md)
- [Protocol specifications](docs/protocols/README.md)
- [Roadmap and larger refactors](ROADMAP.md)

The simulator validates cross-component contracts, both copies of the embedded
headers, storage, MQTT callbacks and browser payload normalization. It does not
simulate RF, GNSS, power, flash failure, real brokers or browsers. Those limits
are listed in [simulation coverage](docs/SIMULATION_COVERAGE.md).

## Default namespace

New installations publish below `lora-tracker/v1`. Device IDs are canonical
lowercase identifiers; routing uses the derived 64-bit public device hash. The
hash is not a secret or an authentication credential.
