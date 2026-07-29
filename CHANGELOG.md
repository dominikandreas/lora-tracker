# Changelog

## Unreleased — production-hardening pass

- Removed support for all superseded LoRa, point and history-request schemas.
- Changed the product, package, workspace and default MQTT namespace to LoRa Tracker.
- Added certificate-verified gateway MQTT TLS and explicit plaintext opt-in.
- Added one random 256-bit owner key per device for BLE, local HTTP and
  configuration-mode OTA authorization; no device password or PIN is used.
- Disabled OTA outside a bounded authenticated configuration window and
  disabled telnet logging by default.
- Strengthened archiver validation and SQLite durability settings.
- Fixed browser defaults, strict schema validation and offline asset caching.
- Added pinned tracker/gateway PlatformIO builds and embedded contract simulation.
- Fixed an ESP32 RTC slow-memory link overflow by sizing the history queue to 448 points.
- Added onboarding, configuration, hardware, operations, security and production-readiness guidance.
- Added per-tracker AES-256-GCM history/ACK frames and monotonic replay rejection.
- Added a distinct retained transmit counter so retries cannot reuse GCM nonces.
- Added a no-bond custom BLE management protocol with attended, passwordless
  owner-key claim and application-session authentication.
- Added transactional runtime MQTT root-CA provisioning for generic gateway images.
- Added RTC retained-history metadata, bounds and CRC validation.
- Added pinned GitHub Actions CI/release builds, checksums, provenance, merged
  ESP Web Tools images and browser-flashing instructions.
- Added smart keyless repeater firmware with bounded multi-hop history/ACK forwarding.
- Fixed ACK AES-GCM nonce-domain reuse across tracker boot epochs.
- Added archive-confirmed SQLite delivery before gateway dedup or tracker ACK progression.
- Added a fail-closed NTP clock gate before certificate-validated gateway MQTT TLS.
- Enforced the Germany 868.0–868.6 MHz band-48 profile, 14 dBm conducted cap and 1% rolling-hour airtime limit on every transmitter.
- Bumped embedded configuration schema to 5; devices must be re-onboarded into the Germany-only radio profile after upgrade.
- Preserved unacknowledged points at daily rollover, stopped full-queue overwrite and made provisioning/key-counter persistence fail closed.
- Fixed repeater post-failure suppression and credential retention on factory reset.
- Hardened browser validation/rendering, reconnect framing, history pagination, local retention and service-worker updates.
- Anchored archiver topic parsing to nested configured base topics and documented remaining power-loss queue work.
- Added the self-contained browser Network Lab with the shared firmware policy core compiled to WASM, deterministic waypoint/environment/RF/repeater/MQTT simulation, scenario import/export and headless browser tests.
- Added a shared relay-clear ACK guard after simulation exposed collisions between fast archive-backed ACKs and still-airborne repeater HISTORY frames.
- Added relay link protocol v2 with selected reverse ACK routes, atomic HISTORY+ACK airtime reservations and packet-airtime-sized relay arbitration slots.
- Fixed the browser simulator accepting ACKs after the tracker timeout; timeout now closes the receive window and puts the virtual radio to sleep.
- Replaced the tracker’s hidden global button-hold ladder with visible per-page
  actions for distance reset, duration-scaled GNSS acquisition, immediate
  airtime-limited transmission with ACK countdown, and BLE debug-log toggling.
- Added machine-readable archiver backup, integrity-check, guarded restore and
  dry-run retention-pruning commands with automated recovery tests.
- Made manual GPS acquisition continuously consume NMEA data and show live
  satellite count, HDOP and listen countdown; moved effective speed to the
  status page.
- Hardened Wireless Tracker GNSS startup by avoiding a reset-line glitch,
  allowing the UC6580 rail to stabilize, and recovering once from a stalled
  NMEA stream with byte/sentence diagnostics.
- Added an Android application around the shared PWA using Capacitor, with
  native BLE onboarding, Keystore-backed opt-in MQTT credential storage,
  SQLite telemetry history, native notifications and the bundled Network Lab.
- Added a Java 21 Android CI build and debug APK artifact, plus browser/native
  transport tests and Android package/asset instrumentation checks.
- Allowed explicitly confirmed `ws://` MQTT connections in the Android app for
  private brokers while retaining HTTPS mixed-content protection in the PWA.
- Replaced manual device credentials and OS Bluetooth bonds with a stable,
  app-generated 256-bit owner key stored per device in Android Keystore-backed
  storage or browser localStorage.
- Kept MQTT transport failures visible throughout reconnect backoff, added
  actionable WebSocket endpoint diagnostics, and made Android persist the MQTT
  password automatically in Keystore-backed encrypted storage.
- Added MQTT WebSocket URL normalization with project defaults of port 1884 for
  `ws://` and 8884 for `wss://`, while preserving explicit custom ports and URL
  paths.
- Unified app and device connection setup around reusable Wi-Fi/MQTT profiles:
  trackers select Wi-Fi, gateways select Wi-Fi and MQTT, and the dashboard uses
  the same MQTT credentials plus its WebSocket endpoint. Incomplete tracker
  onboarding now keeps BLE configuration available without a timeout.
- Replaced tracker-only raw onboarding controls with a native device manager:
  authenticated BLE-first claiming for trackers and gateways, Keystore-backed
  per-device credentials, full role configuration, local-IP reconnect with BLE
  fallback, and coordinated gateway-registry/tracker pairing confirmation.
- Made tracker activation fail closed until Wi-Fi configuration and gateway
  pairing are both persisted; added gateway custom-BLE configuration.
- Fixed a post-pairing ESP32-S3 `BTC_TASK` stack-canary panic by moving BLE
  command parsing, NVS access and notification transmission out of GATT callbacks.
- Reworked tracker-to-gateway pairing as an idempotent two-device transaction:
  the app retains the tracker session, upserts through a narrow gateway registry
  endpoint, then confirms on the tracker. Added independent app/firmware rollout
  fallback, explicit gateway-IP recovery and partial-commit diagnostics.
- Persisted owner keys across device renames and stable BLE identifiers so a
  saved gateway is not misreported as unclaimed.
- Added versioned owner-key QR export/import, no-auth setup AP claim, shared
  BLE/Wi-Fi authorization, and authenticated PlatformIO OTA environments.
- Fixed Windows Capacitor sync paths in junction-backed workspaces and limited
  Android compatibility patching to the generated native modules.
- Fixed interrupted owner-key claims, BLE scan/connect and disconnect races,
  and misleading discovery labels in the device manager.
- Fixed multi-gateway confirmation responses and made tracker identity/key
  changes invalidate stale gateway registrations.
- Made tracker and boot-button gateway configuration/OTA windows consistently
  expire after ten minutes.
- Added short-lived browser management sessions so authenticated firmware
  forms and live logs work without exposing the owner key in URLs.
- Added optimistic revision checks and structured responses to repeater saves.
- Fixed gateway BLE configuration saves exhausting heap and aborting by making
  configuration CRC validation allocation-free, parsing PATCH data in place,
  and turning transactional allocation failures into protocol errors.
- Removed the remaining gateway save-time configuration allocations. BLE, HTTP,
  registry and rollback transactions now use fixed scratch storage, keep the
  live config unchanged until persistence succeeds and restore NVS on later
  CA/onboarding-marker failure.
- Deferred OTA service startup until explicitly requested and added bounded
  Android GATT retries with stale-disconnect rejection, improving first claim
  reliability without OS pairing.
- Added reusable app-level Wi-Fi and MQTT profiles. They can be created before
  connecting a device, then selected to fill compatible device configuration;
  Android stores complete profiles in Keystore-backed encrypted storage.
- Replaced fire-and-forget BLE response notifications with acknowledged,
  ordered indications on trackers and gateways, preventing Android from
  silently dropping middle chunks of larger configuration JSON responses.
- Persisted the MQTT password in the browser as requested, with explicit
  localStorage risk documentation; Android continues to use Keystore storage.

## Initial implementation

- Added adaptive GNSS acquisition, movement filtering and deep-sleep policies.
- Added compressed offline history, ACK-based LoRa delivery and retry backoff.
- Added multi-tracker gateway routing, MQTT publishing and deduplication.
- Added transactional configuration, Wi-Fi/BLE onboarding and rollback.
- Added the SQLite archiver, browser PWA and deterministic system simulator.
