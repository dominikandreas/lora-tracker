# LoRa Tracker — prototype 1.0

A complete first prototype for low-power LoRa tracking with offline route
buffering, Wi-Fi/MQTT gateways, recent-history archiving, device onboarding, and
a browser-based monitoring app.

## Current status

This release is suitable for continued development and controlled field tests.
It is **not production-secure yet**: LoRa telemetry and MQTT payloads are not
end-to-end encrypted or authenticated, MQTT publishing is QoS 0, and the
prototype onboarding access point uses a shared password unless overridden.
Read [SECURITY.md](SECURITY.md) before exposing any part of the system publicly.

## Components

| Component | Included version | Purpose |
|---|---:|---|
| Tracker firmware | timestamp v4 | GNSS tracking, adaptive sleep, compressed offline history, LoRa transport, BLE/Wi-Fi onboarding |
| Gateway firmware | MQTT v6 | Multi-tracker LoRa reception, ACKs, per-device deduplication, MQTT routing and management |
| Archiver | v2 | MQTT-to-SQLite storage, ten-day retention by default, deduplication and paginated history responses |
| Web app | v1 | Static PWA with MQTT-over-WebSocket, IndexedDB cache, tracker selection and route display |

## Major capabilities

- Adaptive GNSS acquisition and no-fix backoff
- Stationary/moving sleep policies and low-power LoRa initialization
- Offline history queue with spatial compression
- One absolute GNSS timestamp plus ULEB128 per-point time deltas
- Exponential LoRa retry backoff and ACK-based queue clearing
- Versioned LoRa envelope, configuration blobs, onboarding API and MQTT schemas
- Multiple trackers and gateways with stable point IDs
- Transactional configuration with CRC, revisions, backup and rollback
- BLE and Wi-Fi onboarding after factory reset
- MQTT event/state split and a standalone SQLite archiver
- Browser PWA with local history caching
- Compatibility with legacy and history-schema-v1 packets during migration

## Repository layout

```text
components/
  tracker-firmware/   Current tracker sketch and shared headers
  gateway-firmware/   Current multi-device gateway sketch and shared headers
  firmware-simulator/ Native C++ contract harness for shared firmware headers
  archiver/           Python/SQLite MQTT archiver and tests
  web-app/            Static progressive web application and tests
docs/
  protocols/          Wire, configuration, onboarding and MQTT specifications
  ARCHITECTURE.md
  BUILD_AND_DEPLOY.md
  COMPATIBILITY.md
  OPERATIONS.md
history/
  original-prototypes/  Input prototypes retained for traceability
  patches/              Milestone patches from power optimization through Step 6
ROADMAP.md             Prioritized future work and deferred Flutter application
SECURITY.md            Current threat model, limitations and crypto direction
CHANGELOG.md           Prototype development history
SHA256SUMS             Integrity hashes for all files in this bundle
```

## Recommended deployment order

1. Deploy or upgrade the archiver to v2.
2. Flash the gateway MQTT-v6 firmware.
3. Verify the gateway still accepts the old tracker format and returns ACKs.
4. Flash the tracker timestamp-v4 firmware.
5. Deploy the web application.
6. Confirm point events, latest state and history retrieval before relying on it
   for unattended tracking.

See [BUILD_AND_DEPLOY.md](docs/BUILD_AND_DEPLOY.md) for concrete setup steps.

## Versioning snapshot

- LoRa transport envelope: v1
- History message schemas: v1 and v2 accepted; tracker emits v2
- ACK schema: v1
- Persistent configuration: v1
- Onboarding/configuration API: v1
- MQTT topic API: v1
- MQTT point/history JSON schema: v2

## Documentation entry points

- [Architecture](docs/ARCHITECTURE.md)
- [Build and deployment](docs/BUILD_AND_DEPLOY.md)
- [Compatibility and migrations](docs/COMPATIBILITY.md)
- [Operations and troubleshooting](docs/OPERATIONS.md)
- [Security status](SECURITY.md)
- [Future roadmap](ROADMAP.md)
- [Protocol specifications](docs/protocols/)

## Validation boundary

The included Python and JavaScript tests can be run locally. The firmware was
previously checked with compile-oriented mocks, but this bundle does not pin the
exact Arduino core and library versions and has not been compiled against your
local hardware toolchain as part of packaging. Perform real board builds and
hardware field tests before deployment.

For a repeatable, brokerless integration check across the tracker, gateway,
archiver and PWA-facing MQTT/history boundary, run:

```text
cd components/archiver
python -m equine_archiver.simulator --trackers 2 --points 12 --service-suite --embedded-suite
```

See [simulation coverage](docs/SIMULATION_COVERAGE.md) for the interface cases
covered in software and the physical checks that require target hardware. The
embedded suite needs a host C++17 compiler and executes the shared C++ headers
from both firmware components; it is not a substitute for an ESP32 board build.
Pinned ESP32 PlatformIO environments are included with each firmware component;
open `equine-tracker-v2.code-workspace` to work with both projects together.
