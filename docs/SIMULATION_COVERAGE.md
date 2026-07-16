# LoRa Tracker simulation coverage

Two complementary simulators are shipped. The brokerless service suite executes
the production Python archiver and SQLite path. The browser **Network Lab** runs
a deterministic network in a Web Worker and loads the same portable C++ policy
core used by the ESP32 firmware as WebAssembly.

## Run the browser lab

```text
cd components/simulator-web
make wasm
npm ci
npm test
npx playwright install --with-deps chromium
npm run test:browser
python3 -m http.server 8080 -d app
```

Then open `http://localhost:8080`. The published copy is available from the
GitHub Pages **Run Network Lab** link. It needs no account, map service, broker
or network API after the static files have loaded.

The map uses metres rather than a geographic tile provider. Add and drag
trackers, repeaters and receivers; draw waypoint routes; place forests, trees
and small or large buildings; configure every modeled device parameter; change
day/night temperature, humidity and foliage wetness; pause, single-step or
accelerate time; and import/export versioned scenario JSON.

## Browser model

| Boundary / behaviour | Implementation |
|---|---|
| Firmware policy | The production C++17 core is compiled to WASM: Germany radio validation, LoRa time-on-air, sensitivity policy, tracker sleep/retry/batching, relay priority timing and relay/ACK collision guard |
| Tracker | Waypoint movement, configurable speed, seeded GNSS acquisition, point spacing, adaptive sleep, batching, retry/ACK timeout, legal airtime and a configurable current/capacity battery estimate |
| Radio | Explainable link budget with frequency, conducted power, antenna gain/loss/height, spreading factor, bandwidth, receiver sensitivity, distance/path loss, wet foliage, trees, buildings and seeded fading |
| Shared channel | Packet airtime, simultaneous transmissions, capture margin, collisions, modem matching and visible packet paths |
| Repeater | Opaque history/ACK forwarding, hop limits, deterministic slots, peer suppression, duplicate lifetimes, queue timing and Germany airtime deferral |
| Receiver | Current-frame authentication/decode decision, MQTT publication and ACK only after archive receipt |
| MQTT/archive | One protocol-faithful in-memory service with online/offline state, latency, point deduplication, durable-commit event and QoS semantics shown in the timeline |
| Reproducibility | Fixed seed, deterministic event ordering, restart, single-step, accelerated time and versioned JSON scenarios |
| Observability | Visual radio waves and paths plus an inspectable timeline for GNSS, sleep, queues, duty deferral, loss/capture, relay, MQTT, archive and ACK actions |

The atmosphere term at 868 MHz is intentionally very small at these distances.
Humidity affects the model primarily through wet foliage. Temperature adds only
a small receiver-noise and battery-context effect. The UI exposes individual
loss terms so it does not imply a dramatic generic “night range” effect.

## Automated evidence

`npm test` covers deterministic traces, movement/environment geometry, Germany
ERP/radio rejection, wet-foliage versus atmospheric loss, collisions, MQTT
outage retention, multi-hop archive-backed ACKs and native/WASM golden vectors.
Playwright opens the real WASM application in headless Chromium, steps time,
adds/configures a tracker and waypoint, exercises an MQTT outage and checks the
mobile layout. CI builds and uploads the complete static lab artifact.

Run the existing brokerless service and embedded contracts separately:

```text
cd components/archiver
python -m lora_tracker_archiver.simulator \
  --trackers 2 --points 12 --service-suite --embedded-suite
```

That path executes the real archiver callbacks, SQLite store, MQTT topic/payload
validation and all embedded protocol/configuration contracts. PlatformIO still
compiles all five real firmware targets.

## What remains physical or infrastructural

The Network Lab is an engineering model, not a digital twin calibrated to a
specific site. It cannot prove:

- antenna radiation patterns, polarization, body/animal attenuation, terrain,
  diffraction, multipath or actual vegetation/building material loss without
  measured site inputs;
- SX1276/SX1262 analogue behaviour, oscillator error, adjacent-channel
  interference, hidden nodes beyond the configured scenario, or installed ERP;
- real GNSS satellite geometry, assistance data, time to first fix or antenna
  placement;
- ESP32 boot, GPIO, BLE/Wi-Fi onboarding, NVS/RTC corruption, brownouts,
  deep-sleep current, battery aging/temperature or flash endurance;
- broker TCP/TLS/DNS, ACLs, retained persistence or QoS behaviour; the built-in
  MQTT service is protocol-faithful state logic, not a TCP broker;
- browser storage quotas/service-worker upgrades or the browser flashing path;
- RED/FuAG conformity or compliance with the Germany allocation. The modeled
  limiter and ERP checks help find configuration mistakes but do not replace
  calibrated measurement and product assessment.

Use hardware-in-the-loop, a real TLS broker, browser end-to-end tests and field
RF/power qualification before deployment. Simulator results should be reported
with the scenario, seed, model version and assumptions.
