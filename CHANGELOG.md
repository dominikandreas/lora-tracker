# Changelog

## 1.0.0-prototype — 2026-07-15

First consolidated prototype release.

### Prototype stabilization

- Reduced normal power consumption through adaptive GNSS acquisition, deep
  sleep, lazy display/radio initialization and cached battery reads.
- Added stationary and no-fix backoff policies.
- Added LoRa batching, ACK handling and exponential retry backoff.
- Corrected BLE toggle lifecycle deadlocks and display-button state handling.
- Separated GNSS movement classification from stale speed values and added
  displacement-derived speed.

### Protocol and multi-node foundation

- Added versioned LoRa transport envelope and public device routing hash.
- Added multi-tracker gateway registry and per-device deduplication.
- Added stable point IDs suitable for several gateways.
- Added legacy rolling-upgrade support.

### Configuration and onboarding

- Added CRC-protected active/backup NVS configuration blobs.
- Added validation, revision conflicts, rollback and migration.
- Added tracker BLE/Wi-Fi onboarding and gateway Wi-Fi onboarding.
- Added physical gateway write-unlock window.

### MQTT and storage

- Split point event stream from retained latest state.
- Added gateway management topics and status.
- Added Python/SQLite archiver with deduplication, multi-gateway reception
  metadata, retention and paginated MQTT history requests.

### Timestamp and web application

- Added history schema v2 with root Unix time and ULEB128 delta timestamps.
- Added point/history JSON schema v2 with GNSS/receive-time fallback.
- Added a static PWA with MQTT-over-WebSocket, IndexedDB and SVG route view.

### Deferred

- Flutter application
- End-to-end encryption and authenticated ACKs
- LoRa relay/mesh support
- QR/app-to-app provisioning transfer
