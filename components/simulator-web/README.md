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

## Map editor and location

Forests and buildings are polygons. Select one to drag a corner; click an edge
midpoint to insert a new corner. Tracker waypoints can also be dragged and
removed from the tracker inspector. Devices and obstacles can be removed from
their inspector; removing a tracker also removes it from receiver registries.

The default view is a metre grid. Enter latitude/longitude, or grant the
browser location permission, then select **Satellite** to use Esri World
Imagery as an optional background. Satellite tiles are requested directly from
that provider only while satellite mode is selected; no location is sent to the
simulator service. Geographic positions shown in the inspector and route/link
lengths use a great-circle (haversine) calculation. The underlying RF/obstacle
model remains a local engineering model and is not terrain-aware.

## Inspection and replay

Selecting a device exposes its modeled local state: sleep/radio status,
airtime tokens, queues, retry/GNSS counters, relay cache/queue state and packet
statistics. The tracker timeline overlays local GNSS points up to a chosen
simulation time. The archive map separately overlays only points durably
committed through the selected receiver and tracker, including the archive-node
provenance. Select a device to show dashed red links to modem-compatible peers
that are currently below receiver sensitivity; the label gives the negative
link margin. These are model diagnostics, not a regulatory range statement.
