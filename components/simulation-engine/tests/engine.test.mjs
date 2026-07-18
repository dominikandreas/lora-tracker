import assert from "node:assert/strict";
import test from "node:test";

import { ReferenceCore } from "../src/firmware-core.js";
import { createDefaultScenario } from "../src/default-scenario.js";
import {
  calculateLink,
  environmentAt,
  SimulationEngine,
  validateScenario,
} from "../src/simulation-engine.js";
import { exactDistanceM, pointInPolygon } from "../src/geometry.js";

test("Germany profile rejects an illegal frequency and installed ERP", async () => {
  const core = new ReferenceCore();
  const scenario = createDefaultScenario();
  assert.deepEqual(await validateScenario(scenario, core), []);
  scenario.devices[0].radio.frequencyHz = 869_525_000;
  assert.match(
    (await validateScenario(scenario, core)).join(" "),
    /Germany radio profile/,
  );
  scenario.devices[0].radio.frequencyHz = 868_100_000;
  scenario.devices[0].antennaGainDbi = 9;
  assert.match(
    (await validateScenario(scenario, core)).join(" "),
    /14 dBm ERP/,
  );
});

test("day and night conditions change wet foliage loss without inventing large atmospheric loss", () => {
  const core = new ReferenceCore();
  const scenario = createDefaultScenario();
  const tracker = scenario.devices[0];
  const gateway = scenario.devices[2];
  const day = environmentAt(scenario, 5 * 3600);
  const night = environmentAt(scenario, 17 * 3600);
  assert.ok(day.temperatureC > night.temperatureC);
  assert.ok(night.humidityPct > day.humidityPct);
  const dryLink = calculateLink(
    scenario,
    core,
    tracker,
    gateway,
    5 * 3600,
    "same",
  );
  const wetLink = calculateLink(
    scenario,
    core,
    tracker,
    gateway,
    17 * 3600,
    "same",
  );
  assert.ok(wetLink.forestLoss > dryLink.forestLoss);
  assert.ok(wetLink.atmosphereLoss < 0.01);
});

test("global range pessimism reduces a link budget by the configured measured margin", () => {
  const core = new ReferenceCore();
  const scenario = createDefaultScenario();
  scenario.environment.siteLossDb = 0;
  const baseline = calculateLink(
    scenario,
    core,
    scenario.devices[0],
    scenario.devices[2],
    0,
    "calibration",
  );
  scenario.environment.siteLossDb = 22;
  const calibrated = calculateLink(
    scenario,
    core,
    scenario.devices[0],
    scenario.devices[2],
    0,
    "calibration",
  );
  assert.equal(calibrated.siteLossDb, 22);
  assert.ok(Math.abs(calibrated.marginDb - baseline.marginDb + 22) < 1e-9);
});

test("fixed seed produces an identical event trace", () => {
  const core = new ReferenceCore();
  const first = new SimulationEngine(createDefaultScenario(), core);
  const second = new SimulationEngine(createDefaultScenario(), core);
  first.advance(900);
  second.advance(900);
  const select = (engine) =>
    engine.events.map(({ timeS, type, message }) => ({ timeS, type, message }));
  assert.deepEqual(select(first), select(second));
  assert.ok(first.events.some((event) => event.type === "radio-tx"));
});

test("relay path can archive data and return an ACK", () => {
  const core = new ReferenceCore();
  const scenario = createDefaultScenario();
  // Block the direct path while keeping tracker -> relay -> gateway viable.
  scenario.devices[0].x = 80;
  scenario.devices[0].y = 470;
  scenario.devices[0].waypoints = [
    { x: 80, y: 470 },
    { x: 250, y: 470 },
  ];
  const engine = new SimulationEngine(scenario, core);
  engine.advance(3600);
  assert.ok(engine.events.some((event) => event.type === "relay-queue"));
  assert.ok(engine.events.some((event) => event.type === "archive"));
  assert.ok(
    engine.events.some((event) => event.type === "ack"),
    JSON.stringify(
      engine.events
        .slice(-40)
        .map(({ timeS, type, message }) => ({ timeS, type, message })),
    ),
  );
  assert.ok(engine.archive.size > 0);
});

test("MQTT outage withholds archive-backed ACK and retains tracker queue", () => {
  const core = new ReferenceCore();
  const scenario = createDefaultScenario();
  scenario.mqtt.online = false;
  const engine = new SimulationEngine(scenario, core);
  engine.advance(1800);
  const tracker = engine.devices.get("tracker-1");
  assert.ok(
    engine.events.some(
      (event) => event.type === "mqtt" && /withheld ACK/.test(event.message),
    ),
  );
  assert.ok(tracker.runtime.queue.length > 0);
  assert.equal(engine.archive.size, 0);
});

test("collision model reports simultaneous equal-power frames", () => {
  const core = new ReferenceCore();
  const scenario = createDefaultScenario();
  const tracker2 = structuredClone(scenario.devices[0]);
  tracker2.id = "tracker-2";
  tracker2.name = "Second tracker";
  tracker2.x += 5;
  tracker2.waypoints = tracker2.waypoints.map((point) => ({
    x: point.x + 5,
    y: point.y,
  }));
  scenario.devices.push(tracker2);
  const engine = new SimulationEngine(scenario, core);
  engine.advance(1200);
  assert.ok(engine.events.some((event) => event.type === "collision"));
});

test("polygon obstacles block links and their vertices are validated", async () => {
  const core = new ReferenceCore();
  const scenario = createDefaultScenario();
  const forest = scenario.obstacles.find(
    (obstacle) => obstacle.type === "forest",
  );
  assert.equal(pointInPolygon({ x: 500, y: 260 }, forest.points), true);
  const link = calculateLink(
    scenario,
    core,
    scenario.devices[0],
    scenario.devices[2],
    0,
    "polygon",
  );
  assert.ok(link.forestLoss > 0);
  forest.points[0].x = Number.NaN;
  assert.match(
    (await validateScenario(scenario, core)).join(" "),
    /invalid polygon/,
  );
});

test("placement coordinates are not constrained by the current render extent", async () => {
  const core = new ReferenceCore();
  const scenario = createDefaultScenario();
  scenario.devices[0].x = -5000;
  scenario.devices[0].waypoints[0] = { x: 12000, y: -8000 };
  scenario.obstacles.find((obstacle) => obstacle.type === "forest").points[0] =
    { x: 20000, y: 15000 };
  assert.deepEqual(await validateScenario(scenario, core), []);
});

test("georeferenced points use great-circle distance and support safe editing removal", () => {
  const core = new ReferenceCore();
  const scenario = createDefaultScenario();
  const start = scenario.devices[0].waypoints[0];
  const end = { x: start.x + 100, y: start.y };
  assert.ok(Math.abs(exactDistanceM(scenario, start, end) - 100) < 0.1);
  const engine = new SimulationEngine(scenario, core);
  engine.updateWaypoint("tracker-1", 1, end);
  assert.deepEqual(engine.scenario.devices[0].waypoints[1], end);
  engine.removeWaypoint("tracker-1", 1);
  assert.equal(engine.scenario.devices[0].waypoints.length, 3);
  engine.removeEntity("tracker-1");
  assert.equal(
    engine.scenario.devices.some((device) => device.id === "tracker-1"),
    false,
  );
  assert.deepEqual(
    engine.scenario.devices.find((device) => device.role === "receiver").config
      .registeredTrackerIds,
    [],
  );
});

test("snapshot exposes bounded local tracks, archive provenance and live link margins", () => {
  const core = new ReferenceCore();
  const scenario = createDefaultScenario();
  const engine = new SimulationEngine(scenario, core);
  engine.advance(1800);
  const snapshot = engine.snapshot();
  assert.ok(snapshot.trackHistory["tracker-1"].length > 0);
  assert.ok(snapshot.archive.length > 0);
  assert.equal(
    snapshot.archive.every(
      (point) => point.gatewayId === "gateway-1" && point.archivedAtS >= 0,
    ),
    true,
  );
  assert.ok(
    snapshot.links.some(
      (link) => link.fromId === "tracker-1" && link.toId === "gateway-1",
    ),
  );
});

test("ACK timeout closes the tracker radio and late ACK cannot clear data", () => {
  const engine = new SimulationEngine(
    createDefaultScenario(),
    new ReferenceCore(),
  );
  const tracker = engine.devices.get("tracker-1");
  tracker.runtime.queue = [{ seq: 7, x: tracker.x, y: tracker.y, timeS: 0 }];
  tracker.runtime.inflight[42] = {
    acked: false,
    active: true,
    lastSeq: 7,
    deadlineS: 1,
  };
  tracker.runtime.rxWindowUntilS = 1;
  engine.timeS = 1;
  engine.ackTimeout({ trackerId: tracker.id, counter: 42 });
  engine.timeS = 2;
  engine.trackerAck(tracker, {
    key: "late-ack",
    transactionCounter: 42,
    ackSeq: 7,
  });
  assert.equal(tracker.runtime.queue.length, 1);
  assert.equal(tracker.runtime.inflight[42].active, false);
  assert.equal(tracker.runtime.status, "sleeping");
  assert.ok(engine.events.some((event) => event.type === "late-ack"));
});

test("co-hearing relays serialize HISTORY and only the selected route accepts ACK", () => {
  const scenario = createDefaultScenario();
  const relay2 = structuredClone(
    scenario.devices.find((device) => device.id === "relay-1"),
  );
  relay2.id = "relay-2";
  relay2.name = "Second relay";
  relay2.x += 5;
  scenario.devices.push(relay2);
  const engine = new SimulationEngine(scenario, new ReferenceCore());
  const tracker = engine.devices.get("tracker-1");
  const relay1 = engine.devices.get("relay-1");
  const second = engine.devices.get("relay-2");
  relay1.runtime.airtimeTokensMs = relay1.runtime.airtimeCapacityMs;
  second.runtime.airtimeTokensMs = second.runtime.airtimeCapacityMs;
  const frame = {
    key: "tracker-1:1:99:history",
    type: "history",
    messageType: 1,
    schemaVersion: 2,
    deviceId: tracker.id,
    deviceHashHi: tracker.hashHi,
    deviceHashLo: tracker.hashLo,
    bootId: 1,
    counter: 99,
    transactionCounter: 99,
    hop: 0,
    hopLimit: 2,
    route: [],
    routeCursor: 0,
    points: [{ seq: 7 }],
    bytes: 100,
  };
  engine.queueRelay(relay1, frame);
  engine.queueRelay(second, frame);
  engine.timeS = 20;
  engine.processTasks();
  assert.equal(relay1.runtime.stats.relayed + second.runtime.stats.relayed, 1);

  const selected = relay1.runtime.stats.relayed ? relay1 : second;
  const unselected = selected.id === relay1.id ? second : relay1;
  const ack = {
    key: "tracker-1:1:7:ack",
    type: "ack",
    messageType: 2,
    schemaVersion: 1,
    deviceId: tracker.id,
    bootId: 1,
    counter: 7,
    transactionCounter: 99,
    hop: 0,
    hopLimit: 1,
    route: [selected.id],
    routeCursor: 1,
    ackSeq: 7,
    bytes: 78,
  };
  engine.queueRelay(selected, ack);
  engine.queueRelay(unselected, ack);
  assert.ok(selected.runtime.pending[ack.key]);
  assert.equal(unselected.runtime.pending[ack.key], undefined);
});
