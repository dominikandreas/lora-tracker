# LoRa Tracker Network Lab

The Network Lab is a static, self-contained browser simulator. Its Web Worker
loads the portable C++ firmware policy core as WebAssembly and runs a seeded
discrete-event model for trackers, keyless repeaters, receivers, a single MQTT
service and its durable archive receipt.

## Build and run

Install Emscripten, Node.js 20+ and Chromium dependencies, then:

```text
cd components/simulator-web
make wasm
npm ci
npm test
npx playwright install --with-deps chromium
npm run test:browser
python3 -m http.server 8080 -d app
```

Open `http://localhost:8080`. The application fails closed if
`firmware-core.wasm` is absent or incompatible; the JavaScript reference core
exists only for deterministic unit vectors.

## Model boundary

The shared C++ core supplies Germany radio validation, LoRa airtime and
sensitivity policy, tracker sleep/retry/batching decisions, relay priority
timing and the relay/ACK collision guard. The JavaScript engine supplies
virtual time, movement, seeded GNSS outcomes, geometry, environmental loss,
collisions/capture, battery accounting and the in-browser MQTT/archive model.

Scenario files use versioned JSON and contain no secrets. The RF result is an
explainable engineering estimate, not a range promise or regulatory test.
