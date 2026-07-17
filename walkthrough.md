# Application Refactoring Complete

I have completed the P1 Application architecture refactoring, including testing, verification, and documentation. The application now supports robust offline mapping, isolated modes, and reliable BLE onboarding.

## Summary of Changes

### 1. Application Modes & Simulation Isolation
- Integrated a Mode switch in the UI (Dashboard vs Network Lab).
- **Dashboard Mode**: Uses the production `IndexedDB` storage, hides the simulation UI, and does not instantiate the Web Worker.
- **Lab Mode**: Explicitly initializes the Web Worker for the Network Simulator, uses an isolated in-memory namespace for simulated telemetry (`isSimulated` flag in map points), and cleans up all simulation resources when the mode is disabled.
- Application mode is correctly persisted to `localStorage`.

### 2. Offline Maps & PMTiles
- Configured local offline mapping via **Leaflet** and **PMTiles**.
- Created an Explicit Layer Selector:
  - **Grid (Offline)**: Provides a fallback grid mapping layer when no archive is installed.
  - **OpenStreetMap (Online)**: A remote explicitly-opted-in map layer.
  - **PMTiles Archive (Offline)**: Support for offline maps imported from local `.pmtiles` archives via the Origin Private File System (OPFS).
- Repaired map camera behavior: Bounds are now fit only on explicit user actions (e.g. clicking a tracker or fetching history) instead of resetting uncontrollably on every heartbeat.

### 3. Robust BLE Transport & Onboarding
- Replaced rudimentary direct GATT writes with a highly-robust `BleTransport` class wrapper.
- Uses strict 18-byte MTU chunking and sequential sequencing for all payloads over BLE.
- Added a serialized queue, ensuring exactly one outstanding command exists at a time, backed by per-command timeouts and disconnect rejections.
- Implemented handler cleanup upon reconnect/disconnect.
- Supported tracking flows for `CLAIM`, `AUTH`, `GET/PATCH CONFIG`, `ROLLBACK`, `REBOOT` and `FACTORY RESET`.
- Dropped gateway HTTP onboarding provisioning from the PWA as decided.

### 4. Secure Storage
- Enforced session-only scope for MQTT credentials. All connection parameters (except passwords) are saved, while the password must explicitly be re-entered (or populated via browser auto-fill). The previous PIN-backed storage scheme was deleted to prevent weak symmetric fallback storage in a shared-origin deployment (like GitHub Pages).

### 5. Alerts & Notifications
- Refined alerting logic into robust state machines evaluated on a fixed interval (`setInterval`), separating alert checks from new point ingestions.
- Added hysteresis loops to battery states (e.g. critical alerts at 20%, 10%, 5%, resetting only when charging past a 5% threshold).
- Discovered and alerted on "Unusual Movement" dynamically (e.g. speed spikes exceeding normal biological limitations).
- Integrated an explicit `Enable Alerts` UI button adhering to modern Notification API requirements.
- Documented distinction between active PWA notifications vs closed-app Web Push logic.

### 6. Verification & End-to-End Testing
- Fully integrated **Playwright** end-to-end tests covering Mode switching, explicit simulation initialization, CSP rules (guaranteeing no CDN scripts), map fallback logic, and BLE capability.
- Added a fully mocked-adapter suite for the `BleTransport` chunking algorithm, verifying maximum string chunk lengths without hardware-in-the-loop dependencies.

All tests passed locally with deterministic builds, and the code has been staged, committed, and pushed!
