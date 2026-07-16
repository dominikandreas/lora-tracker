import { FirmwareCore } from './firmware-core.js';
import { createDefaultScenario } from './default-scenario.js';
import { SimulationEngine, validateScenario } from './simulation-engine.js';

let core;
let engine;
let timer;
let speed = 60;

function send(type, payload = {}) { self.postMessage({ type, ...payload }); }
function snapshot() { send('snapshot', { snapshot: engine.snapshot() }); }

function normalizeScenario(scenario) {
  scenario.world.minXM ??= 0; scenario.world.minYM ??= 0;
  scenario.map.anchorX ??= scenario.world.minXM + scenario.world.widthM / 2;
  scenario.map.anchorY ??= scenario.world.minYM + scenario.world.heightM / 2;
  scenario.environment.siteLossDb ??= 0;
  return scenario;
}

function expandWorldForScenario(scenario) {
  const world = scenario.world;
  let minX = world.minXM ?? 0; let minY = world.minYM ?? 0;
  let maxX = minX + world.widthM; let maxY = minY + world.heightM;
  const include = (point, margin = 0) => {
    minX = Math.min(minX, point.x - margin); minY = Math.min(minY, point.y - margin);
    maxX = Math.max(maxX, point.x + margin); maxY = Math.max(maxY, point.y + margin);
  };
  for (const device of scenario.devices) { include(device); for (const point of device.waypoints ?? []) include(point); }
  for (const obstacle of scenario.obstacles) {
    if (obstacle.type === 'tree') include(obstacle, obstacle.radius ?? 0);
    else for (const point of obstacle.points ?? []) include(point);
  }
  const grid = Math.max(1, world.gridM ?? 50);
  world.minXM = Math.floor(minX / grid) * grid;
  world.minYM = Math.floor(minY / grid) * grid;
  world.widthM = Math.ceil((maxX - world.minXM) / grid) * grid;
  world.heightM = Math.ceil((maxY - world.minYM) / grid) * grid;
}

async function initialize(scenario = createDefaultScenario()) {
  scenario = normalizeScenario(scenario);
  core ??= await FirmwareCore.load();
  const errors = await validateScenario(scenario, core);
  if (errors.length) throw new Error(errors.join('; '));
  engine = new SimulationEngine(scenario, core);
  speed = scenario.clock.speed ?? 60;
  snapshot();
  send('ready', { core: { kind: core.kind, version: core.version } });
}

async function requireValidMutation(mutate) {
  const candidate = engine.snapshot().scenario;
  mutate(candidate);
  expandWorldForScenario(candidate);
  const errors = await validateScenario(candidate, core);
  if (errors.length) throw new Error(errors.join('; '));
  engine.updateScenario({ world: candidate.world });
}

function stop() {
  clearInterval(timer);
  timer = undefined;
  send('play-state', { playing: false });
}

function play() {
  if (timer) return;
  timer = setInterval(() => {
    engine.advance(Math.max(1, Math.round(speed / 5)));
    snapshot();
  }, 200);
  send('play-state', { playing: true });
}

self.onmessage = async ({ data }) => {
  try {
    if (data.type === 'init') await initialize(data.scenario);
    else if (data.type === 'play') play();
    else if (data.type === 'pause') stop();
    else if (data.type === 'step') { engine.advance(data.seconds ?? 1); snapshot(); }
    else if (data.type === 'speed') { speed = data.speed; }
    else if (data.type === 'reset') { stop(); await initialize(data.scenario ?? engine.scenario); }
    else if (data.type === 'replace-scenario') { stop(); await initialize(data.scenario); }
    else if (data.type === 'environment') {
      await requireValidMutation((candidate) => {
        if (data.patch.environment) Object.assign(candidate.environment, data.patch.environment);
        if (data.patch.mqtt) Object.assign(candidate.mqtt, data.patch.mqtt);
        if (data.patch.clock) Object.assign(candidate.clock, data.patch.clock);
        if (data.patch.map) Object.assign(candidate.map, data.patch.map);
      });
      engine.updateScenario(data.patch); snapshot();
    }
    else if (data.type === 'add-entity') {
      await requireValidMutation((candidate) => {
        if (data.entity.role) candidate.devices.push(data.entity);
        else candidate.obstacles.push(data.entity);
      });
      engine.addEntity(data.entity); snapshot();
    }
    else if (data.type === 'update-entity') {
      await requireValidMutation((candidate) => {
        const entity = candidate.devices.find((item) => item.id === data.id) ??
          candidate.obstacles.find((item) => item.id === data.id);
        if (!entity) throw new Error(`Unknown entity ${data.id}`);
        Object.assign(entity, data.changes);
      });
      engine.updateEntity(data.id, data.changes); snapshot();
    }
    else if (data.type === 'remove-entity') {
      await requireValidMutation((candidate) => {
        const device = candidate.devices.find((item) => item.id === data.id);
        if (device) {
          candidate.devices = candidate.devices.filter((item) => item.id !== data.id);
          if (device.role === 'tracker') for (const receiver of candidate.devices.filter((item) => item.role === 'receiver')) receiver.config.registeredTrackerIds = receiver.config.registeredTrackerIds.filter((id) => id !== data.id);
        } else candidate.obstacles = candidate.obstacles.filter((item) => item.id !== data.id);
      });
      engine.removeEntity(data.id); snapshot();
    }
    else if (data.type === 'add-waypoint') {
      await requireValidMutation((candidate) => {
        const tracker = candidate.devices.find((item) => item.id === data.id && item.role === 'tracker');
        if (!tracker) throw new Error(`Unknown tracker ${data.id}`);
        tracker.waypoints.push(data.point);
      });
      engine.addWaypoint(data.id, data.point); snapshot();
    }
    else if (data.type === 'update-waypoint' || data.type === 'remove-waypoint') {
      await requireValidMutation((candidate) => {
        const tracker = candidate.devices.find((item) => item.id === data.id && item.role === 'tracker');
        if (!tracker) throw new Error(`Unknown tracker ${data.id}`);
        if (data.type === 'update-waypoint') tracker.waypoints[data.index] = data.point;
        else tracker.waypoints.splice(data.index, 1);
      });
      if (data.type === 'update-waypoint') engine.updateWaypoint(data.id, data.index, data.point);
      else engine.removeWaypoint(data.id, data.index);
      snapshot();
    }
  } catch (error) {
    const fatal = !engine;
    if (fatal) stop();
    send('error', { message: error instanceof Error ? error.message : String(error), fatal });
    if (engine) snapshot();
  }
};
