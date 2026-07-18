import { MqttWebSocketClient } from "./mqtt.js";
import {
  putPoint,
  listPoints,
  listLatestPoints,
  clearPoints,
} from "./storage.js";
import { normalizePoint } from "./points.js";
import { MapManager } from "./map.js";
import { AlertsManager } from "./alerts.js";
import { OnboardingManager } from "./onboarding.js";

const els = Object.fromEntries(
  [...document.querySelectorAll("[id]")].map((el) => [el.id, el]),
);
const mapManager = new MapManager("map");
const alertsManager = new AlertsManager();
const onboardingManager = new OnboardingManager();
const mqtt = new MqttWebSocketClient();
const trackers = new Map();
let selectedHash = null;
let connected = false;
let pendingHistoryRequest = null;

onboardingManager.transport.onDisconnect = () => {
  setBleStatus("Disconnected");
  els.bleControls.hidden = true;
  els.blePassword.value = "";
  els.cfgRadioKey.value = "";
  onboardingManager.lastConfig = null;
};

let saved = {};
try {
  saved = JSON.parse(
    localStorage.getItem("lora-tracker.web.settings") || "{}",
  );
} catch {
  localStorage.removeItem("lora-tracker.web.settings");
}
els.brokerUrl.value = saved.brokerUrl || "";
els.baseTopic.value = saved.baseTopic || "lora-tracker";
els.username.value = saved.username || "";

let mapLayerType = ["none", "osm", "pmtiles"].includes(saved.mapLayerType)
  ? saved.mapLayerType
  : "none";
els.mapLayer.value = mapLayerType;

async function loadPmtiles() {
  try {
    if (!navigator.storage?.getDirectory) throw new Error("OPFS unavailable");
    const root = await navigator.storage.getDirectory();
    const handle = await root.getFileHandle("map.pmtiles");
    await mapManager.setLayer("pmtiles", handle);
    els.mapLayer.querySelector('option[value="pmtiles"]').disabled = false;
  } catch {
    if (mapLayerType === "pmtiles") mapLayerType = "none";
    els.mapLayer.value = mapLayerType;
    mapManager.setLayer(mapLayerType);
    saveSettings();
  }
}

if (mapLayerType === "pmtiles") {
  loadPmtiles();
} else {
  mapManager.setLayer(mapLayerType);
  // Still check if we have a PMTiles file available
  if (navigator.storage?.getDirectory) {
    navigator.storage
      .getDirectory()
      .then((root) => root.getFileHandle("map.pmtiles"))
      .then(() => {
        els.mapLayer.querySelector('option[value="pmtiles"]').disabled = false;
      })
      .catch(() => {});
  }
}

els.mapLayer.addEventListener("change", async (e) => {
  const previous = mapLayerType;
  mapLayerType = e.target.value;
  try {
    if (mapLayerType === "pmtiles") {
      await loadPmtiles();
    } else {
      await mapManager.setLayer(mapLayerType);
    }
    saveSettings();
  } catch (error) {
    mapLayerType = previous;
    els.mapLayer.value = previous;
    els.connectionMessage.textContent = `Map layer failed: ${error.message}`;
  }
});

// --- Onboarding Logic ---
function setBleStatus(msg) {
  els.bleStatus.textContent = msg;
  const connected = msg === "Connected";
  const state =
    connected
      ? "online"
      : msg === "Disconnected" || msg.startsWith("Error")
        ? "offline"
        : "connecting";
  els.bleStatus.className = `badge ${state}`;
}

function appendBleOutput(msg) {
  els.bleOutput.textContent +=
    (typeof msg === "string" ? msg : JSON.stringify(msg, null, 2)) + "\n";
  els.bleOutput.scrollTop = els.bleOutput.scrollHeight;
}

els.onboardingButton.addEventListener("click", async () => {
  els.onboardingPanel.hidden = !els.onboardingPanel.hidden;
  if (!els.onboardingPanel.hidden) {
    try {
      setBleStatus("Connecting...");
      await onboardingManager.connectTracker();
      setBleStatus("Connected");
      els.bleControls.hidden = false;
    } catch (e) {
      setBleStatus(`Error: ${e.message}`);
    }
  } else {
    onboardingManager.disconnect();
    setBleStatus("Disconnected");
    els.bleControls.hidden = true;
  }
});

function requireCredential() {
  const credential = els.blePassword.value;
  if (
    credential.length < 12 ||
    credential.length > 24 ||
    /\s/.test(credential)
  ) {
    throw new Error("Credential must be 12–24 characters without spaces");
  }
  return credential;
}

els.bleClaim.addEventListener("click", async () => {
  try {
    appendBleOutput("Claiming...");
    const result = await onboardingManager.claim(requireCredential());
    appendBleOutput(result);
    els.bleGetConfig.click();
  } catch (e) {
    appendBleOutput(`Error: ${e.message}`);
  }
});

els.bleAuth.addEventListener("click", async () => {
  try {
    appendBleOutput("Authenticating...");
    const result = await onboardingManager.auth(requireCredential());
    appendBleOutput(result);
    // Auto-fetch config after auth
    els.bleGetConfig.click();
  } catch (e) {
    appendBleOutput(`Error: ${e.message}`);
  }
});

els.bleReplaceCredential.addEventListener("click", async () => {
  try {
    if (!onboardingManager.lastConfig) {
      throw new Error("Authenticate and fetch configuration first");
    }
    if (!confirm("Replace the administrator credential and reboot the tracker?")) {
      return;
    }
    const result = await onboardingManager.replaceCredential(
      requireCredential(),
    );
    appendBleOutput(result);
  } catch (e) {
    appendBleOutput(`Error: ${e.message}`);
  }
});

function loadConfigIntoForm(config) {
  els.bleConfigForm.hidden = false;
  els.cfgDeviceId.value = config.device_id || "";
  els.cfgDeviceName.value = config.device_name || "";
  els.cfgWifiSsid.value = config.wifi_ssid || "";
  els.cfgWifiPassword.value = "";
  els.cfgFrequency.value = config.lora?.frequency_hz ?? "";
  els.cfgTxPower.value = config.lora?.tx_power_dbm ?? "";
  els.cfgSf.value = config.lora?.sf ?? "";
  els.cfgHopLimit.value = config.lora?.relay_hop_limit ?? "";
  els.cfgTxInterval.value = config.communication?.tx_interval_s ?? "";
  els.cfgAckTimeout.value = config.communication?.ack_timeout_ms ?? "";
  els.cfgMovingSleep.value = config.sleep?.moving_s ?? "";
  els.cfgStationarySleep.value = config.sleep?.stationary_s ?? "";
  els.cfgRadioKey.value = config.lora_aead_key || "";
}

els.bleRevealKey.addEventListener("click", () => {
  const showing = els.cfgRadioKey.type === "text";
  els.cfgRadioKey.type = showing ? "password" : "text";
  els.bleRevealKey.textContent = showing ? "Show key" : "Hide key";
});

els.bleGetConfig.addEventListener("click", async () => {
  try {
    appendBleOutput("Fetching config...");
    const result = await onboardingManager.getConfig();
    appendBleOutput(`Configuration revision ${result.revision} loaded.`);
    loadConfigIntoForm(result);
  } catch (e) {
    appendBleOutput(`Error: ${e.message}`);
  }
});

els.bleSaveConfig.addEventListener("click", async () => {
  try {
    const fields = {};
    if (els.cfgDeviceId.value) fields.device_id = els.cfgDeviceId.value;
    if (els.cfgDeviceName.value) fields.device_name = els.cfgDeviceName.value;
    if (els.cfgWifiSsid.value) fields.wifi_ssid = els.cfgWifiSsid.value;
    if (els.cfgWifiPassword.value)
      fields.wifi_password = els.cfgWifiPassword.value;
    const numericFields = [
      ["lora_frequency_hz", els.cfgFrequency],
      ["lora_tx_power_dbm", els.cfgTxPower],
      ["lora_sf", els.cfgSf],
      ["lora_relay_hop_limit", els.cfgHopLimit],
      ["lora_tx_interval_s", els.cfgTxInterval],
      ["lora_ack_timeout_ms", els.cfgAckTimeout],
      ["moving_sleep_s", els.cfgMovingSleep],
      ["stationary_sleep_s", els.cfgStationarySleep],
    ];
    for (const [name, input] of numericFields) {
      if (input.value !== "") fields[name] = input.value;
    }

    if (!onboardingManager.lastConfig)
      throw new Error("Must fetch config first");

    appendBleOutput("Saving config...");
    const result = await onboardingManager.patchConfig(
      onboardingManager.lastConfig.revision,
      fields,
    );
    appendBleOutput(result);
    // Refresh to get updated revision
    els.bleGetConfig.click();
  } catch (e) {
    appendBleOutput(`Error: ${e.message}`);
  }
});

els.bleRollback.addEventListener("click", async () => {
  try {
    const result = await onboardingManager.rollback();
    if (result) appendBleOutput(result);
  } catch (e) {
    appendBleOutput(`Error: ${e.message}`);
  }
});

els.bleReboot.addEventListener("click", async () => {
  try {
    const result = await onboardingManager.reboot();
    if (result) appendBleOutput(result);
  } catch (e) {
    appendBleOutput(`Error: ${e.message}`);
  }
});

els.bleFactoryReset.addEventListener("click", async () => {
  try {
    const result = await onboardingManager.factoryReset();
    if (result) appendBleOutput(result);
  } catch (e) {
    appendBleOutput(`Error: ${e.message}`);
  }
});

async function importPmtiles(file) {
  // Validate and display before replacing the persisted archive.
  await mapManager.setLayer("pmtiles", file);
  if (navigator.storage?.getDirectory) {
    try {
      const root = await navigator.storage.getDirectory();
      const draft = await root.getFileHandle("map.pmtiles", { create: true });
      const writable = await draft.createWritable();
      await writable.write(file);
      await writable.close();
    } catch (error) {
      els.connectionMessage.textContent =
        `Map loaded for this session but could not be retained: ${error.message}`;
    }
  }
  els.mapLayer.querySelector('option[value="pmtiles"]').disabled = false;
  els.mapLayer.value = "pmtiles";
  mapLayerType = "pmtiles";
  saveSettings();
}

els.importPmtilesButton.addEventListener("click", async () => {
  if ("showOpenFilePicker" in window) {
    try {
      const [fileHandle] = await window.showOpenFilePicker({
        types: [
          {
            description: "PMTiles Archive",
            accept: { "application/octet-stream": [".pmtiles"] },
          },
        ],
      });
      const file = await fileHandle.getFile();
      await importPmtiles(file);
    } catch (e) {
      if (e.name === "AbortError") return;
      if (e.name === "NotSupportedError" || e.name === "SecurityError") {
        els.pmtilesInput?.click();
      } else {
        alert(`Failed to load PMTiles: ${e.message}`);
      }
    }
  } else {
    els.pmtilesInput?.click();
  }
});

function updateAlertsButtonState() {
  if (alertsManager.enabled) {
    els.enableAlertsButton.textContent = "Alerts Enabled";
    els.enableAlertsButton.disabled = true;
    alertsManager.startInterval(trackers);
  } else {
    els.enableAlertsButton.textContent = "Enable Alerts";
    els.enableAlertsButton.disabled = false;
    alertsManager.stopInterval();
  }
}

els.enableAlertsButton.addEventListener("click", async () => {
  if (await alertsManager.requestPermission()) {
    updateAlertsButtonState();
  }
});
updateAlertsButtonState();

els.pmtilesInput?.addEventListener("change", async (e) => {
  const file = e.target.files[0];
  if (!file) return;
  try {
    await importPmtiles(file);
  } catch (err) {
    alert(`Failed to load PMTiles: ${err.message}`);
  } finally {
    e.target.value = "";
  }
});

function saveSettings() {
  localStorage.setItem(
    "lora-tracker.web.settings",
    JSON.stringify({
      brokerUrl: els.brokerUrl.value.trim(),
      baseTopic: els.baseTopic.value.trim() || "lora-tracker",
      username: els.username.value.trim(),
      mapLayerType,
    }),
  );
}

function setConnectionState(state, message = "") {
  els.connectionBadge.className = `badge ${state === "online" ? "online" : state === "offline" ? "offline" : "connecting"}`;
  els.connectionBadge.textContent =
    state === "online"
      ? "Online"
      : state === "offline"
        ? "Offline"
        : "Connecting";
  els.connectionMessage.textContent = message || state;
  els.connectButton.textContent =
    state === "online" || state === "connecting" ? "Disconnect" : "Connect";
}

function trackerFromPoint(point) {
  let tracker = trackers.get(point.device_hash);
  if (!tracker) {
    tracker = {
      hash: point.device_hash,
      name: point.device_name || point.device_id || point.device_hash,
      id: point.device_id || "",
      latest: null,
    };
    trackers.set(point.device_hash, tracker);
  }
  tracker.name = point.device_name || tracker.name;
  if (
    !tracker.latest ||
    point.effective_time_unix_ms >= tracker.latest.effective_time_unix_ms
  )
    tracker.latest = point;
  if (!selectedHash) selectedHash = point.device_hash;
  return tracker;
}

async function ingestPoint(raw, { evaluateAlerts = true } = {}) {
  const point = normalizePoint(raw);
  const tracker = trackerFromPoint(point);
  await putPoint(point);
  if (evaluateAlerts && tracker.latest === point)
    alertsManager.evaluateTracker(tracker, point);
  renderTrackerList();
  if (point.device_hash === selectedHash) await renderSelectedTracker();
}

function ageText(ms) {
  if (!ms) return "—";
  const seconds = Math.max(0, Math.floor((Date.now() - ms) / 1000));
  if (seconds < 60) return `${seconds}s`;
  if (seconds < 3600) return `${Math.floor(seconds / 60)}m`;
  if (seconds < 86400) return `${Math.floor(seconds / 3600)}h`;
  return `${Math.floor(seconds / 86400)}d`;
}

function formatTime(ms) {
  if (!ms) return "—";
  return new Intl.DateTimeFormat(undefined, {
    dateStyle: "short",
    timeStyle: "medium",
  }).format(new Date(ms));
}

function renderTrackerList() {
  els.trackerList.innerHTML = "";
  const sorted = [...trackers.values()].sort(
    (a, b) =>
      (b.latest?.effective_time_unix_ms || 0) -
      (a.latest?.effective_time_unix_ms || 0),
  );
  for (const tracker of sorted) {
    const node = els.trackerTemplate.content.firstElementChild.cloneNode(true);
    node.classList.toggle("active", tracker.hash === selectedHash);
    const fresh =
      tracker.latest &&
      Date.now() - tracker.latest.effective_time_unix_ms < 10 * 60_000;
    node.classList.toggle("fresh", Boolean(fresh));
    node.querySelector(".tracker-name").textContent = tracker.name;
    node.querySelector(".tracker-hash").textContent = tracker.hash;
    node.querySelector(".tracker-age").textContent = ageText(
      tracker.latest?.effective_time_unix_ms,
    );
    node.addEventListener("click", async () => {
      selectedHash = tracker.hash;
      renderTrackerList();
      await renderSelectedTracker(true);
    });
    els.trackerList.append(node);
  }
  els.trackerCount.textContent = String(sorted.length);
}

function drawRoute(points, fitBounds = false) {
  if (!points.length) {
    els.routeBounds.textContent = "—";
    return;
  }
  let minLat = Infinity,
    maxLat = -Infinity,
    minLon = Infinity,
    maxLon = -Infinity;
  for (const p of points) {
    minLat = Math.min(minLat, p.latitude);
    maxLat = Math.max(maxLat, p.latitude);
    minLon = Math.min(minLon, p.longitude);
    maxLon = Math.max(maxLon, p.longitude);
  }
  const bounds = mapManager.drawRoute(selectedHash, points);
  if (fitBounds) mapManager.fitBounds(bounds);
  mapManager.updateTracker(
    selectedHash,
    points.at(-1),
    trackers.get(selectedHash)?.name || selectedHash,
  );
  els.routeBounds.textContent = `${minLat.toFixed(5)}, ${minLon.toFixed(5)} → ${maxLat.toFixed(5)}, ${maxLon.toFixed(5)}`;
}

async function renderSelectedTracker(fitBounds = false) {
  const tracker = trackers.get(selectedHash);
  if (!tracker) return;
  const latest = tracker.latest;
  els.selectedTrackerName.textContent = tracker.name;
  els.lastSeen.textContent = latest
    ? ageText(latest.effective_time_unix_ms) + " ago"
    : "—";
  els.timeSource.textContent = latest
    ? `${formatTime(latest.effective_time_unix_ms)} · ${latest.timestamp_valid ? "GNSS time" : "receive-time fallback"}`
    : "No data";
  els.battery.textContent = latest ? `${latest.battery_level}%` : "—";
  els.batteryDetail.textContent = latest
    ? `Boot ${latest.boot_id} · seq ${latest.seq}`
    : "—";
  els.distance.textContent = latest
    ? `${(latest.dist_m / 1000).toFixed(2)} km`
    : "—";
  els.rssi.textContent = latest ? `${latest.rssi} dBm` : "—";
  els.gateway.textContent = latest
    ? latest.gateway_id || latest.gateway_hash || "unknown gateway"
    : "—";

  const hours = Number(els.historyRange.value || 24);
  const points = await listPoints(
    selectedHash,
    Date.now() - hours * 3600_000,
    Date.now() + 3600_000,
  );
  drawRoute(points, fitBounds);
  els.pointCount.textContent = `${points.length} point${points.length === 1 ? "" : "s"}`;
  els.eventTable.innerHTML = "";
  for (const point of points.slice(-30).reverse()) {
    const row = document.createElement("tr");
    const values = [
      formatTime(point.effective_time_unix_ms),
      `${point.latitude.toFixed(6)}, ${point.longitude.toFixed(6)}`,
      `${point.battery_level}%`,
      `${point.rssi} dBm`,
      point.gateway_id || point.gateway_hash || "—",
    ];
    for (const [index, value] of values.entries()) {
      const cell = document.createElement("td");
      if (index === 1) {
        const code = document.createElement("code");
        code.textContent = value;
        cell.append(code);
      } else {
        cell.textContent = value;
      }
      row.append(cell);
    }
    els.eventTable.append(row);
  }
}

function parseTopic(topic) {
  const base = (els.baseTopic.value.trim() || "lora-tracker")
    .split("/")
    .filter(Boolean);
  const parts = topic.split("/");
  if (!base.every((part, index) => parts[index] === part)) return null;
  return parts.slice(base.length);
}

mqtt.addEventListener("status", (event) => {
  const state = event.detail.state;
  connected = state === "online";
  setConnectionState(
    state,
    state === "reconnecting"
      ? `Reconnecting in ${Math.round(event.detail.delay / 1000)} seconds…`
      : state === "online"
        ? "Subscribed to tracker telemetry and history responses."
        : state === "connecting"
          ? "Opening secure MQTT WebSocket…"
          : "Not connected.",
  );
  if (state === "online") {
    const base = els.baseTopic.value.trim() || "lora-tracker";
    mqtt.subscribe(
      `${base}/v1/trackers/+/events/point`,
      `${base}/v1/trackers/+/state`,
      `${base}/v1/trackers/+/history/response/+`,
    );
  }
});

mqtt.addEventListener("error", (event) => {
  els.connectionMessage.textContent = event.detail.message;
});
mqtt.addEventListener("message", async (event) => {
  const { topic, payload } = event.detail;
  let data;
  try {
    data = JSON.parse(payload);
  } catch {
    return;
  }
  const tail = parseTopic(topic);
  if (!tail || tail[0] !== "v1" || tail[1] !== "trackers") return;
  if (data.device_hash && data.device_hash !== tail[2]) return;
  if ((tail[3] === "events" && tail[4] === "point") || tail[3] === "state") {
    try {
      await ingestPoint(data);
    } catch (error) {
      console.warn(error);
    }
  } else if (tail[3] === "history" && tail[4] === "response") {
    if (!data.ok || !Array.isArray(data.points)) return;
    for (const raw of data.points) {
      try {
        await ingestPoint(raw, { evaluateAlerts: false });
      } catch (error) {
        console.warn(error);
      }
    }
    if (
      data.final &&
      pendingHistoryRequest?.requestId === data.request_id &&
      pendingHistoryRequest.deviceHash === tail[2]
    ) {
      const request = pendingHistoryRequest;
      if (
        data.has_more &&
        Number.isSafeInteger(data.next_cursor) &&
        data.next_cursor > request.cursor &&
        request.page < 100
      ) {
        requestHistoryPage({
          ...request,
          cursor: data.next_cursor,
          page: request.page + 1,
        });
      } else {
        pendingHistoryRequest = null;
        els.connectionMessage.textContent = data.has_more
          ? "History pagination stopped because the server cursor was invalid."
          : `History received for ${tail[2]} (${request.page + 1} page${request.page ? "s" : ""}).`;
      }
    }
  }
});

els.connectButton.addEventListener("click", () => {
  if (connected || els.connectButton.textContent === "Disconnect") {
    mqtt.disconnect();
    connected = false;
    setConnectionState("offline", "Disconnected by user.");
    return;
  }
  const url = els.brokerUrl.value.trim();
  if (!/^wss?:\/\//i.test(url)) {
    els.connectionMessage.textContent =
      "Enter an MQTT WebSocket URL beginning with ws:// or wss://.";
    return;
  }
  if (location.protocol === "https:" && !url.toLowerCase().startsWith("wss://")) {
    els.connectionMessage.textContent =
      "This HTTPS page requires a secure wss:// MQTT WebSocket endpoint.";
    return;
  }
  const baseTopic = els.baseTopic.value.trim();
  if (
    !baseTopic ||
    baseTopic.startsWith("/") ||
    baseTopic.endsWith("/") ||
    /[+#\u0000]/.test(baseTopic)
  ) {
    els.connectionMessage.textContent =
      "Base topic must be a concrete MQTT topic without wildcards.";
    return;
  }
  saveSettings();
  mqtt.connect({
    url,
    username: els.username.value.trim(),
    password: els.password.value,
    clientId: `ltw-${crypto.randomUUID().replaceAll("-", "").slice(0, 18)}`,
  });
});

function requestHistoryPage(request) {
  const base = els.baseTopic.value.trim() || "lora-tracker";
  const requestId = `web-${Date.now().toString(36)}-${request.page.toString(36)}`;
  pendingHistoryRequest = { ...request, requestId };
  mqtt.publish(
    `${base}/v1/trackers/${request.deviceHash}/history/request`,
    JSON.stringify({
      api_version: 1,
      schema_version: 2,
      request_id: requestId,
      from_unix_ms: request.fromUnixMs,
      to_unix_ms: request.toUnixMs,
      limit: 500,
      cursor: request.cursor,
    }),
  );
  els.connectionMessage.textContent = `Requesting history page ${request.page + 1}…`;
}

els.historyButton.addEventListener("click", () => {
  if (!selectedHash || !connected) {
    els.connectionMessage.textContent =
      "Connect to MQTT and select a tracker first.";
    return;
  }
  const hours = Number(els.historyRange.value || 24);
  requestHistoryPage({
    deviceHash: selectedHash,
    fromUnixMs: Date.now() - hours * 3600_000,
    toUnixMs: Date.now() + 60_000,
    cursor: 0,
    page: 0,
  });
});

els.historyRange.addEventListener("change", () => renderSelectedTracker(true));
els.clearLocalButton.addEventListener("click", async () => {
  if (!selectedHash) return;
  await clearPoints(selectedHash);
  const tracker = trackers.get(selectedHash);
  if (tracker) tracker.latest = null;
  await renderSelectedTracker();
});

setInterval(() => {
  renderTrackerList();
  if (selectedHash) renderSelectedTracker();
}, 30_000);
async function restoreCachedTrackers() {
  try {
    for (const point of await listLatestPoints()) trackerFromPoint(point);
    renderTrackerList();
    if (selectedHash) await renderSelectedTracker(true);
  } catch (error) {
    console.warn("Could not restore cached trackers", error);
  }
}
restoreCachedTrackers();
if ("serviceWorker" in navigator)
  navigator.serviceWorker.register("./sw.js").catch(console.warn);
setConnectionState("offline", "Not connected.");
