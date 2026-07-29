const PROFILE_KINDS = new Set(["wifi", "mqtt"]);

function text(value, maximum = 4096) {
  return String(value ?? "").slice(0, maximum);
}

function normalizeWifi(profile) {
  return {
    id: text(profile.id, 80),
    name: text(profile.name, 80),
    ssid: text(profile.ssid, 32),
    password: text(profile.password, 64),
  };
}

function normalizeMqtt(profile) {
  const tlsEnabled = Boolean(profile.tls_enabled);
  const proposedPort = Number.parseInt(profile.port, 10);
  return {
    id: text(profile.id, 80),
    name: text(profile.name, 80),
    host: text(profile.host, 64).trim(),
    port:
      proposedPort >= 1 && proposedPort <= 65535
        ? proposedPort
        : tlsEnabled
          ? 8883
          : 1883,
    tls_enabled: tlsEnabled,
    username: text(profile.username, 32),
    password: text(profile.password, 64),
    base_topic: text(profile.base_topic || "lora-tracker", 32).trim(),
    ca_certificate: text(profile.ca_certificate, 4096),
    websocket_url: text(profile.websocket_url, 512).trim(),
  };
}

export function normalizeConfigurationProfiles(value) {
  const source = value && typeof value === "object" ? value : {};
  const normalizeList = (kind, normalizer) =>
    (Array.isArray(source[kind]) ? source[kind] : [])
      .filter((profile) => profile && typeof profile === "object")
      .map(normalizer)
      .filter((profile) => profile.id && profile.name);
  return {
    wifi: normalizeList("wifi", normalizeWifi),
    mqtt: normalizeList("mqtt", normalizeMqtt),
  };
}

export function upsertConfigurationProfile(profiles, kind, profile) {
  if (!PROFILE_KINDS.has(kind)) throw new Error("Unknown profile type");
  const normalized = normalizeConfigurationProfiles(profiles);
  const item = kind === "wifi" ? normalizeWifi(profile) : normalizeMqtt(profile);
  if (!item.id || !item.name) throw new Error("Profile name is required");
  if (kind === "wifi" && !item.ssid) throw new Error("Wi-Fi SSID is required");
  if (kind === "mqtt" && !item.host) throw new Error("MQTT host is required");
  if (kind === "mqtt" && item.websocket_url) {
    let parsed;
    try {
      parsed = new URL(item.websocket_url);
    } catch {
      throw new Error("App WebSocket URL is invalid");
    }
    if (!["ws:", "wss:"].includes(parsed.protocol)) {
      throw new Error("App WebSocket URL must use ws:// or wss://");
    }
  }
  const index = normalized[kind].findIndex(({ id }) => id === item.id);
  if (index >= 0) normalized[kind][index] = item;
  else normalized[kind].push(item);
  return normalized;
}

export function removeConfigurationProfile(profiles, kind, id) {
  if (!PROFILE_KINDS.has(kind)) throw new Error("Unknown profile type");
  const normalized = normalizeConfigurationProfiles(profiles);
  normalized[kind] = normalized[kind].filter((profile) => profile.id !== id);
  return normalized;
}

export function configurationFieldsForProfile(kind, profile) {
  if (kind === "wifi") {
    const value = normalizeWifi(profile || {});
    return { wifi_ssid: value.ssid, wifi_password: value.password };
  }
  if (kind === "mqtt") {
    const value = normalizeMqtt(profile || {});
    return {
      mqtt_host: value.host,
      mqtt_port: String(value.port),
      mqtt_tls_enabled: value.tls_enabled ? "1" : "0",
      mqtt_username: value.username,
      mqtt_password: value.password,
      mqtt_base_topic: value.base_topic,
      mqtt_ca_certificate: value.ca_certificate,
    };
  }
  throw new Error("Unknown profile type");
}

export function mqttWebSocketUrlForProfile(profile) {
  const value = normalizeMqtt(profile || {});
  if (value.websocket_url) return value.websocket_url;
  const scheme = value.tls_enabled ? "wss" : "ws";
  const port = value.tls_enabled ? 8884 : 1884;
  return value.host ? `${scheme}://${value.host}:${port}` : "";
}

export function applicationFieldsForMqttProfile(profile) {
  const value = normalizeMqtt(profile || {});
  return {
    brokerUrl: mqttWebSocketUrlForProfile(value),
    baseTopic: value.base_topic,
    username: value.username,
    password: value.password,
  };
}

export function profileKindsForRole(role) {
  return {
    wifi: role === "tracker" || role === "gateway",
    mqtt: role === "gateway",
  };
}
