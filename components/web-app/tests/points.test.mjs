import test from 'node:test';
import assert from 'node:assert/strict';
import { normalizePoint } from '../points.js';

const base = { api_version: 1, point_schema_version: 2, point_id: 'abc:1:1' };

test('uses a valid GNSS timestamp as the browser display time', () => {
  const point = normalizePoint({ ...base, timestamp_valid: true, fix_time_unix_ms: 1_784_050_000_000 });
  assert.equal(point.effective_time_unix_ms, 1_784_050_000_000);
  assert.equal(point.time_source, 'gnss');
});

test('uses received time for untimed and malformed timestamp fields', () => {
  const untimed = normalizePoint({ ...base, timestamp_valid: false, received_at_ms: 1234 }, 99);
  const malformed = normalizePoint({ ...base, timestamp_valid: true, fix_time_unix_ms: 1.5 }, 99);
  assert.equal(untimed.effective_time_unix_ms, 1234);
  assert.equal(untimed.time_source, 'received');
  assert.equal(malformed.effective_time_unix_ms, 99);
  assert.equal(malformed.timestamp_valid, false);
});

test('rejects incompatible point schemas', () => {
  assert.throws(() => normalizePoint({ api_version: 2, point_schema_version: 2 }), /Unsupported/);
  assert.throws(() => normalizePoint({ api_version: 1, point_schema_version: 1 }), /Unsupported/);
});
