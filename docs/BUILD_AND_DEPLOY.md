# Build and deployment

## Prerequisites

- PlatformIO Core for embedded builds
- Python 3.11+ for the archiver and system simulator
- Node.js 22+ for browser tests and Capacitor tooling
- Emscripten and GNU Make for the WebAssembly Network Lab
- Java 21 plus the Android SDK for the native Android application
- A private MQTT broker with TLS TCP and WebSocket listeners

## Secrets

Copy `secrets.example.h` to `secrets.h` in each firmware directory. These files
are git-ignored. Generic devices receive a random 256-bit owner key from the
app during attended BLE or setup-AP claim; no factory password, PIN, Bluetooth
bond, or OTA-only credential is compiled into the image or printed. For the gateway, provision the TLS port
and PEM root CA during authenticated onboarding;
`mqtt_ca_certificate` in `secrets.h` is only an optional factory seed. Leave
`allow_insecure_mqtt=false` outside an isolated test network.

The archiver uses environment variables documented in `.env.example`. TLS is
enabled by default; provide `MQTT_CA_FILE`. Set `ALLOW_INSECURE_MQTT=true` only
for an explicit local test.

## Tracker firmware

```bash
pio run -d components/tracker-firmware -e heltec_wifi_lora_32_v2
pio run -d components/tracker-firmware -e heltec_wireless_tracker
```

The first target supports the older Heltec V2/SX1276 plus BN-220/OLED branch.
The second supports the Heltec Wireless Tracker ESP32-S3/SX1262/UC6580/TFT
branch. Never upload until the exact board, region, antenna and serial port have
been checked.

An unprovisioned tracker starts an open `LoRaTracker-<suffix>` setup AP and a
no-bond BLE claim service. See [onboarding](ONBOARDING.md).

## Gateway firmware

```bash
pio run -d components/gateway-firmware -e heltec_wifi_lora_32_v2
```

The checked-in gateway pin map is only for the Heltec V2/SX1276:
`SCK=5`, `MISO=19`, `MOSI=27`, `SS=18`, `RST=14`, `DIO0=26`. Porting to a
current ESP32-S3/SX1262 board is a production TODO.

An unprovisioned gateway starts `LoRaGateway-<gateway_id>`. Once provisioned,
hold USER for five seconds to unlock writes for ten minutes; HTTP authentication
is still required. The saved owner key can also open
`http://<gateway-ip>/enable-config` or the app can enable the same window over
BLE/Wi-Fi before a PlatformIO OTA upload.

## Repeater firmware

```bash
pio run -d components/repeater-firmware -e heltec_wifi_lora_32_v2
pio run -d components/repeater-firmware -e heltec_wireless_tracker
```

An erased repeater starts an open, attended setup AP and is claimed with an app-
generated owner key. It does not need or store tracker AEAD keys. See
[repeaters](REPEATERS.md) for forwarding policy, placement and qualification.

## Archiver

```bash
cd components/archiver
python -m venv .venv
. .venv/bin/activate
pip install .
cp .env.example .env
set -a; . ./.env; set +a
python -m lora_tracker_archiver
```

Or run the example container deployment:

```bash
cd components/archiver
cp .env.example .env
docker compose -f docker-compose.example.yml up --build -d
```

Persist and back up `/data/lora-tracker-history.sqlite3` using SQLite's online
backup mechanism or a stopped, consistent copy.

## Web app

Serve `components/web-app` from HTTPS and connect it to a trusted `wss://` MQTT
listener. A local development server is sufficient for a smoke test:

```bash
cd components/web-app
npm ci
npm run build
python3 -m http.server 8080
```

The browser app persists broker URL, namespace, username, MQTT password,
reusable Wi-Fi/MQTT profiles and per-device owner keys in origin-scoped
localStorage so reconnects and repeated device setup require no manual entry.
Use a trusted browser profile and a least-privilege broker account. Android
stores MQTT passwords, complete connection profiles and owner keys in
Keystore-backed encrypted storage and can transfer device authority through an
explicit full-authority QR export. Run `npm run package` to assemble the self-contained
`dist/` artifact. The public
documentation deploys that artifact at `/app/` and the Network Lab at
`/simulator/`.

## Android application

The Android application reuses the exact web-app source and embeds its built
assets with Capacitor. It adds native BLE, native local-device HTTP, secure
broker/per-device credential storage, SQLite and
notifications; no remote application bundle is loaded at runtime.

Install Android Studio (including the current SDK), Java 21 and Node.js. Then:

```bash
cd components/web-app
npm ci
npm run native:build
```

The command selects `gradlew` or `gradlew.bat` for the host. If several JDKs are
installed, set `JAVA_HOME` to Android Studio's Java 21 runtime or another Java
21 installation first. The debug APK is created at
`android/app/build/outputs/apk/debug/app-debug.apk`. Install it only on a test device;
store signing, release signing-key custody and app publication are deliberately
out of scope for now.

`native:sync` rebuilds the dashboard and bundled Network Lab before updating
Android assets and plugin configuration. Android accepts `wss://` and, after an
explicit warning, `ws://` for private-network brokers. Plaintext WebSockets
expose MQTT credentials and gateway-decoded telemetry; LoRa frame encryption
does not extend beyond the gateway. Non-secret connection settings use
Preferences, telemetry uses bounded SQLite storage, and MQTT and per-device
credentials use Android Keystore-backed secure storage. Closed-app alerts still
require a future server-backed push
service; native local notifications are generated while the application process
is running.

## Browser Network Lab

```bash
cd components/simulator-web
npm ci
npm run build
npx playwright install --with-deps chromium
npm run test:browser
python3 -m http.server 8080 -d app
```

`npm run build` compiles `components/firmware-core` with Emscripten through the
shared simulation engine. Do not serve the source directory without first producing
`app/firmware-core.wasm`; the release
application fails closed rather than substituting JavaScript policy logic.
GitHub Pages builds this file from source and publishes the lab below
`/simulator/`.

## Validation

```bash
cd components/archiver
python -m pytest
python -m lora_tracker_archiver.simulator \
  --trackers 2 --points 12 --service-suite --embedded-suite

cd ../web-app
npm ci
npm run build
npm test

cd ../simulator-web
npm ci
npm run build
npx playwright install chromium
npm run test:browser
```

Run all five PlatformIO targets above. Before any field trial, also exercise a
real TLS broker, supported browsers and physical devices as described in
[simulation coverage](SIMULATION_COVERAGE.md).

## Deployment sequence

There is no rolling compatibility mode. Upgrade the complete installation in a
maintenance window:

1. Back up gateway configuration and the archiver database.
2. Stop consumers and deploy the archiver and web app.
3. Flash and provision the gateway, then verify TLS and broker ACLs.
4. Flash and place repeaters, if used, then verify their airtime and hop policy.
5. Flash each tracker with the matching release and verify its registry entry.
6. Confirm current timestamps, increasing sequences, ACK-driven queue progress
   and a complete history response.
7. Record the firmware commit and configuration revision for every unit.

Use `lora-tracker.code-workspace` to open all PlatformIO projects in VS Code.

## Automated builds and releases

`.github/workflows/ci.yml` tests Python and both browser applications,
compiles the shared core to WASM, runs native/WASM parity plus headless Chromium,
builds an Android debug APK with Java 21, uploads the static application and
Android artifacts, and compiles every firmware target on
pushes and pull requests. `.github/workflows/release.yml`
runs the same gates for `v*` tags, checks that the tag matches `VERSION`, builds
secret-free generic firmware, creates merged ESP Web Tools images, publishes
checksums/ELF files/manifests, and generates build-provenance attestations.

Prepare a release only from a reviewed clean commit:

```bash
VERSION=$(cat VERSION)
git tag -s "v${VERSION}" -m "LoRa Tracker ${VERSION}"
git push origin "v${VERSION}"
```

The workflow verifies that the remote tag already exists before creating the
GitHub release. Enable immutable releases in repository settings when the
project's release policy is finalized. See [browser flashing](FLASHING.md) for
the operator workflow.
