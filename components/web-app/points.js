// Pure point contract shared by the browser application and its simulator tests.
export function normalizePoint(raw, fallbackNow = Date.now()) {
  if (!raw || raw.api_version !== 1 || raw.point_schema_version !== 2) {
    throw new Error('Unsupported point schema');
  }
  const validTime = raw.timestamp_valid === true
    && Number.isSafeInteger(raw.fix_time_unix_ms)
    && raw.fix_time_unix_ms > 0;
  const effective = validTime ? raw.fix_time_unix_ms : (raw.received_at_ms || fallbackNow);
  return {
    ...raw,
    timestamp_valid: validTime,
    fix_time_unix_ms: validTime ? raw.fix_time_unix_ms : 0,
    time_source: validTime ? 'gnss' : 'received',
    effective_time_unix_ms: raw.effective_time_unix_ms || effective,
  };
}
