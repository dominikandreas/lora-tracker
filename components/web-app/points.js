// Pure point contract shared by the browser application and its simulator tests.
const HASH_RE = /^[0-9a-f]{16}$/;
const ID_RE = /^[a-z0-9](?:[a-z0-9_-]{0,23})$/;
const MAX_UNIX_MS = 32_503_680_000_000;

function integer(value, minimum, maximum, field) {
  if (!Number.isSafeInteger(value) || value < minimum || value > maximum) {
    throw new Error(`${field} is invalid`);
  }
  return value;
}

function finiteNumber(value, minimum, maximum, field) {
  if (
    typeof value !== "number" ||
    !Number.isFinite(value) ||
    value < minimum ||
    value > maximum
  ) {
    throw new Error(`${field} is invalid`);
  }
  return value;
}

function canonicalId(value, field) {
  if (typeof value !== "string" || !ID_RE.test(value)) {
    throw new Error(`${field} is invalid`);
  }
  return value;
}

export function normalizePoint(raw, fallbackNow = Date.now()) {
  if (!raw || raw.api_version !== 1 || raw.point_schema_version !== 2) {
    throw new Error("Unsupported point schema");
  }
  if (raw.transport_version !== 2 || raw.schema_version !== 2) {
    throw new Error("Unsupported radio transport schema");
  }
  if (
    typeof raw.device_hash !== "string" ||
    !HASH_RE.test(raw.device_hash) ||
    typeof raw.gateway_hash !== "string" ||
    !HASH_RE.test(raw.gateway_hash)
  ) {
    throw new Error("Device or gateway hash is invalid");
  }
  const bootId = integer(raw.boot_id, 0, 0xffffffff, "boot_id");
  const seq = integer(raw.seq, 0, 0xffffffff, "seq");
  if (raw.point_id !== `${raw.device_hash}:${bootId}:${seq}`) {
    throw new Error("point_id does not match the point identity");
  }
  canonicalId(raw.device_id, "device_id");
  canonicalId(raw.gateway_id, "gateway_id");
  if (
    typeof raw.device_name !== "string" ||
    raw.device_name.length < 1 ||
    raw.device_name.length > 32 ||
    /[\u0000-\u001f\u007f]/.test(raw.device_name)
  ) {
    throw new Error("device_name is invalid");
  }
  finiteNumber(raw.latitude, -90, 90, "latitude");
  finiteNumber(raw.longitude, -180, 180, "longitude");
  integer(raw.dist_m, 0, 0xffffffff, "dist_m");
  integer(raw.battery_level, 0, 100, "battery_level");
  integer(raw.rssi, -200, 50, "rssi");
  integer(raw.gateway_uptime_ms, 0, 0xffffffff, "gateway_uptime_ms");
  if (typeof raw.timestamp_valid !== "boolean") {
    throw new Error("timestamp_valid is invalid");
  }
  const fixTime = integer(
    raw.fix_time_unix_ms,
    0,
    MAX_UNIX_MS,
    "fix_time_unix_ms",
  );
  const expectedSource = raw.timestamp_valid ? "gnss" : "unavailable";
  if (
    raw.time_source !== expectedSource ||
    (raw.timestamp_valid ? fixTime === 0 : fixTime !== 0)
  ) {
    throw new Error("Timestamp fields are inconsistent");
  }
  const fallback =
    Number.isSafeInteger(raw.received_at_ms) && raw.received_at_ms > 0
      ? raw.received_at_ms
      : fallbackNow;
  const effective = raw.timestamp_valid ? fixTime : fallback;
  return {
    ...raw,
    boot_id: bootId,
    seq,
    fix_time_unix_ms: fixTime,
    time_source: raw.timestamp_valid ? "gnss" : "received",
    effective_time_unix_ms: effective,
  };
}
