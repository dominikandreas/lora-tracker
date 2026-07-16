# LoRa Tracker

LoRa Tracker is a low-power GNSS tracking system with battery-powered tracker
firmware, a Wi-Fi/MQTT gateway, a SQLite history service, a browser PWA and a
deterministic cross-component simulator. Optional keyless repeaters extend
encrypted history traffic and receiver ACKs across bounded multi-hop paths.

## Release status

The repository is suitable for development and controlled field trials, but
is not approved for unattended production deployment. LoRa telemetry/ACKs now
use per-tracker AES-256-GCM and tracker BLE configuration requires an encrypted,
authenticated session. Gateway ACKs now require a durable archiver receipt,
and all transmitters enforce the supported Germany radio profile. Remaining
release blockers include signed firmware with ESP32 secure boot, a power-loss-safe
tracker queue, key custody/rotation and hardware-in-the-loop qualification. See [production readiness](https://dominikandreas.github.io/lora-tracker-docs/reference/production-readiness.html)
and [security](https://dominikandreas.github.io/lora-tracker-docs/reference/security.html).

## Components

| Component | Purpose |
|---|---|
| `components/tracker-firmware` | GNSS acquisition, motion-aware sleep, offline history and LoRa transport |
| `components/gateway-firmware` | Multi-tracker LoRa reception, ACKs, deduplication and MQTT routing |
| `components/repeater-firmware` | Keyless bounded forwarding of encrypted history and ACK frames |
| `components/archiver` | Validated MQTT ingestion, SQLite retention and paginated history |
| `components/web-app` | Installable MQTT-over-WebSocket monitoring PWA |
| `components/firmware-core` | Portable C++ policies shared by firmware and WebAssembly |
| `components/firmware-simulator` | Native embedded contract tests |
| `components/simulator-web` | Interactive deterministic WASM network lab |

Only the current protocol and JSON schemas are accepted. Older packet, history,
point and request schemas are deliberately rejected.

## Quick start

Run the brokerless system and embedded contract simulation:

```bash
cd components/archiver
python -m lora_tracker_archiver.simulator \
  --trackers 2 --points 12 --service-suite --embedded-suite
```

Build and serve the interactive WASM Network Lab:

```bash
cd components/simulator-web
make wasm
npm ci && npm test
python3 -m http.server 8080 -d app
```

Build every firmware target with the pinned PlatformIO toolchain:

```bash
pio run -d components/tracker-firmware -e heltec_wifi_lora_32_v2
pio run -d components/tracker-firmware -e heltec_wireless_tracker
pio run -d components/gateway-firmware -e heltec_wifi_lora_32_v2
pio run -d components/repeater-firmware -e heltec_wifi_lora_32_v2
pio run -d components/repeater-firmware -e heltec_wireless_tracker
```

Copy `secrets.example.h` to git-ignored `secrets.h` for tracker and gateway
source builds; the repeater has no factory secret header. Generic builds generate their admin credential on erased
first boot; a factory seed and OTA password hash are optional. Provision the
gateway broker's PEM root CA through the authenticated setup API or a per-device
factory build. Plain MQTT is disabled by default.

## Documentation

The complete reader-facing documentation is rendered on [GitHub Pages](https://dominikandreas.github.io/lora-tracker-docs/).

- [Architecture](https://dominikandreas.github.io/lora-tracker-docs/reference/architecture.html)
- [Onboarding and configuration](https://dominikandreas.github.io/lora-tracker-docs/reference/onboarding.html)
- [Configuration reference](https://dominikandreas.github.io/lora-tracker-docs/reference/configuration.html)
- [Build and deployment](https://dominikandreas.github.io/lora-tracker-docs/reference/build-and-deploy.html)
- [Browser flashing](https://dominikandreas.github.io/lora-tracker-docs/reference/flashing.html)
- [Hardware recommendations](https://dominikandreas.github.io/lora-tracker-docs/reference/hardware.html)
- [Germany radio compliance](https://dominikandreas.github.io/lora-tracker-docs/reference/radio-compliance-de.html)
- [Repeaters](https://dominikandreas.github.io/lora-tracker-docs/reference/repeaters.html)
- [Operations](https://dominikandreas.github.io/lora-tracker-docs/reference/operations.html)
- [Simulation coverage](https://dominikandreas.github.io/lora-tracker-docs/reference/simulation-coverage.html)
- [Production readiness](https://dominikandreas.github.io/lora-tracker-docs/reference/production-readiness.html)
- [Protocol specifications](https://dominikandreas.github.io/lora-tracker-docs/protocols.html)
- [Roadmap and larger refactors](https://dominikandreas.github.io/lora-tracker-docs/reference/roadmap.html)

The browser [Network Lab](https://dominikandreas.github.io/lora-tracker-docs/simulator/)
uses the production C++ policy core compiled to WebAssembly. It visualizes
trackers, repeaters, receivers, obstacles, RF link budgets, collisions,
day/night conditions, MQTT archival and archive-backed ACK paths. The separate
brokerless suite continues to execute production archiver and embedded
contracts. Both are engineering evidence rather than RF, power, infrastructure
or regulatory qualification; see [simulation coverage](https://dominikandreas.github.io/lora-tracker-docs/reference/simulation-coverage.html).

## Default namespace

New installations publish below `lora-tracker/v1`. Device IDs are canonical
lowercase identifiers; routing uses the derived 64-bit public device hash. The
hash is not a secret or an authentication credential.
