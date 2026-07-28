const commonRadio = [
  ["lora_frequency_hz", "Frequency (Hz)", "lora.frequency_hz", "number"],
  ["lora_bandwidth_hz", "Bandwidth (Hz)", "lora.bandwidth_hz", "number"],
  ["lora_tx_power_dbm", "Transmit power (dBm)", "lora.tx_power_dbm", "number"],
  ["lora_sf", "Spreading factor", "lora.sf", "number"],
  ["lora_coding_rate", "Coding rate denominator", "lora.coding_rate", "number"],
  ["lora_preamble_length", "Preamble length", "lora.preamble_length", "number"],
  ["lora_sync_word", "Sync word", "lora.sync_word", "number"],
  ["lora_relay_hop_limit", "Relay hop limit", "lora.relay_hop_limit", "number"],
];

const trackerFields = [
  ["device_id", "Device ID", "device_id"],
  ["device_name", "Device name", "device_name"],
  ["wifi_ssid", "Wi-Fi SSID", "wifi_ssid"],
  ["wifi_password", "Wi-Fi password", null, "password", true],
  ["ble_debug_enabled", "BLE debug enabled", "ble_debug_enabled", "checkbox"],
  ["battery_sense_enabled", "Battery sensing enabled", "battery_sense_enabled", "checkbox"],
  ["lora_aead_key", "LoRa encryption key", "lora_aead_key", "password"],
  ...commonRadio,
  ["lora_tx_interval_s", "Transmit interval (s)", "communication.tx_interval_s", "number"],
  ["lora_tx_min_points", "Minimum points per batch", "communication.tx_min_points", "number"],
  ["lora_ack_timeout_ms", "ACK timeout (ms)", "communication.ack_timeout_ms", "number"],
  ...[0, 1, 2, 3].map((i) => [`lora_retry_backoff_${i + 1}_s`, `Retry backoff ${i + 1} (s)`, `communication.retry_backoff_s.${i}`, "number"]),
  ["min_distance_m", "Minimum point distance (m)", "gps.min_distance_m", "number"],
  ["min_speed_kmph", "Minimum speed (km/h)", "gps.min_speed_kmph", "number"],
  ["max_hdop", "Maximum HDOP", "gps.max_hdop", "number"],
  ["min_satellites", "Minimum satellites", "gps.min_satellites", "number"],
  ["max_speed_mps", "Maximum plausible speed (m/s)", "gps.max_speed_mps", "number"],
  ["max_fix_age_s", "Maximum fix age (s)", "gps.max_fix_age_s", "number"],
  ...[0, 1, 2, 3].map((i) => [`gps_timeout_${i + 1}_ms`, `GNSS timeout ${i + 1} (ms)`, `gps.timeouts_ms.${i}`, "number"]),
  ["gps_full_retry_interval_s", "Full GNSS retry interval (s)", "gps.full_retry_interval_s", "number"],
  ["gps_initial_listen_ms", "Initial GNSS listen (ms)", "gps.initial_listen_ms", "number"],
  ["gps_light_sleep_chunk_ms", "GNSS sleep chunk (ms)", "gps.light_sleep_chunk_ms", "number"],
  ["gps_listen_window_ms", "GNSS listen window (ms)", "gps.listen_window_ms", "number"],
  ["movement_speed_threshold_kmph", "Movement speed threshold (km/h)", "movement.speed_threshold_kmph", "number"],
  ["movement_displacement_threshold_m", "Movement displacement (m)", "movement.displacement_threshold_m", "number"],
  ["movement_evidence_distance_m", "Evidence distance (m)", "movement.evidence_distance_m", "number"],
  ["movement_evidence_step_m", "Evidence step (m)", "movement.evidence_step_m", "number"],
  ["movement_direction_tolerance_deg", "Direction tolerance (°)", "movement.direction_tolerance_deg", "number"],
  ["movement_evidence_required", "Evidence samples", "movement.evidence_required", "number"],
  ["history_point_spacing_m", "History point spacing (m)", "storage.history_point_spacing_m", "number"],
  ["save_distance_threshold_m", "Save distance threshold (m)", "storage.save_distance_threshold_m", "number"],
  ["nvs_save_interval_s", "NVS save interval (s)", "storage.nvs_save_interval_s", "number"],
  ["moving_sleep_s", "Moving sleep (s)", "sleep.moving_s", "number"],
  ["stationary_sleep_s", "Stationary sleep (s)", "sleep.stationary_s", "number"],
  ["long_stationary_sleep_s", "Long stationary sleep (s)", "sleep.long_stationary_s", "number"],
  ...[0, 1, 2, 3].map((i) => [`no_fix_sleep_${i + 1}_s`, `No-fix sleep ${i + 1} (s)`, `sleep.no_fix_s.${i}`, "number"]),
  ["stationary_fixes_for_long_sleep", "Fixes before long sleep", "sleep.stationary_fixes_for_long_sleep", "number"],
  ["stationary_fixes_for_max_sleep", "Fixes before maximum sleep", "sleep.stationary_fixes_for_max_sleep", "number"],
];

const gatewayFields = [
  ["gateway_id", "Gateway ID", "gateway_id"],
  ["gateway_name", "Gateway name", "gateway_name"],
  ["wifi_ssid", "Wi-Fi SSID", "wifi_ssid"],
  ["wifi_password", "Wi-Fi password", null, "password", true],
  ["mqtt_host", "MQTT host", "mqtt.host"],
  ["mqtt_port", "MQTT port", "mqtt.port", "number"],
  ["mqtt_tls_enabled", "MQTT TLS enabled", "mqtt.tls_enabled", "checkbox"],
  ["mqtt_username", "MQTT username", "mqtt.username"],
  ["mqtt_password", "MQTT password", null, "password", true],
  ["mqtt_base_topic", "MQTT base topic", "mqtt.base_topic"],
  ["mqtt_buffer_size", "MQTT buffer size", "mqtt.buffer_size", "number"],
  ["mqtt_ca_certificate", "MQTT root CA certificate", null, "textarea", true],
  ...commonRadio,
  ["dedup_save_interval", "Dedup save interval", "dedup_save_interval", "number"],
  ["wifi_retry_interval_ms", "Wi-Fi retry interval (ms)", "wifi_retry_interval_ms", "number"],
  ["mqtt_retry_interval_ms", "MQTT retry interval (ms)", "mqtt_retry_interval_ms", "number"],
  ["tracker_count", "Tracker registry size", "trackers.length", "number"],
];

const repeaterFields = [
  ["repeater_id", "Repeater ID", "repeater_id"],
  ["repeater_name", "Repeater name", "repeater_name"],
  ["frequency_hz", "Frequency (Hz)", "lora.frequency_hz", "number"],
  ["bandwidth_hz", "Bandwidth (Hz)", "lora.bandwidth_hz", "number"],
  ["tx_power_dbm", "Transmit power (dBm)", "lora.tx_power_dbm", "number"],
  ["sf", "Spreading factor", "lora.sf", "number"],
  ["coding_rate", "Coding rate denominator", "lora.coding_rate", "number"],
  ["preamble", "Preamble length", "lora.preamble_length", "number"],
  ["sync_word", "Sync word", "lora.sync_word", "number"],
  ["hop_limit", "Relay hop limit", "lora.relay_hop_limit", "number"],
  ["base_delay_ms", "Forwarding base delay (ms)", "forwarding.base_delay_ms", "number"],
  ["slot_width_ms", "Priority slot width (ms)", "forwarding.slot_width_ms", "number"],
  ["slot_count", "Priority slot count", "forwarding.slot_count", "number"],
  ["cache_ttl_s", "Duplicate cache lifetime (s)", "forwarding.cache_ttl_s", "number"],
  ["airtime_budget_ms", "Airtime budget per hour (ms)", "forwarding.airtime_budget_ms", "number"],
  ["heartbeat_s", "Heartbeat interval (s)", "forwarding.heartbeat_s", "number"],
];

function getPath(source, path) {
  if (!path) return "";
  return path.split(".").reduce((value, key) => value?.[key], source) ?? "";
}

export function renderDeviceConfig(container, config) {
  const fields = config.role === "gateway"
    ? gatewayFields
    : config.role === "repeater"
      ? repeaterFields
      : trackerFields;
  container.replaceChildren();
  for (const [name, labelText, path, type = "text", secret = false] of fields) {
    const label = document.createElement("label");
    label.textContent = labelText;
    const input = document.createElement(type === "textarea" ? "textarea" : "input");
    input.dataset.configField = name;
    input.dataset.secret = secret ? "true" : "false";
    if (type !== "textarea") input.type = type;
    if (type === "number") input.step = "any";
    const value = getPath(config, path);
    if (type === "checkbox") input.checked = Boolean(value);
    else input.value = value;
    if (secret && !path) input.placeholder = "Leave empty to keep existing value";
    label.append(input);
    container.append(label);
  }
  if (config.role === "gateway") {
    for (const tracker of config.trackers || []) {
      const index = Number(tracker.index);
      const heading = document.createElement("h4");
      heading.textContent = `Tracker registry slot ${index + 1}`;
      container.append(heading);
      const registryFields = [
        [`tracker.${index}.id`, "Tracker ID", tracker.id, "text", false],
        [`tracker.${index}.name`, "Tracker name", tracker.name, "text", false],
        [`tracker.${index}.lora_aead_key`, "LoRa encryption key", "", "password", true],
        [`tracker.${index}.enabled`, "Enabled", tracker.enabled, "checkbox", false],
      ];
      for (const [name, labelText, value, type, secret] of registryFields) {
        const label = document.createElement("label");
        label.textContent = labelText;
        const input = document.createElement("input");
        input.dataset.configField = name;
        input.dataset.secret = secret ? "true" : "false";
        input.type = type;
        if (type === "checkbox") input.checked = Boolean(value);
        else input.value = value;
        if (secret) input.placeholder = "Leave empty to keep existing key";
        label.append(input);
        container.append(label);
      }
    }
  }
}

export function collectDeviceConfig(container) {
  const fields = {};
  for (const input of container.querySelectorAll("[data-config-field]")) {
    let value = input.type === "checkbox" ? (input.checked ? "1" : "0") : input.value;
    if (input.dataset.secret === "true" && value === "") continue;
    if (input.type !== "checkbox" && value === "") continue;
    fields[input.dataset.configField] = value;
  }
  return fields;
}
