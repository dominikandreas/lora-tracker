import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import test from 'node:test';

import { FirmwareCore, ReferenceCore } from '../app/firmware-core.js';

async function loadWasm() {
  const bytes = await readFile(new URL('../app/firmware-core.wasm', import.meta.url));
  const { instance } = await WebAssembly.instantiate(bytes, { env: {} });
  return new FirmwareCore(instance);
}

test('WASM executes the same golden policy vectors as the reference contracts', async () => {
  const wasm = await loadWasm();
  const reference = new ReferenceCore();
  const radios = [
    { frequencyHz: 868_100_000, bandwidthHz: 125_000, txPowerDbm: 14, spreadingFactor: 7, codingRateDenominator: 5, preambleSymbols: 8 },
    { frequencyHz: 868_100_000, bandwidthHz: 125_000, txPowerDbm: 14, spreadingFactor: 10, codingRateDenominator: 5, preambleSymbols: 8 },
    { frequencyHz: 868_300_000, bandwidthHz: 500_000, txPowerDbm: 10, spreadingFactor: 12, codingRateDenominator: 8, preambleSymbols: 12 },
  ];
  for (const radio of radios) {
    assert.equal(wasm.validateRadio(radio), reference.validateRadio(radio));
    for (const bytes of [1, 56, 128, 255]) assert.equal(wasm.airtimeMs(bytes, radio), reference.airtimeMs(bytes, radio));
    assert.ok(Math.abs(wasm.sensitivityDbm(radio) - reference.sensitivityDbm(radio)) < 1e-9);
    assert.equal(wasm.ackRelayGuardMs(255, radio, 0, 2), reference.ackRelayGuardMs(255, radio, 0, 2));
  }
  assert.equal(wasm.validateRadio({ ...radios[0], spreadingFactor: 263 }), false);
  assert.equal(wasm.validateRadio({ ...radios[0], txPowerDbm: 270 }), false);
  const config = {
    movingSleepS: 60, stationarySleepS: 300, longStationarySleepS: 600,
    noFixSleepS: [120, 300, 600, 900], stationaryFixesForLongSleep: 3,
    stationaryFixesForMaxSleep: 12, retryBackoffS: [60, 120, 300, 600],
    txIntervalS: 300, txMinPoints: 3,
  };
  for (const vector of [[true, true, 0, 0], [true, false, 3, 0], [true, false, 12, 0], [false, false, 0, 4]]) {
    assert.equal(wasm.sleepSeconds(config, ...vector), reference.sleepSeconds(config, ...vector));
  }
  for (const failures of [0, 1, 2, 3, 9]) assert.equal(wasm.retrySeconds(config, failures), reference.retrySeconds(config, failures));
  assert.equal(wasm.batchDue(config, 3, 0), true);
  assert.equal(wasm.batchDue(config, 1, 299), false);
  assert.equal(wasm.batchDue(config, 1, 300), true);
});
