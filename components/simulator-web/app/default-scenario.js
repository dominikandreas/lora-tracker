export const SCENARIO_VERSION = 2;

export const radioDefaults = () => ({
  frequencyHz: 868_100_000,
  bandwidthHz: 125_000,
  txPowerDbm: 14,
  spreadingFactor: 10,
  codingRateDenominator: 5,
  preambleSymbols: 8,
  syncWord: 0x12,
  relayHopLimit: 2,
});

export const trackerDefaults = () => ({
  movingSleepS: 60,
  stationarySleepS: 300,
  longStationarySleepS: 600,
  noFixSleepS: [120, 300, 600, 900],
  stationaryFixesForLongSleep: 3,
  stationaryFixesForMaxSleep: 12,
  txIntervalS: 300,
  txMinPoints: 3,
  ackTimeoutMs: 15_000,
  retryBackoffS: [60, 120, 300, 600],
  historyPointSpacingM: 15,
  minSatellites: 6,
  maxHdop: 2,
  batteryCapacityMah: 1000,
  sleepCurrentMa: 0.8,
  gnssCurrentMa: 28,
  txCurrentMa: 120,
});

export const repeaterDefaults = () => ({
  forwardingBaseDelayMs: 40,
  forwardingSlotWidthMs: 45,
  forwardingSlotCount: 8,
  duplicateCacheTtlS: 120,
  airtimeBudgetMsPerHour: 36_000,
});

function device(id, role, name, x, y, extra = {}) {
  return {
    id, role, name, x, y, antennaGainDbi: 2.15, cableLossDb: 0,
    antennaHeightM: role === 'tracker' ? 1.2 : 4,
    radio: radioDefaults(), ...extra,
  };
}

export function createDefaultScenario() {
  return {
    schemaVersion: SCENARIO_VERSION,
    name: 'Forest edge relay',
    seed: 0x51a7c0de,
    world: { minXM: 0, minYM: 0, widthM: 1000, heightM: 620, gridM: 50 },
    map: { mode: 'grid', centerLat: 51.1657, centerLng: 10.4515, zoom: 17, anchorX: 500, anchorY: 310 },
    clock: { startHour: 7, speed: 60 },
    environment: {
      dayTemperatureC: 24, nightTemperatureC: 10,
      dayHumidityPct: 48, nightHumidityPct: 88,
      foliageWetness: 0.35, siteLossDb: 0,
    },
    mqtt: { online: true, latencyMs: 350, archiveLatencyMs: 500 },
    devices: [
      device('tracker-1', 'tracker', 'Pasture tracker', 120, 430, {
        config: trackerDefaults(), speedKmh: 5,
        waypoints: [{ x: 120, y: 430 }, { x: 410, y: 360 }, { x: 690, y: 470 }, { x: 210, y: 520 }],
        pathMode: 'loop', batteryPct: 100,
      }),
      device('relay-1', 'repeater', 'Forest relay', 530, 250, {
        config: repeaterDefaults(), antennaHeightM: 6,
      }),
      device('gateway-1', 'receiver', 'Farm gateway', 870, 120, {
        antennaHeightM: 7,
        config: { mqttConnected: true, registeredTrackerIds: ['tracker-1'] },
      }),
    ],
    obstacles: [
      { id: 'forest-1', type: 'forest', points: [{ x: 330, y: 150 }, { x: 680, y: 150 }, { x: 660, y: 400 }, { x: 350, y: 380 }], density: 0.75, label: 'Mixed forest' },
      { id: 'building-1', type: 'building-large', points: [{ x: 730, y: 235 }, { x: 850, y: 235 }, { x: 850, y: 325 }, { x: 730, y: 325 }], label: 'Barn' },
      { id: 'building-2', type: 'building-small', points: [{ x: 90, y: 90 }, { x: 160, y: 90 }, { x: 160, y: 145 }, { x: 90, y: 145 }], label: 'Shed' },
      { id: 'tree-1', type: 'tree', x: 710, y: 180, radius: 9, label: 'Oak' },
      { id: 'tree-2', type: 'tree', x: 705, y: 205, radius: 8, label: 'Oak' },
    ],
  };
}
