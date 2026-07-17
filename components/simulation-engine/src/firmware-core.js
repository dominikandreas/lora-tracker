const RADIO_DEFAULTS = Object.freeze({
  frequencyHz: 868_100_000,
  bandwidthHz: 125_000,
  txPowerDbm: 14,
  spreadingFactor: 10,
  codingRateDenominator: 5,
  preambleSymbols: 8,
});

function exported(instance, name) {
  const fn = instance.exports[name] || instance.exports[`_${name}`];
  if (typeof fn !== 'function') throw new Error(`WASM export ${name} is missing`);
  return fn;
}

export class FirmwareCore {
  static async load(url = new URL('./firmware-core.wasm', import.meta.url)) {
    const response = await fetch(url);
    if (!response.ok) throw new Error(`Could not load firmware core (${response.status})`);
    const bytes = await response.arrayBuffer();
    const { instance } = await WebAssembly.instantiate(bytes, { env: {} });
    return new FirmwareCore(instance);
  }

  constructor(instance) {
    this.kind = 'wasm';
    this.version = exported(instance, 'lt_core_version')();
    this.fn = {
      validateRadio: exported(instance, 'lt_validate_germany_radio'),
      airtime: exported(instance, 'lt_airtime_ms'),
      ackRelayGuard: exported(instance, 'lt_ack_relay_guard_ms'),
      sensitivity: exported(instance, 'lt_receiver_sensitivity_dbm'),
      sleep: exported(instance, 'lt_tracker_sleep_s'),
      retry: exported(instance, 'lt_tracker_retry_s'),
      batchDue: exported(instance, 'lt_tracker_batch_due'),
      forwardingDelay: exported(instance, 'lt_forwarding_delay_ms'),
    };
  }

  validateRadio(radio) {
    const r = { ...RADIO_DEFAULTS, ...radio };
    return this.fn.validateRadio(r.frequencyHz, r.bandwidthHz, r.txPowerDbm,
      r.spreadingFactor, r.codingRateDenominator, r.preambleSymbols) === 1;
  }

  airtimeMs(bytes, radio) {
    const r = { ...RADIO_DEFAULTS, ...radio };
    return this.fn.airtime(bytes, r.frequencyHz, r.bandwidthHz, r.txPowerDbm,
      r.spreadingFactor, r.codingRateDenominator, r.preambleSymbols);
  }

  ackRelayGuardMs(bytes, radio, incomingHop, hopLimit) {
    const r = { ...RADIO_DEFAULTS, ...radio };
    return this.fn.ackRelayGuard(bytes, r.bandwidthHz, r.spreadingFactor,
      r.codingRateDenominator, r.preambleSymbols, incomingHop, hopLimit, 400);
  }

  sensitivityDbm(radio) {
    const r = { ...RADIO_DEFAULTS, ...radio };
    return this.fn.sensitivity(r.bandwidthHz, r.spreadingFactor, 6);
  }

  sleepSeconds(config, fixFound, moving, stationaryStreak, noFixCycles) {
    return this.fn.sleep(config.movingSleepS, config.stationarySleepS,
      config.longStationarySleepS, ...config.noFixSleepS,
      config.stationaryFixesForLongSleep, config.stationaryFixesForMaxSleep,
      Number(fixFound), Number(moving), stationaryStreak, noFixCycles);
  }

  retrySeconds(config, failures) {
    return this.fn.retry(...config.retryBackoffS, failures);
  }

  batchDue(config, queued, sinceAck, capacity = 100) {
    return this.fn.batchDue(config.txIntervalS, config.txMinPoints,
      queued, sinceAck, capacity) === 1;
  }

  forwardingDelayMs(frame, repeater) {
    return this.fn.forwardingDelay(frame.deviceHashHi, frame.deviceHashLo,
      frame.bootId, frame.counter, frame.messageType, frame.schemaVersion,
      repeater.hashHi, repeater.hashLo, repeater.config.forwardingBaseDelayMs,
      repeater.config.forwardingSlotCount, repeater.config.forwardingSlotWidthMs);
  }
}

// Deterministic test double. Browser releases never fall back to it: the worker
// fails closed if the production WASM core cannot be loaded.
export class ReferenceCore {
  constructor() { this.kind = 'reference'; this.version = 1; }
  validateRadio(radio) {
    const r = { ...RADIO_DEFAULTS, ...radio };
    const supported = [62_500, 125_000, 250_000, 500_000].includes(r.bandwidthHz);
    const half = r.bandwidthHz / 2;
    return supported && r.frequencyHz >= 868_000_000 + half &&
      r.frequencyHz <= 868_600_000 - half && r.txPowerDbm >= 2 &&
      r.txPowerDbm <= 14 && r.spreadingFactor >= 7 && r.spreadingFactor <= 12 &&
      r.codingRateDenominator >= 5 && r.codingRateDenominator <= 8 &&
      r.preambleSymbols >= 6 && r.preambleSymbols <= 32;
  }
  airtimeMs(bytes, radio) {
    const r = { ...RADIO_DEFAULTS, ...radio };
    if (bytes < 1 || bytes > 255) return 0;
    const symbol = (2 ** r.spreadingFactor) / r.bandwidthHz;
    const de = symbol >= 0.016 ? 1 : 0;
    const numerator = 8 * bytes - 4 * r.spreadingFactor + 28 + 16;
    const blocks = Math.max(0, Math.ceil(numerator / (4 * (r.spreadingFactor - 2 * de))));
    return Math.ceil((r.preambleSymbols + 4.25 + 8 + blocks * r.codingRateDenominator) * symbol * 1000);
  }
  ackRelayGuardMs(bytes, radio, incomingHop, hopLimit) {
    return incomingHop >= hopLimit ? 0 : this.airtimeMs(bytes, radio) + 400;
  }
  sensitivityDbm(radio) {
    const r = { ...RADIO_DEFAULTS, ...radio };
    const snr = [-7.5, -10, -12.5, -15, -17.5, -20][r.spreadingFactor - 7];
    return -174 + 10 * Math.log10(r.bandwidthHz) + 6 + snr;
  }
  sleepSeconds(c, fix, moving, streak, misses) {
    if (!fix) return c.noFixSleepS[Math.min(Math.max(misses - 1, 0), 3)];
    if (moving) return c.movingSleepS;
    if (streak >= c.stationaryFixesForMaxSleep) return c.longStationarySleepS;
    if (streak >= c.stationaryFixesForLongSleep) return c.stationarySleepS;
    return c.movingSleepS;
  }
  retrySeconds(c, failures) {
    return failures === 0 ? 0 : c.retryBackoffS[Math.min(failures - 1, 3)];
  }
  batchDue(c, queued, sinceAck, capacity = 100) {
    return queued > 0 && (queued >= c.txMinPoints || sinceAck >= c.txIntervalS || queued >= capacity - 16);
  }
  forwardingDelayMs(frame, repeater) {
    // Unit tests only require stable bounded scheduling; golden tests compare
    // the native and WASM implementations directly.
    const value = ((frame.counter * 2654435761) ^ repeater.hashLo) >>> 0;
    return repeater.config.forwardingBaseDelayMs +
      (value % repeater.config.forwardingSlotCount) * repeater.config.forwardingSlotWidthMs;
  }
}

export { RADIO_DEFAULTS };
