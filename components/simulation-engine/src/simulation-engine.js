import { SCENARIO_VERSION } from './default-scenario.js';
import { exactDistanceM, pointInObstacle } from './geometry.js';

const deepCopy = (value) => JSON.parse(JSON.stringify(value));
const clamp = (value, min, max) => Math.max(min, Math.min(max, value));
const distance = (a, b) => Math.hypot(a.x - b.x, a.y - b.y);

function hashText(value) {
  let hash = 0x811c9dc5;
  for (const char of value) {
    hash ^= char.charCodeAt(0);
    hash = Math.imul(hash, 0x01000193) >>> 0;
  }
  return hash >>> 0;
}

function pointToSegmentDistance(point, start, end) {
  const dx = end.x - start.x;
  const dy = end.y - start.y;
  if (dx === 0 && dy === 0) return distance(point, start);
  const t = clamp(((point.x - start.x) * dx + (point.y - start.y) * dy) /
    (dx * dx + dy * dy), 0, 1);
  return distance(point, { x: start.x + t * dx, y: start.y + t * dy });
}

export function environmentAt(scenario, timeS) {
  const hour = (scenario.clock.startHour + timeS / 3600) % 24;
  const daylight = clamp(Math.sin(((hour - 6) / 12) * Math.PI), 0, 1);
  const e = scenario.environment;
  const temperatureC = e.nightTemperatureC +
    (e.dayTemperatureC - e.nightTemperatureC) * daylight;
  const humidityPct = e.nightHumidityPct +
    (e.dayHumidityPct - e.nightHumidityPct) * daylight;
  const wetness = clamp(e.foliageWetness + (humidityPct - 50) / 160, 0, 1);
  return { hour, daylight, temperatureC, humidityPct, wetness };
}

export function obstacleLossDb(scenario, from, to, environment) {
  const samples = Math.max(12, Math.ceil(distance(from, to) / 15));
  let forestSamples = 0;
  let buildingLoss = 0;
  let treeLoss = 0;
  for (const obstacle of scenario.obstacles) {
    if (obstacle.type === 'forest') {
      let inside = 0;
      for (let index = 1; index < samples; index += 1) {
        const point = {
          x: from.x + (to.x - from.x) * index / samples,
          y: from.y + (to.y - from.y) * index / samples,
        };
        if (pointInObstacle(point, obstacle)) inside += 1;
      }
      forestSamples += inside * (obstacle.density ?? 0.7);
    } else if (obstacle.type.startsWith('building')) {
      let intersects = false;
      for (let index = 1; index < samples; index += 1) {
        const point = {
          x: from.x + (to.x - from.x) * index / samples,
          y: from.y + (to.y - from.y) * index / samples,
        };
        if (pointInObstacle(point, obstacle)) { intersects = true; break; }
      }
      if (intersects) buildingLoss += obstacle.type === 'building-large' ? 24 : 12;
    } else if (obstacle.type === 'tree') {
      if (pointToSegmentDistance(obstacle, from, to) <= (obstacle.radius ?? 8)) {
        treeLoss += 2.5 + 3.5 * environment.wetness;
      }
    }
  }
  const forestMeters = forestSamples / samples * exactDistanceM(scenario, from, to);
  const forestLoss = forestMeters * 0.035 * (0.75 + environment.wetness * 0.9);
  return { total: forestLoss + buildingLoss + treeLoss, forestLoss, buildingLoss, treeLoss };
}

export function calculateLink(scenario, core, from, to, timeS, frameKey = '') {
  const rangeM = Math.max(1, exactDistanceM(scenario, from, to));
  const frequencyMhz = from.radio.frequencyHz / 1e6;
  const freeSpaceLoss = 32.44 + 20 * Math.log10(rangeM / 1000) + 20 * Math.log10(frequencyMhz);
  const excessGroundLoss = rangeM > 40 ? 7 * Math.log10(rangeM / 40) : 0;
  const heightBenefitDb = clamp(3 * Math.log2(
    Math.max(0.05, from.antennaHeightM * to.antennaHeightM / 4)), -8, 8);
  const environment = environmentAt(scenario, timeS);
  const obstacles = obstacleLossDb(scenario, from, to, environment);
  const atmosphereLoss = rangeM / 1000 * (0.0002 + environment.humidityPct * 0.000004);
  const siteLossDb = scenario.environment.siteLossDb ?? 0;
  const fadeHash = hashText(`${scenario.seed}:${frameKey}:${to.id}`);
  const fadingDb = (fadeHash / 0xffffffff - 0.5) * 8;
  const rxPowerDbm = from.radio.txPowerDbm + from.antennaGainDbi - from.cableLossDb +
    to.antennaGainDbi - to.cableLossDb - freeSpaceLoss - excessGroundLoss +
    heightBenefitDb - obstacles.total - atmosphereLoss - siteLossDb + fadingDb;
  const temperaturePenalty = Math.max(0, environment.temperatureC - 20) * 0.025;
  const sensitivityDbm = core.sensitivityDbm(to.radio) + temperaturePenalty;
  return {
    rangeM, rxPowerDbm, sensitivityDbm,
    marginDb: rxPowerDbm - sensitivityDbm,
    freeSpaceLoss, excessGroundLoss, heightBenefitDb, atmosphereLoss, siteLossDb, fadingDb,
    ...obstacles,
  };
}

export async function validateScenario(scenario, core) {
  const errors = [];
  if (scenario.schemaVersion !== SCENARIO_VERSION) errors.push('Unsupported scenario schema');
  const world = scenario.world ?? {};
  if (!Number.isFinite(world.minXM ?? 0) || !Number.isFinite(world.minYM ?? 0) ||
      !Number.isFinite(world.widthM) || !Number.isFinite(world.heightM) ||
      world.widthM < 100 || world.heightM < 100) {
    errors.push('World must be at least 100 m by 100 m');
  }
  const map = scenario.map ?? {};
  if (!['grid', 'satellite'].includes(map.mode) || !Number.isFinite(map.centerLat) || Math.abs(map.centerLat) > 85 ||
      !Number.isFinite(map.centerLng) || Math.abs(map.centerLng) > 180 || !(map.zoom >= 1 && map.zoom <= 20)) errors.push('Map location is invalid');
  const environment = scenario.environment ?? {};
  if (![environment.dayTemperatureC, environment.nightTemperatureC].every((value) => value >= -30 && value <= 60) ||
      ![environment.dayHumidityPct, environment.nightHumidityPct].every((value) => value >= 0 && value <= 100) ||
      !(environment.foliageWetness >= 0 && environment.foliageWetness <= 1) || !((environment.siteLossDb ?? 0) >= 0 && (environment.siteLossDb ?? 0) <= 80)) errors.push('Environment values are outside supported bounds');
  if (!scenario.mqtt || ![scenario.mqtt.latencyMs, scenario.mqtt.archiveLatencyMs].every((value) => value >= 0 && value <= 60000)) {
    errors.push('MQTT latency must be between 0 and 60000 ms');
  }
  if (!Array.isArray(scenario.devices) || !Array.isArray(scenario.obstacles)) {
    errors.push('Scenario devices and obstacles must be arrays');
  }
  const ids = new Set();
  for (const device of scenario.devices ?? []) {
    if (!/^[a-z0-9][a-z0-9_-]{0,23}$/.test(device.id)) errors.push(`${device.id || 'device'} has an invalid ID`);
    if (ids.has(device.id)) errors.push(`Duplicate device ID ${device.id}`);
    ids.add(device.id);
    if (!['tracker', 'repeater', 'receiver'].includes(device.role)) errors.push(`${device.id} has an invalid role`);
    if (!core.validateRadio(device.radio)) errors.push(`${device.id} violates the Germany radio profile`);
    const maximumAirtimeMs = core.airtimeMs(255, device.radio);
    if (maximumAirtimeMs <= 0 || maximumAirtimeMs >= 36_000) errors.push(`${device.id} radio cannot fit one maximum frame inside the Germany airtime budget`);
    const installedEirp = device.radio.txPowerDbm + device.antennaGainDbi - device.cableLossDb;
    if (installedEirp > 16.15 + 1e-6) errors.push(`${device.id} exceeds 14 dBm ERP (16.15 dBm EIRP)`);
    if (device.radio.relayHopLimit < 0 || device.radio.relayHopLimit > 4) errors.push(`${device.id} has an invalid hop limit`);
    if (!Number.isFinite(device.x) || !Number.isFinite(device.y)) errors.push(`${device.id} has invalid coordinates`);
    if (!(device.antennaHeightM > 0 && device.antennaHeightM <= 100) ||
        !Number.isFinite(device.antennaGainDbi) || !Number.isFinite(device.cableLossDb)) errors.push(`${device.id} has invalid antenna properties`);
    if (device.role === 'tracker') {
      const c = device.config ?? {};
      if (!device.waypoints || device.waypoints.length < 1) errors.push(`${device.id} needs a waypoint`);
      else if (device.waypoints.some((point) => !Number.isFinite(point.x) || !Number.isFinite(point.y))) errors.push(`${device.id} has invalid waypoint coordinates`);
      if (!(device.speedKmh >= 0 && device.speedKmh <= 100)) errors.push(`${device.id} has an invalid speed`);
      if (!(c.movingSleepS >= 10 && c.movingSleepS <= 86400 &&
            c.stationarySleepS >= c.movingSleepS && c.stationarySleepS <= 86400 &&
            c.longStationarySleepS >= c.stationarySleepS && c.longStationarySleepS <= 86400)) errors.push(`${device.id} has invalid sleep policy`);
      if (!Array.isArray(c.noFixSleepS) || c.noFixSleepS.length !== 4 ||
          c.noFixSleepS.some((value, index) => value < 10 || value > 86400 || (index && value < c.noFixSleepS[index - 1]))) errors.push(`${device.id} has invalid no-fix backoff`);
      if (!Array.isArray(c.retryBackoffS) || c.retryBackoffS.length !== 4 ||
          c.retryBackoffS.some((value, index) => value < 10 || value > 86400 || (index && value < c.retryBackoffS[index - 1]))) errors.push(`${device.id} has invalid radio retry backoff`);
      if (!(c.txIntervalS >= 10 && c.txIntervalS <= 86400 && c.txMinPoints >= 1 && c.txMinPoints <= 100 &&
            c.ackTimeoutMs >= 100 && c.ackTimeoutMs <= 30000)) errors.push(`${device.id} has invalid transmit policy`);
      if (!(c.historyPointSpacingM >= 1 && c.historyPointSpacingM <= 1000 && c.minSatellites >= 3 && c.minSatellites <= 64 &&
            c.maxHdop >= 0.5 && c.maxHdop <= 20)) errors.push(`${device.id} has invalid GNSS policy`);
      if (!(c.batteryCapacityMah > 0 && c.batteryCapacityMah <= 100000 && c.sleepCurrentMa >= 0 &&
            c.gnssCurrentMa >= 0 && c.txCurrentMa >= 0)) errors.push(`${device.id} has invalid battery model`);
    } else if (device.role === 'repeater') {
      const c = device.config ?? {};
      if (!(c.forwardingBaseDelayMs >= 10 && c.forwardingBaseDelayMs <= 2000 &&
            c.forwardingSlotWidthMs >= 5 && c.forwardingSlotWidthMs <= 1000 &&
            c.forwardingSlotCount >= 1 && c.forwardingSlotCount <= 32 &&
            c.duplicateCacheTtlS >= 10 && c.duplicateCacheTtlS <= 3600 &&
            c.airtimeBudgetMsPerHour >= 1000 && c.airtimeBudgetMsPerHour <= 36000 &&
            maximumAirtimeMs < c.airtimeBudgetMsPerHour)) errors.push(`${device.id} has invalid relay policy`);
    } else if (device.role === 'receiver') {
      const c = device.config ?? {};
      if (typeof c.mqttConnected !== 'boolean' || !Array.isArray(c.registeredTrackerIds) ||
          c.registeredTrackerIds.some((id) => typeof id !== 'string')) errors.push(`${device.id} has invalid gateway registry`);
    }
  }
  const trackerIds = new Set((scenario.devices ?? [])
    .filter((device) => device.role === 'tracker').map((device) => device.id));
  for (const receiver of (scenario.devices ?? []).filter((device) => device.role === 'receiver')) {
    const registry = receiver.config?.registeredTrackerIds ?? [];
    if (registry.length > 12 || new Set(registry).size !== registry.length ||
        registry.some((id) => !trackerIds.has(id))) errors.push(`${receiver.id} registry contains duplicate or unknown trackers`);
  }
  for (const obstacle of scenario.obstacles ?? []) {
    if (!obstacle.id || ids.has(obstacle.id)) errors.push(`Duplicate or missing obstacle ID ${obstacle.id ?? ''}`);
    ids.add(obstacle.id);
    if (!['forest', 'tree', 'building-small', 'building-large'].includes(obstacle.type)) errors.push(`${obstacle.id} has an invalid obstacle type`);
    if (obstacle.type === 'tree' && (!Number.isFinite(obstacle.x) || !Number.isFinite(obstacle.y) || !(obstacle.radius > 0 && obstacle.radius <= 100))) errors.push(`${obstacle.id} has invalid tree geometry`);
    if (obstacle.type !== 'tree' && (!Array.isArray(obstacle.points) || obstacle.points.length < 3 ||
        obstacle.points.some((point) => !Number.isFinite(point.x) || !Number.isFinite(point.y)))) errors.push(`${obstacle.id} has an invalid polygon`);
    if (obstacle.type === 'forest' && !(obstacle.density >= 0 && obstacle.density <= 1)) errors.push(`${obstacle.id} has invalid density`);
  }
  if (!(scenario.devices ?? []).some((d) => d.role === 'receiver')) errors.push('At least one receiver is required');
  return errors;
}

function initialRuntime(device, core) {
  const ackAirtimeMs = core.airtimeMs(78, device.radio);
  const radioCapacity = core.airtimeMs(255, device.radio) +
    (device.role === 'repeater' ? ackAirtimeMs : 0);
  return {
    nextWakeS: device.role === 'tracker' ? 0 : null,
    waypointIndex: 0,
    direction: 1,
    lastPoint: { x: device.x, y: device.y },
    stationaryStreak: 0,
    noFixCycles: 0,
    failures: 0,
    secondsSinceAck: 86400,
    secondsSinceAttempt: 86400,
    queue: [],
    nextSeq: 0,
    counter: 0,
    bootId: 1,
    airtimeTokensMs: 0,
    airtimeCapacityMs: radioCapacity,
    seen: {},
    pending: {},
    ackReservations: {},
    inflight: {},
    rxWindowUntilS: null,
    batteryMah: device.role === 'tracker'
      ? device.config.batteryCapacityMah * (device.batteryPct ?? 100) / 100
      : null,
    status: device.role === 'tracker' ? 'sleeping' : 'listening',
    stats: { tx: 0, rx: 0, lost: 0, relayed: 0, archived: 0, collisions: 0 },
  };
}

export class SimulationEngine {
  constructor(scenario, core) {
    this.scenario = deepCopy(scenario);
    this.core = core;
    this.timeS = 0;
    this.events = [];
    this.tasks = [];
    this.transmissions = [];
    this.archive = new Map();
    this.trackHistory = new Map();
    this.eventId = 0;
    this.taskId = 0;
    this.devices = new Map(this.scenario.devices.map((device) => {
      const copy = { ...deepCopy(device), runtime: initialRuntime(device, core) };
      const hash = hashText(device.id);
      copy.hashHi = hashText(`hi:${device.id}`);
      copy.hashLo = hash;
      return [copy.id, copy];
    }));
    this.log('simulation', 'Simulation ready', { severity: 'info' });
  }

  log(type, message, details = {}) {
    const event = { id: ++this.eventId, timeS: this.timeS, type, message, ...details };
    this.events.push(event);
    if (this.events.length > 600) this.events.splice(0, this.events.length - 600);
    return event;
  }

  schedule(delayS, type, payload) {
    this.tasks.push({ id: ++this.taskId, dueS: this.timeS + Math.max(0.001, delayS), type, payload });
  }

  updateScenario(patch) {
    if (patch.environment) Object.assign(this.scenario.environment, patch.environment);
    if (patch.mqtt) Object.assign(this.scenario.mqtt, patch.mqtt);
    if (patch.clock) Object.assign(this.scenario.clock, patch.clock);
    if (patch.map) Object.assign(this.scenario.map, patch.map);
    if (patch.world) Object.assign(this.scenario.world, patch.world);
  }

  replaceScenario(scenario) {
    return new SimulationEngine(scenario, this.core);
  }

  addEntity(entity) {
    if (entity.role) {
      const device = deepCopy(entity);
      this.scenario.devices.push(device);
      const runtimeDevice = { ...deepCopy(device), runtime: initialRuntime(device, this.core) };
      runtimeDevice.hashHi = hashText(`hi:${device.id}`);
      runtimeDevice.hashLo = hashText(device.id);
      this.devices.set(device.id, runtimeDevice);
    } else {
      this.scenario.obstacles.push(deepCopy(entity));
    }
    this.log('configuration', `Added ${entity.name ?? entity.label ?? entity.type}`, { deviceId: entity.id });
  }

  updateEntity(id, changes) {
    const device = this.devices.get(id);
    if (device) {
      Object.assign(device, deepCopy(changes));
      const source = this.scenario.devices.find((item) => item.id === id);
      if (source) Object.assign(source, deepCopy(changes));
    } else {
      const obstacle = this.scenario.obstacles.find((item) => item.id === id);
      if (obstacle) Object.assign(obstacle, deepCopy(changes));
    }
  }

  addWaypoint(id, point) {
    const device = this.devices.get(id);
    if (!device || device.role !== 'tracker') return;
    device.waypoints.push(point);
    const source = this.scenario.devices.find((item) => item.id === id);
    source.waypoints.push(deepCopy(point));
    this.log('configuration', `Waypoint added to ${device.name}`, { deviceId: id });
  }

  removeEntity(id) {
    const device = this.devices.get(id);
    if (device) {
      this.devices.delete(id);
      this.scenario.devices = this.scenario.devices.filter((item) => item.id !== id);
      if (device.role === 'tracker') for (const receiver of this.scenario.devices.filter((item) => item.role === 'receiver')) receiver.config.registeredTrackerIds = receiver.config.registeredTrackerIds.filter((trackerId) => trackerId !== id);
    } else this.scenario.obstacles = this.scenario.obstacles.filter((item) => item.id !== id);
    this.log('configuration', `Removed ${id}`, { deviceId: id });
  }

  updateWaypoint(id, index, point) {
    const device = this.devices.get(id);
    if (!device || device.role !== 'tracker' || !device.waypoints[index]) return;
    device.waypoints[index] = deepCopy(point);
    const source = this.scenario.devices.find((item) => item.id === id);
    source.waypoints[index] = deepCopy(point);
  }

  removeWaypoint(id, index) {
    const device = this.devices.get(id);
    if (!device || device.role !== 'tracker') return;
    device.waypoints.splice(index, 1);
    const source = this.scenario.devices.find((item) => item.id === id);
    source.waypoints.splice(index, 1);
  }

  advance(seconds = 1) {
    const steps = clamp(Math.floor(seconds), 1, 3600);
    for (let step = 0; step < steps; step += 1) {
      this.timeS += 1;
      this.updateEnvironmentAndBudgets();
      this.moveTrackers(1);
      this.processTasks();
      for (const device of this.devices.values()) {
        if (device.role === 'tracker' && device.runtime.nextWakeS <= this.timeS) this.wakeTracker(device);
      }
      this.transmissions = this.transmissions.filter((tx) => tx.endS + 2 >= this.timeS);
    }
    return this.snapshot();
  }

  updateEnvironmentAndBudgets() {
    for (const device of this.devices.values()) {
      const runtime = device.runtime;
      const legalBudget = device.role === 'repeater'
        ? device.config.airtimeBudgetMsPerHour : 36_000;
      const refillBudget = Math.max(0, legalBudget - runtime.airtimeCapacityMs);
      runtime.airtimeTokensMs = Math.min(runtime.airtimeCapacityMs,
        runtime.airtimeTokensMs + refillBudget / 3600);
      if (device.role === 'tracker') {
        runtime.secondsSinceAck += 1;
        runtime.secondsSinceAttempt += 1;
        runtime.batteryMah = Math.max(0, runtime.batteryMah - device.config.sleepCurrentMa / 3600);
      }
      for (const [key, expires] of Object.entries(runtime.seen)) {
        if (expires <= this.timeS) delete runtime.seen[key];
      }
      for (const [key, reservation] of Object.entries(runtime.ackReservations)) {
        if (reservation.expiresS > this.timeS) continue;
        runtime.airtimeTokensMs = Math.min(runtime.airtimeCapacityMs,
          runtime.airtimeTokensMs + reservation.airtimeMs);
        delete runtime.ackReservations[key];
      }
    }
  }

  moveTrackers(deltaS) {
    for (const tracker of this.devices.values()) {
      if (tracker.role !== 'tracker' || tracker.waypoints.length < 2 || tracker.speedKmh <= 0) continue;
      let remaining = tracker.speedKmh / 3.6 * deltaS;
      while (remaining > 0) {
        const next = tracker.waypoints[tracker.runtime.waypointIndex];
        const gap = distance(tracker, next);
        if (gap <= 0.01) {
          this.advanceWaypoint(tracker);
          continue;
        }
        const move = Math.min(gap, remaining);
        tracker.x += (next.x - tracker.x) / gap * move;
        tracker.y += (next.y - tracker.y) / gap * move;
        remaining -= move;
        if (move >= gap - 0.01) this.advanceWaypoint(tracker);
      }
    }
  }

  advanceWaypoint(tracker) {
    const max = tracker.waypoints.length - 1;
    if (tracker.pathMode === 'reverse') {
      if (tracker.runtime.waypointIndex >= max) tracker.runtime.direction = -1;
      if (tracker.runtime.waypointIndex <= 0) tracker.runtime.direction = 1;
      tracker.runtime.waypointIndex += tracker.runtime.direction;
    } else if (tracker.pathMode === 'stop' && tracker.runtime.waypointIndex >= max) {
      tracker.speedKmh = 0;
    } else {
      tracker.runtime.waypointIndex = (tracker.runtime.waypointIndex + 1) % tracker.waypoints.length;
    }
  }

  localGnssQuality(tracker) {
    let quality = 0.96;
    for (const obstacle of this.scenario.obstacles) {
      if (obstacle.type === 'forest' && pointInObstacle(tracker, obstacle)) quality -= 0.22 * obstacle.density;
      if (obstacle.type.startsWith('building') && pointInObstacle(tracker, obstacle)) quality -= 0.55;
      if (obstacle.type === 'tree' && distance(tracker, obstacle) < obstacle.radius * 2) quality -= 0.05;
    }
    return clamp(quality, 0.08, 0.99);
  }

  deterministicChance(label) {
    return hashText(`${this.scenario.seed}:${label}`) / 0xffffffff;
  }

  wakeTracker(tracker) {
    const r = tracker.runtime;
    r.status = 'gnss';
    const quality = this.localGnssQuality(tracker);
    const fixFound = this.deterministicChance(`gnss:${tracker.id}:${this.timeS}`) < quality;
    const movedM = distance(tracker, r.lastPoint);
    const movementAccepted = fixFound && movedM >= tracker.config.historyPointSpacingM;
    const acquisitionS = fixFound ? 3 + Math.round((1 - quality) * 18) : 30;
    r.batteryMah = Math.max(0, r.batteryMah - tracker.config.gnssCurrentMa * acquisitionS / 3600);
    this.log('gnss', fixFound
      ? `${tracker.name} acquired a GNSS fix in ${acquisitionS} s`
      : `${tracker.name} timed out waiting for GNSS`, {
      deviceId: tracker.id, x: tracker.x, y: tracker.y,
      severity: fixFound ? 'success' : 'warning', acquisitionS,
    });

    if (fixFound) {
      r.noFixCycles = 0;
      if (movementAccepted || r.queue.length === 0) {
        const point = { seq: r.nextSeq++, x: tracker.x, y: tracker.y, timeS: this.timeS,
          batteryPct: Math.round(r.batteryMah / tracker.config.batteryCapacityMah * 100) };
        r.queue.push(point);
        const history = this.trackHistory.get(tracker.id) ?? [];
        history.push(deepCopy(point));
        this.trackHistory.set(tracker.id, history.slice(-2000));
        r.lastPoint = { x: tracker.x, y: tracker.y };
        r.stationaryStreak = movementAccepted ? 0 : r.stationaryStreak + 1;
        this.log('queue', `${tracker.name} queued point #${point.seq}`, {
          deviceId: tracker.id, queueDepth: r.queue.length, severity: 'info',
        });
      } else {
        r.stationaryStreak += 1;
      }
    } else {
      r.noFixCycles = Math.min(255, r.noFixCycles + 1);
      r.stationaryStreak = 0;
    }

    const retryReady = r.failures === 0 || r.secondsSinceAttempt >= this.core.retrySeconds(tracker.config, r.failures);
    let transmitted = false;
    if (this.core.batchDue(tracker.config, r.queue.length, r.secondsSinceAck) && retryReady) {
      transmitted = this.transmitHistory(tracker);
    }
    const sleepS = this.core.sleepSeconds(tracker.config, fixFound, movementAccepted,
      r.stationaryStreak, r.noFixCycles);
    r.nextWakeS = this.timeS + sleepS;
    if (!transmitted) r.status = 'sleeping';
    this.log('power', transmitted
      ? `${tracker.name} will sleep after its ACK window closes`
      : `${tracker.name} sleeps for ${sleepS} s`, {
      deviceId: tracker.id, sleepS, severity: 'info',
    });
  }

  transmitHistory(tracker) {
    const r = tracker.runtime;
    const points = r.queue.slice(0, 32);
    const bytes = Math.min(255, 79 + points.length * 5);
    const airtimeMs = this.core.airtimeMs(bytes, tracker.radio);
    if (r.airtimeTokensMs < airtimeMs) {
      this.log('duty-cycle', `${tracker.name} deferred transmission by the Germany 1% limiter`, {
        deviceId: tracker.id, neededMs: airtimeMs,
        availableMs: Math.floor(r.airtimeTokensMs), severity: 'warning',
      });
      return false;
    }
    r.airtimeTokensMs -= airtimeMs;
    r.secondsSinceAttempt = 0;
    const counter = r.counter++;
    const frame = {
      key: `${tracker.id}:1:${counter}:history`, type: 'history', messageType: 1,
      schemaVersion: 2, deviceId: tracker.id,
      deviceHashHi: tracker.hashHi, deviceHashLo: tracker.hashLo,
      bootId: r.bootId, counter, transactionCounter: counter, hop: 0,
      hopLimit: tracker.radio.relayHopLimit, route: [], routeCursor: 0,
      points, bytes,
    };
    const deadlineS = this.timeS + tracker.config.ackTimeoutMs / 1000;
    r.inflight[counter] = {
      acked: false, active: true, lastSeq: points.at(-1).seq, deadlineS,
    };
    r.rxWindowUntilS = deadlineS;
    r.batteryMah = Math.max(0, r.batteryMah - tracker.config.txCurrentMa * airtimeMs / 3_600_000);
    this.startTransmission(tracker, frame, airtimeMs);
    this.schedule(tracker.config.ackTimeoutMs / 1000, 'ack-timeout', { trackerId: tracker.id, counter });
    return true;
  }

  startTransmission(sender, frame, airtimeMs) {
    const transmission = {
      id: `tx-${++this.taskId}`, senderId: sender.id, frame: deepCopy(frame),
      startS: this.timeS, endS: this.timeS + airtimeMs / 1000,
    };
    this.transmissions.push(transmission);
    sender.runtime.stats.tx += 1;
    sender.runtime.status = 'transmitting';
    this.log('radio-tx', `${sender.name} transmitted ${frame.type.toUpperCase()} (${airtimeMs} ms)`, {
      deviceId: sender.id, transmissionId: transmission.id, frameKey: frame.key,
      from: { x: sender.x, y: sender.y }, airtimeMs, hop: frame.hop, severity: 'radio',
    });
    for (const receiver of this.devices.values()) {
      if (receiver.id === sender.id || !this.sameModem(sender.radio, receiver.radio)) continue;
      const link = calculateLink(this.scenario, this.core, sender, receiver, this.timeS, frame.key);
      this.schedule(Math.max(0.001, airtimeMs / 1000), 'radio-receive', {
        transmissionId: transmission.id, receiverId: receiver.id, link,
      });
    }
  }

  sameModem(a, b) {
    return a.frequencyHz === b.frequencyHz && a.bandwidthHz === b.bandwidthHz &&
      a.spreadingFactor === b.spreadingFactor && a.codingRateDenominator === b.codingRateDenominator &&
      a.syncWord === b.syncWord;
  }

  processTasks() {
    const horizonS = this.timeS;
    while (this.tasks.length) {
      this.tasks.sort((a, b) => a.dueS - b.dueS || a.id - b.id);
      if (this.tasks[0].dueS > horizonS) break;
      const task = this.tasks.shift();
      // Preserve sub-second radio/relay ordering even though movement and power
      // advance on one-second ticks. This prevents artificial collisions caused
      // by rounding every forwarding slot and packet airtime to a whole second.
      this.timeS = task.dueS;
      if (task.type === 'radio-receive') this.receiveTransmission(task.payload);
      else if (task.type === 'relay-forward') this.forwardRelay(task.payload);
      else if (task.type === 'archive') this.archiveFrame(task.payload);
      else if (task.type === 'send-ack') this.sendArchiveAck(
        this.devices.get(task.payload.gatewayId), task.payload.frame);
      else if (task.type === 'ack-timeout') this.ackTimeout(task.payload);
    }
    this.timeS = horizonS;
  }

  receiveTransmission({ transmissionId, receiverId, link }) {
    const tx = this.transmissions.find((item) => item.id === transmissionId);
    const receiver = this.devices.get(receiverId);
    if (!tx || !receiver) return;
    if (receiver.role === 'tracker' && tx.frame.type === 'ack') {
      const inflight = receiver.runtime.inflight[tx.frame.transactionCounter];
      if (!inflight?.active || tx.endS > inflight.deadlineS ||
          tx.endS > (receiver.runtime.rxWindowUntilS ?? -Infinity)) {
        this.log('radio-sleep', `${receiver.name} did not receive the late ACK because its radio was asleep`, {
          deviceId: receiver.id, frameKey: tx.frame.key,
          deadlineS: inflight?.deadlineS, completedAtS: tx.endS, severity: 'warning',
        });
        return;
      }
    }
    const ownTransmission = this.transmissions.find((other) =>
      other.senderId === receiver.id && other.startS < tx.endS && other.endS > tx.startS);
    if (ownTransmission) {
      receiver.runtime.stats.lost += 1;
      this.log('half-duplex', `${receiver.name} could not receive while transmitting`, {
        deviceId: receiver.id, frameKey: tx.frame.key, link, severity: 'warning',
      });
      return;
    }
    const overlapping = this.transmissions.filter((other) => other.id !== tx.id &&
      other.startS < tx.endS && other.endS > tx.startS &&
      this.sameModem(this.devices.get(other.senderId).radio, receiver.radio))
      .map((other) => ({ other, link: calculateLink(this.scenario, this.core,
        this.devices.get(other.senderId), receiver, tx.startS, other.frame.key) }))
      .filter((item) => item.link.rxPowerDbm >= item.link.sensitivityDbm);
    const strongestInterferer = overlapping.sort((a, b) => b.link.rxPowerDbm - a.link.rxPowerDbm)[0];
    if (strongestInterferer && link.rxPowerDbm - strongestInterferer.link.rxPowerDbm < 6) {
      receiver.runtime.stats.collisions += 1;
      receiver.runtime.stats.lost += 1;
      this.log('collision', `${receiver.name} lost ${tx.frame.type.toUpperCase()} in a collision`, {
        deviceId: receiver.id, frameKey: tx.frame.key, link, severity: 'error',
      });
      return;
    }
    if (link.marginDb < 0) {
      receiver.runtime.stats.lost += 1;
      this.log('radio-loss', `${tx.frame.type.toUpperCase()} was below ${receiver.name}'s sensitivity`, {
        deviceId: receiver.id, frameKey: tx.frame.key, link, severity: 'warning',
      });
      return;
    }
    receiver.runtime.stats.rx += 1;
    this.log('radio-rx', `${receiver.name} received ${tx.frame.type.toUpperCase()} at ${link.rxPowerDbm.toFixed(1)} dBm`, {
      deviceId: receiver.id, frameKey: tx.frame.key, link, hop: tx.frame.hop, severity: 'success',
    });
    this.handleFrame(receiver, tx.frame);
  }

  handleFrame(receiver, frame) {
    if (receiver.role === 'repeater') return this.queueRelay(receiver, frame);
    if (receiver.role === 'receiver' && frame.type === 'history') return this.gatewayHistory(receiver, frame);
    if (receiver.role === 'tracker' && frame.type === 'ack' && frame.deviceId === receiver.id) return this.trackerAck(receiver, frame);
  }

  queueRelay(repeater, frame) {
    const r = repeater.runtime;
    if (r.seen[frame.key] > this.timeS || frame.hop >= frame.hopLimit) return;
    if (frame.type === 'ack' &&
        frame.route?.[frame.routeCursor - 1] !== repeater.id) return;
    const outgoingHop = frame.hop + 1;
    const existing = r.pending[frame.key];
    if (existing && frame.hop >= existing.outgoingHop) {
      existing.cancelled = true;
      this.log('relay-suppress', `${repeater.name} suppressed a redundant forward`, {
        deviceId: repeater.id, frameKey: frame.key, severity: 'info',
      });
      return;
    }
    const packetAirtimeMs = this.core.airtimeMs(frame.bytes, repeater.radio);
    const safeRepeater = { ...repeater, config: { ...repeater.config,
      forwardingSlotWidthMs: Math.max(
        repeater.config.forwardingSlotWidthMs, packetAirtimeMs + 50),
    } };
    const delayMs = frame.type === 'ack'
      ? repeater.config.forwardingBaseDelayMs
      : this.core.forwardingDelayMs(frame, safeRepeater);
    const pending = { frame: deepCopy(frame), outgoingHop, cancelled: false };
    r.pending[frame.key] = pending;
    this.schedule(delayMs / 1000, 'relay-forward', { repeaterId: repeater.id, frameKey: frame.key });
    this.log('relay-queue', `${repeater.name} scheduled hop ${outgoingHop} in ${delayMs} ms`, {
      deviceId: repeater.id, frameKey: frame.key, delayMs, severity: 'info',
    });
  }

  forwardRelay({ repeaterId, frameKey }) {
    const repeater = this.devices.get(repeaterId);
    const pending = repeater?.runtime.pending[frameKey];
    if (!repeater || !pending || pending.cancelled) return;
    delete repeater.runtime.pending[frameKey];
    const frame = { ...pending.frame, hop: pending.outgoingHop };
    if (frame.type === 'history') frame.route = [...(frame.route ?? []), repeater.id];
    else frame.routeCursor -= 1;
    const airtimeMs = this.core.airtimeMs(frame.bytes, repeater.radio);
    const reservationKey = `${frame.deviceId}:${frame.bootId}:${frame.transactionCounter}`;
    const reservation = repeater.runtime.ackReservations[reservationKey];
    const ackAirtimeMs = this.core.airtimeMs(78, repeater.radio);
    const requiredAirtimeMs = frame.type === 'history'
      ? airtimeMs + ackAirtimeMs
      : (reservation ? 0 : airtimeMs);
    if (repeater.runtime.airtimeTokensMs < requiredAirtimeMs) {
      this.log('duty-cycle', `${repeater.name} dropped the incomplete relay transaction and awaits a tracker retry`, {
        deviceId: repeater.id, frameKey, severity: 'warning',
      });
      return;
    }
    repeater.runtime.airtimeTokensMs -= requiredAirtimeMs;
    if (frame.type === 'history') {
      repeater.runtime.ackReservations[reservationKey] = {
        airtimeMs: ackAirtimeMs, expiresS: this.timeS + 35,
      };
    } else if (reservation) {
      delete repeater.runtime.ackReservations[reservationKey];
    }
    repeater.runtime.seen[frame.key] = this.timeS +
      (frame.type === 'ack' ? 5 : repeater.config.duplicateCacheTtlS);
    repeater.runtime.stats.relayed += 1;
    this.startTransmission(repeater, frame, airtimeMs);
  }

  gatewayHistory(gateway, frame) {
    if (!gateway.config.registeredTrackerIds.includes(frame.deviceId)) {
      this.log('gateway', `${gateway.name} rejected unregistered tracker ${frame.deviceId}`, {
        deviceId: gateway.id, frameKey: frame.key, severity: 'warning',
      });
      return;
    }
    this.log('gateway', `${gateway.name} authenticated and decoded ${frame.points.length} points`, {
      deviceId: gateway.id, frameKey: frame.key, severity: 'success',
    });
    if (!this.scenario.mqtt.online || !gateway.config.mqttConnected) {
      this.log('mqtt', `${gateway.name} withheld ACK because MQTT is offline`, {
        deviceId: gateway.id, frameKey: frame.key, severity: 'warning',
      });
      return;
    }
    this.log('mqtt', `${gateway.name} published ${frame.points.length} point events`, {
      deviceId: gateway.id, frameKey: frame.key, qos: 0, severity: 'info',
    });
    this.schedule((this.scenario.mqtt.latencyMs + this.scenario.mqtt.archiveLatencyMs) / 1000,
      'archive', { gatewayId: gateway.id, frame: deepCopy(frame), receivedAtS: this.timeS });
  }

  archiveFrame({ gatewayId, frame, receivedAtS }) {
    const gateway = this.devices.get(gatewayId);
    let inserted = 0;
    for (const point of frame.points) {
      const key = `${frame.deviceId}:${frame.bootId}:${point.seq}`;
      if (!this.archive.has(key)) { this.archive.set(key, { ...point, deviceId: frame.deviceId, gatewayId, archivedAtS: this.timeS }); inserted += 1; }
    }
    gateway.runtime.stats.archived += inserted;
    this.log('archive', `MQTT archive committed ${inserted} new point${inserted === 1 ? '' : 's'}`, {
      deviceId: gateway.id, frameKey: frame.key, inserted, severity: 'success',
    });
    const guardMs = this.core.ackRelayGuardMs(
      frame.bytes, gateway.radio, frame.hop, frame.hopLimit);
    const remainingGuardS = Math.max(0, receivedAtS + guardMs / 1000 - this.timeS);
    if (remainingGuardS > 0.001) {
      this.log('ack-guard', `${gateway.name} waits ${Math.ceil(remainingGuardS * 1000)} ms for relay airtime to clear`, {
        deviceId: gateway.id, frameKey: frame.key, severity: 'info',
      });
      this.schedule(remainingGuardS, 'send-ack', { gatewayId, frame });
    } else {
      this.sendArchiveAck(gateway, frame);
    }
  }

  sendArchiveAck(gateway, frame) {
    const tracker = this.devices.get(frame.deviceId);
    if (!tracker || frame.points.length === 0) return;
    const ackSeq = frame.points.at(-1).seq;
    const ackCounter = ackSeq;
    const ack = {
      key: `${frame.deviceId}:${frame.bootId}:${ackCounter}:ack`, type: 'ack', messageType: 2,
      schemaVersion: 1, deviceId: frame.deviceId,
      deviceHashHi: frame.deviceHashHi, deviceHashLo: frame.deviceHashLo,
      bootId: frame.bootId, counter: ackCounter,
      transactionCounter: frame.transactionCounter, hop: 0,
      hopLimit: frame.route?.length ?? 0, route: [...(frame.route ?? [])],
      routeCursor: frame.route?.length ?? 0, ackSeq, bytes: 78,
    };
    const airtimeMs = this.core.airtimeMs(ack.bytes, gateway.radio);
    if (gateway.runtime.airtimeTokensMs < airtimeMs) {
      this.log('duty-cycle', `${gateway.name} withheld ACK pending legal airtime`, {
        deviceId: gateway.id, frameKey: frame.key, severity: 'warning',
      });
      return;
    }
    gateway.runtime.airtimeTokensMs -= airtimeMs;
    this.startTransmission(gateway, ack, airtimeMs);
  }

  trackerAck(tracker, frame) {
    const r = tracker.runtime;
    const inflight = r.inflight[frame.transactionCounter];
    if (!inflight?.active || this.timeS > inflight.deadlineS) {
      this.log('late-ack', `${tracker.name} ignored an ACK outside its receive window`, {
        deviceId: tracker.id, frameKey: frame.key, severity: 'warning',
      });
      return;
    }
    const before = r.queue.length;
    r.queue = r.queue.filter((point) => point.seq > frame.ackSeq);
    for (const entry of Object.values(r.inflight)) {
      if (entry.lastSeq <= frame.ackSeq) {
        entry.acked = true;
        entry.active = false;
      }
    }
    r.rxWindowUntilS = null;
    r.status = 'sleeping';
    r.failures = 0;
    r.secondsSinceAck = 0;
    r.secondsSinceAttempt = 0;
    this.log('ack', `${tracker.name} accepted ACK through point #${frame.ackSeq}`, {
      deviceId: tracker.id, removed: before - r.queue.length,
      queueDepth: r.queue.length, severity: 'success',
    });
  }

  ackTimeout({ trackerId, counter }) {
    const tracker = this.devices.get(trackerId);
    const inflight = tracker?.runtime.inflight[counter];
    if (!tracker || !inflight || inflight.acked) return;
    inflight.active = false;
    tracker.runtime.rxWindowUntilS = null;
    tracker.runtime.status = 'sleeping';
    tracker.runtime.failures = Math.min(255, tracker.runtime.failures + 1);
    this.log('ack-timeout', `${tracker.name} did not receive an archive-backed ACK`, {
      deviceId: tracker.id, failures: tracker.runtime.failures,
      retryS: this.core.retrySeconds(tracker.config, tracker.runtime.failures), severity: 'warning',
    });
  }

  snapshot() {
    const environment = environmentAt(this.scenario, this.timeS);
    const devices = [...this.devices.values()];
    const links = [];
    for (const from of devices) for (const to of devices) if (from.id !== to.id && this.sameModem(from.radio, to.radio)) {
      links.push({ fromId: from.id, toId: to.id, ...calculateLink(this.scenario, this.core, from, to, this.timeS, `inspection:${from.id}:${to.id}`) });
    }
    return {
      timeS: this.timeS,
      environment,
      scenario: deepCopy({ ...this.scenario, devices: devices.map((device) => {
        const { runtime, ...plain } = device;
        return plain;
      }) }),
      devices: devices.map((device) => ({
        ...deepCopy(device),
        batteryPct: device.role === 'tracker'
          ? clamp(device.runtime.batteryMah / device.config.batteryCapacityMah * 100, 0, 100)
          : null,
      })),
      events: deepCopy(this.events.slice(-160)),
      transmissions: deepCopy(this.transmissions.filter((tx) => tx.endS + 1 >= this.timeS)),
      trackHistory: Object.fromEntries([...this.trackHistory].map(([id, points]) => [id, deepCopy(points)])),
      archive: deepCopy([...this.archive.values()]),
      links,
      mqtt: { ...this.scenario.mqtt, archivedPoints: this.archive.size },
      core: { kind: this.core.kind, version: this.core.version },
    };
  }
}
