import test from "node:test";
import assert from "node:assert/strict";
import { normalizePoint } from "../points.js";

const HASH = "3db3edf61a18fac0";
const base = {
  api_version: 1,
  point_schema_version: 2,
  transport_version: 2,
  schema_version: 2,
  device_hash: HASH,
  point_id: `${HASH}:1:1`,
  latitude: 50.1,
  longitude: 8.6,
  dist_m: 42,
  battery_level: 80,
  rssi: -100,
  boot_id: 1,
  seq: 1,
  device_id: "tracker-1",
  device_name: "Tracker 1",
  gateway_id: "home",
  gateway_hash: "1111111111111111",
  gateway_uptime_ms: 1000,
};

test("uses a valid GNSS timestamp as the browser display time", () => {
  const point = normalizePoint({
    ...base,
    timestamp_valid: true,
    fix_time_unix_ms: 1_784_050_000_000,
    time_source: "gnss",
  });
  assert.equal(point.effective_time_unix_ms, 1_784_050_000_000);
  assert.equal(point.time_source, "gnss");
});

test("uses received time for a consistent untimed point", () => {
  const point = normalizePoint({
    ...base,
    timestamp_valid: false,
    fix_time_unix_ms: 0,
    time_source: "unavailable",
    received_at_ms: 1234,
  }, 99);
  assert.equal(point.effective_time_unix_ms, 1234);
  assert.equal(point.time_source, "received");
});

test("rejects incompatible schemas and malformed identity", () => {
  assert.throws(() => normalizePoint({ api_version: 2, point_schema_version: 2 }), /Unsupported/);
  assert.throws(() => normalizePoint({ ...base, transport_version: 1 }), /Unsupported/);
  assert.throws(() => normalizePoint({
    ...base,
    timestamp_valid: true,
    fix_time_unix_ms: 1.5,
    time_source: "gnss",
  }), /fix_time/);
  assert.throws(() => normalizePoint({
    ...base,
    gateway_id: `<img src=x onerror=alert(1)>`,
    timestamp_valid: false,
    fix_time_unix_ms: 0,
    time_source: "unavailable",
  }), /gateway_id/);
  assert.throws(() => normalizePoint({
    ...base,
    latitude: "50.1",
    timestamp_valid: false,
    fix_time_unix_ms: 0,
    time_source: "unavailable",
  }), /latitude/);
});
