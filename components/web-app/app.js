import { MqttWebSocketClient } from "./mqtt.js";
import { putPoint, listPoints, clearPoints } from "./storage.js";
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

const saved = JSON.parse(
  localStorage.getItem("lora-tracker.web.settings") || "{}",
);
els.brokerUrl.value = saved.brokerUrl || "";
els.baseTopic.value = saved.baseTopic || "lora-tracker";
els.username.value = saved.username || "";

let mapLayerType = saved.mapLayerType || "none";
els.mapLayer.value = mapLayerType;

async function loadPmtiles() {
  try {
    const root = await navigator.storage.getDirectory();
    const handle = await root.getFileHandle("map.pmtiles");
    await mapManager.setLayer("pmtiles", handle);
    els.mapLayer.querySelector('option[value="pmtiles"]').disabled = false;
  } catch {
    if (mapLayerType === "pmtiles") mapLayerType = "none";
    els.mapLayer.value = mapLayerType;
    mapManager.setLayer(mapLayerType);
  }
}

if (mapLayerType === "pmtiles") {
  loadPmtiles();
} else {
  mapManager.setLayer(mapLayerType);
  // Still check if we have a PMTiles file available
  navigator.storage
    .getDirectory()
    .then((root) => root.getFileHandle("map.pmtiles"))
    .then(() => {
      els.mapLayer.querySelector('option[value="pmtiles"]').disabled = false;
    })
    .catch(() => {});
}

els.mapLayer.addEventListener("change", async (e) => {
  mapLayerType = e.target.value;
  saveSettings();
  if (mapLayerType === "pmtiles") {
    await loadPmtiles();
  } else {
    mapManager.setLayer(mapLayerType);
  }
});

// --- Onboarding Logic ---
function setBleStatus(msg) {
  els.bleStatus.textContent = msg;
}

function appendBleOutput(msg) {
  els.bleOutput.textContent +=
    (typeof msg === "string" ? msg : JSON.stringify(msg, null, 2)) + "\n";
  els.bleOutput.scrollTop = els.bleOutput.scrollHeight;
}

els.onboardingButton.addEventListener("click", async () => {
  els.onboardingPanel.style.display =
    els.onboardingPanel.style.display === "none" ? "block" : "none";
  if (els.onboardingPanel.style.display === "block") {
    try {
      setBleStatus("Connecting...");
      await onboardingManager.connectTracker();
      setBleStatus("Connected");
      els.bleControls.style.display = "block";
    } catch (e) {
      setBleStatus(`Error: ${e.message}`);
    }
  } else {
    onboardingManager.disconnect();
    setBleStatus("Disconnected");
    els.bleControls.style.display = "none";
  }
});

els.bleClaim.addEventListener("click", async () => {
  try {
    appendBleOutput("Claiming...");
    const result = await onboardingManager.claim(els.blePassword.value);
    appendBleOutput(result);
  } catch (e) {
    appendBleOutput(`Error: ${e.message}`);
  }
});

els.bleAuth.addEventListener("click", async () => {
  try {
    appendBleOutput("Authenticating...");
    const result = await onboardingManager.auth(els.blePassword.value);
    appendBleOutput(result);
    // Auto-fetch config after auth
    els.bleGetConfig.click();
  } catch (e) {
    appendBleOutput(`Error: ${e.message}`);
  }
});

function loadConfigIntoForm(config) {
  try {
    els.bleConfigForm.style.display = "block";
    els.cfgDeviceName.value = config.device_name || "";
    els.cfgWifiSsid.value = config.wifi_ssid || "";
    els.cfgWifiPassword.value = config.wifi_password || "";
    els.cfgTxInterval.value = config.lora_tx_interval_s || "";
  } catch (e) {
    // ignore
  }
}

els.bleGetConfig.addEventListener("click", async () => {
  try {
    appendBleOutput("Fetching config...");
    const result = await onboardingManager.getConfig();
    appendBleOutput(JSON.stringify(result, null, 2));
    loadConfigIntoForm(result);
  } catch (e) {
    appendBleOutput(`Error: ${e.message}`);
  }
});

els.bleSaveConfig.addEventListener("click", async () => {
  try {
    const fields = {};
    if (els.cfgDeviceName.value) fields.device_name = els.cfgDeviceName.value;
    if (els.cfgWifiSsid.value) fields.wifi_ssid = els.cfgWifiSsid.value;
    if (els.cfgWifiPassword.value)
      fields.wifi_password = els.cfgWifiPassword.value;
    if (els.cfgTxInterval.value)
      fields.lora_tx_interval_s = parseInt(els.cfgTxInterval.value, 10);

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
      const root = await navigator.storage.getDirectory();
      const draft = await root.getFileHandle("map.pmtiles", { create: true });
      const writable = await draft.createWritable();
      await writable.write(file);
      await writable.close();

      els.mapLayer.querySelector('option[value="pmtiles"]').disabled = false;
      els.mapLayer.value = "pmtiles";
      mapLayerType = "pmtiles";
      saveSettings();
      await mapManager.setLayer("pmtiles", draft);
    } catch (e) {
      if (e.name !== "AbortError") els.pmtilesInput?.click();
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
    const root = await navigator.storage.getDirectory();
    const draft = await root.getFileHandle("map.pmtiles", { create: true });
    const writable = await draft.createWritable();
    await writable.write(file);
    await writable.close();

    els.mapLayer.querySelector('option[value="pmtiles"]').disabled = false;
    els.mapLayer.value = "pmtiles";
    mapLayerType = "pmtiles";
    saveSettings();
    await mapManager.setLayer("pmtiles", draft);
  } catch (err) {
    alert(`Failed to load PMTiles: ${err.message}`);
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

async function ingestPoint(raw) {
  const point = normalizePoint(raw);
  const tracker = trackerFromPoint(point);
  await putPoint(point);
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
  const start = parts.findIndex((_, i) =>
    base.every((part, j) => parts[i + j] === part),
  );
  if (start < 0) return null;
  const tail = parts.slice(start + base.length);
  return tail;
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
        await ingestPoint(raw);
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
if ("serviceWorker" in navigator)
  navigator.serviceWorker.register("./sw.js").catch(console.warn);
setConnectionState("offline", "Not connected.");
