import { MqttWebSocketClient } from './mqtt.js';
import { putPoint, listPoints, clearPoints } from './storage.js';
import { normalizePoint } from './points.js';

const els = Object.fromEntries([...document.querySelectorAll('[id]')].map(el => [el.id, el]));
const mqtt = new MqttWebSocketClient();
const trackers = new Map();
let selectedHash = null;
let connected = false;
let pendingHistoryRequest = null;

const saved = JSON.parse(localStorage.getItem('equine.web.settings') || '{}');
els.brokerUrl.value = saved.brokerUrl || '';
els.baseTopic.value = saved.baseTopic || 'equine';
els.username.value = saved.username || '';

function saveSettings() {
  localStorage.setItem('equine.web.settings', JSON.stringify({
    brokerUrl: els.brokerUrl.value.trim(),
    baseTopic: els.baseTopic.value.trim() || 'equine',
    username: els.username.value.trim(),
  }));
}

function setConnectionState(state, message = '') {
  els.connectionBadge.className = `badge ${state === 'online' ? 'online' : state === 'offline' ? 'offline' : 'connecting'}`;
  els.connectionBadge.textContent = state === 'online' ? 'Online' : state === 'offline' ? 'Offline' : 'Connecting';
  els.connectionMessage.textContent = message || state;
  els.connectButton.textContent = state === 'online' || state === 'connecting' ? 'Disconnect' : 'Connect';
}

function trackerFromPoint(point) {
  let tracker = trackers.get(point.device_hash);
  if (!tracker) {
    tracker = {
      hash: point.device_hash,
      name: point.device_name || point.device_id || point.device_hash,
      id: point.device_id || '',
      latest: null,
    };
    trackers.set(point.device_hash, tracker);
  }
  tracker.name = point.device_name || tracker.name;
  if (!tracker.latest || point.effective_time_unix_ms >= tracker.latest.effective_time_unix_ms) tracker.latest = point;
  if (!selectedHash) selectedHash = point.device_hash;
  return tracker;
}

async function ingestPoint(raw) {
  const point = normalizePoint(raw);
  trackerFromPoint(point);
  await putPoint(point);
  renderTrackerList();
  if (point.device_hash === selectedHash) await renderSelectedTracker();
}

function ageText(ms) {
  if (!ms) return '—';
  const seconds = Math.max(0, Math.floor((Date.now() - ms) / 1000));
  if (seconds < 60) return `${seconds}s`;
  if (seconds < 3600) return `${Math.floor(seconds / 60)}m`;
  if (seconds < 86400) return `${Math.floor(seconds / 3600)}h`;
  return `${Math.floor(seconds / 86400)}d`;
}

function formatTime(ms) {
  if (!ms) return '—';
  return new Intl.DateTimeFormat(undefined, { dateStyle: 'short', timeStyle: 'medium' }).format(new Date(ms));
}

function renderTrackerList() {
  els.trackerList.innerHTML = '';
  const sorted = [...trackers.values()].sort((a, b) => (b.latest?.effective_time_unix_ms || 0) - (a.latest?.effective_time_unix_ms || 0));
  for (const tracker of sorted) {
    const node = els.trackerTemplate.content.firstElementChild.cloneNode(true);
    node.classList.toggle('active', tracker.hash === selectedHash);
    const fresh = tracker.latest && Date.now() - tracker.latest.effective_time_unix_ms < 10 * 60_000;
    node.classList.toggle('fresh', Boolean(fresh));
    node.querySelector('.tracker-name').textContent = tracker.name;
    node.querySelector('.tracker-hash').textContent = tracker.hash;
    node.querySelector('.tracker-age').textContent = ageText(tracker.latest?.effective_time_unix_ms);
    node.addEventListener('click', async () => {
      selectedHash = tracker.hash;
      renderTrackerList();
      await renderSelectedTracker();
    });
    els.trackerList.append(node);
  }
  els.trackerCount.textContent = String(sorted.length);
}

function drawGrid() {
  if (els.gridLayer.childElementCount) return;
  for (let x = 100; x < 1000; x += 100) {
    const line = document.createElementNS('http://www.w3.org/2000/svg', 'line');
    line.setAttribute('x1', x); line.setAttribute('x2', x); line.setAttribute('y1', 0); line.setAttribute('y2', 560);
    els.gridLayer.append(line);
  }
  for (let y = 80; y < 560; y += 80) {
    const line = document.createElementNS('http://www.w3.org/2000/svg', 'line');
    line.setAttribute('x1', 0); line.setAttribute('x2', 1000); line.setAttribute('y1', y); line.setAttribute('y2', y);
    els.gridLayer.append(line);
  }
}

function drawRoute(points) {
  drawGrid();
  if (!points.length) {
    els.routeLine.setAttribute('points', '');
    els.routeStart.setAttribute('cx', -20); els.routeEnd.setAttribute('cx', -20);
    els.routeBounds.textContent = '—';
    return;
  }
  let minLat = Infinity, maxLat = -Infinity, minLon = Infinity, maxLon = -Infinity;
  for (const p of points) {
    minLat = Math.min(minLat, p.latitude); maxLat = Math.max(maxLat, p.latitude);
    minLon = Math.min(minLon, p.longitude); maxLon = Math.max(maxLon, p.longitude);
  }
  const latSpan = Math.max(maxLat - minLat, 0.0001);
  const lonSpan = Math.max(maxLon - minLon, 0.0001);
  const projected = points.map(p => ({
    x: 40 + ((p.longitude - minLon) / lonSpan) * 920,
    y: 520 - ((p.latitude - minLat) / latSpan) * 480,
  }));
  els.routeLine.setAttribute('points', projected.map(p => `${p.x.toFixed(1)},${p.y.toFixed(1)}`).join(' '));
  const start = projected[0], end = projected.at(-1);
  els.routeStart.setAttribute('cx', start.x); els.routeStart.setAttribute('cy', start.y);
  els.routeEnd.setAttribute('cx', end.x); els.routeEnd.setAttribute('cy', end.y);
  els.routeBounds.textContent = `${minLat.toFixed(5)}, ${minLon.toFixed(5)} → ${maxLat.toFixed(5)}, ${maxLon.toFixed(5)}`;
}

async function renderSelectedTracker() {
  const tracker = trackers.get(selectedHash);
  if (!tracker) return;
  const latest = tracker.latest;
  els.selectedTrackerName.textContent = tracker.name;
  els.lastSeen.textContent = latest ? ageText(latest.effective_time_unix_ms) + ' ago' : '—';
  els.timeSource.textContent = latest ? `${formatTime(latest.effective_time_unix_ms)} · ${latest.timestamp_valid ? 'GNSS time' : 'receive-time fallback'}` : 'No data';
  els.battery.textContent = latest ? `${latest.battery_level}%` : '—';
  els.batteryDetail.textContent = latest ? `Boot ${latest.boot_id} · seq ${latest.seq}` : '—';
  els.distance.textContent = latest ? `${(latest.dist_m / 1000).toFixed(2)} km` : '—';
  els.rssi.textContent = latest ? `${latest.rssi} dBm` : '—';
  els.gateway.textContent = latest ? latest.gateway_id || latest.gateway_hash || 'unknown gateway' : '—';

  const hours = Number(els.historyRange.value || 24);
  const points = await listPoints(selectedHash, Date.now() - hours * 3600_000, Date.now() + 3600_000);
  drawRoute(points);
  els.pointCount.textContent = `${points.length} point${points.length === 1 ? '' : 's'}`;
  els.eventTable.innerHTML = '';
  for (const point of points.slice(-30).reverse()) {
    const row = document.createElement('tr');
    row.innerHTML = `<td>${formatTime(point.effective_time_unix_ms)}</td><td><code>${point.latitude.toFixed(6)}, ${point.longitude.toFixed(6)}</code></td><td>${point.battery_level}%</td><td>${point.rssi} dBm</td><td>${point.gateway_id || point.gateway_hash || '—'}</td>`;
    els.eventTable.append(row);
  }
}

function parseTopic(topic) {
  const base = (els.baseTopic.value.trim() || 'equine').split('/').filter(Boolean);
  const parts = topic.split('/');
  const start = parts.findIndex((_, i) => base.every((part, j) => parts[i + j] === part));
  if (start < 0) return null;
  const tail = parts.slice(start + base.length);
  return tail;
}

mqtt.addEventListener('status', event => {
  const state = event.detail.state;
  connected = state === 'online';
  setConnectionState(state, state === 'reconnecting' ? `Reconnecting in ${Math.round(event.detail.delay / 1000)} seconds…` : state === 'online' ? 'Subscribed to tracker telemetry and history responses.' : state === 'connecting' ? 'Opening secure MQTT WebSocket…' : 'Not connected.');
  if (state === 'online') {
    const base = els.baseTopic.value.trim() || 'equine';
    mqtt.subscribe(
      `${base}/v1/trackers/+/events/point`,
      `${base}/v1/trackers/+/state`,
      `${base}/v1/trackers/+/history/response/+`
    );
  }
});

mqtt.addEventListener('error', event => { els.connectionMessage.textContent = event.detail.message; });
mqtt.addEventListener('message', async event => {
  const { topic, payload } = event.detail;
  let data;
  try { data = JSON.parse(payload); } catch { return; }
  const tail = parseTopic(topic);
  if (!tail || tail[0] !== 'v1' || tail[1] !== 'trackers') return;
  if (tail[3] === 'events' && tail[4] === 'point' || tail[3] === 'state') {
    try { await ingestPoint(data); } catch (error) { console.warn(error); }
  } else if (tail[3] === 'history' && tail[4] === 'response') {
    if (!data.ok || !Array.isArray(data.points)) return;
    for (const raw of data.points) {
      try { await ingestPoint(raw); } catch (error) { console.warn(error); }
    }
    if (data.final && pendingHistoryRequest === data.request_id) {
      pendingHistoryRequest = null;
      els.connectionMessage.textContent = `History received for ${tail[2]}.`;
      if (data.has_more) els.connectionMessage.textContent += ' More data is available; request pagination is retained for the next app iteration.';
    }
  }
});

els.connectButton.addEventListener('click', () => {
  if (connected || els.connectButton.textContent === 'Disconnect') {
    mqtt.disconnect();
    connected = false;
    setConnectionState('offline', 'Disconnected by user.');
    return;
  }
  const url = els.brokerUrl.value.trim();
  if (!/^wss?:\/\//i.test(url)) {
    els.connectionMessage.textContent = 'Enter an MQTT WebSocket URL beginning with ws:// or wss://.';
    return;
  }
  saveSettings();
  mqtt.connect({
    url,
    username: els.username.value.trim(),
    password: els.password.value,
    clientId: `equine-web-${crypto.randomUUID().replaceAll('-', '').slice(0, 18)}`,
  });
});

els.historyButton.addEventListener('click', () => {
  if (!selectedHash || !connected) {
    els.connectionMessage.textContent = 'Connect to MQTT and select a tracker first.';
    return;
  }
  const base = els.baseTopic.value.trim() || 'equine';
  const hours = Number(els.historyRange.value || 24);
  const requestId = `web-${Date.now().toString(36)}`;
  pendingHistoryRequest = requestId;
  mqtt.publish(`${base}/v1/trackers/${selectedHash}/history/request`, JSON.stringify({
    api_version: 1,
    schema_version: 2,
    request_id: requestId,
    from_unix_ms: Date.now() - hours * 3600_000,
    to_unix_ms: Date.now() + 60_000,
    limit: 500,
    cursor: 0,
  }));
  els.connectionMessage.textContent = `Requested ${hours} hours of history…`;
});

els.historyRange.addEventListener('change', renderSelectedTracker);
els.clearLocalButton.addEventListener('click', async () => {
  if (!selectedHash) return;
  await clearPoints(selectedHash);
  const tracker = trackers.get(selectedHash);
  if (tracker) tracker.latest = null;
  await renderSelectedTracker();
});

setInterval(() => { renderTrackerList(); if (selectedHash) renderSelectedTracker(); }, 30_000);
if ('serviceWorker' in navigator) navigator.serviceWorker.register('./sw.js').catch(console.warn);
setConnectionState('offline', 'Not connected.');
drawGrid();
