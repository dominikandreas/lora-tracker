import { createDefaultScenario, radioDefaults, repeaterDefaults, trackerDefaults } from './default-scenario.js';
import { exactDistanceM, geoPosition, isPolygonObstacle, pointInObstacle, pointInPolygon, polygonCenter } from './geometry.js';

const SCENARIO_STORAGE_KEY = 'lora-tracker.network-lab.scenario.v2';
const VIEW_STORAGE_KEY = 'lora-tracker.network-lab.view.v1';
const worker = new Worker(new URL('./simulator-worker.js', import.meta.url), { type: 'module' });
const canvas = document.querySelector('#map');
const context = canvas.getContext('2d');
const mapWrap = document.querySelector('#map-wrap');
const eventsElement = document.querySelector('#events');
const form = document.querySelector('#device-form');
const toast = document.querySelector('#toast');

let snapshot;
let initialScenario = loadStoredScenario() ?? createDefaultScenario();
let selectedId = null;
let pendingSelection = null;
let tool = 'select';
let playing = false;
let dragging = false;
let editTarget = null;
let knownEventId = 0;
let visibleAfterEventId = 0;
let backgroundImage = null;
let trackDeviceId = null;
let trackTimeS = null;
let archiveGatewayId = null;
let archiveTrackerId = null;
let panning = false;
let panOrigin = null;
let persistTimer;
let view = loadStoredView();
const satelliteTiles = new Map();
const animations = [];
const txOrigins = new Map();

const $ = (selector) => document.querySelector(selector);
const clamp = (value, min, max) => Math.max(min, Math.min(max, value));
const copy = (value) => JSON.parse(JSON.stringify(value));

function loadStoredScenario() {
  try { const value = JSON.parse(localStorage.getItem(SCENARIO_STORAGE_KEY)); return value?.schemaVersion === 2 ? value : null; } catch { return null; }
}
function loadStoredView() {
  try { const value = JSON.parse(localStorage.getItem(VIEW_STORAGE_KEY)); return value && Number.isFinite(value.zoom) && Number.isFinite(value.panX) && Number.isFinite(value.panY) ? value : { zoom: 1, panX: 0, panY: 0 }; } catch { return { zoom: 1, panX: 0, panY: 0 }; }
}
function persistView() { try { localStorage.setItem(VIEW_STORAGE_KEY, JSON.stringify(view)); } catch { /* Private browsing may deny storage. */ } }
function scheduleScenarioPersist(scenario) {
  clearTimeout(persistTimer);
  persistTimer = setTimeout(() => { try { localStorage.setItem(SCENARIO_STORAGE_KEY, JSON.stringify(scenario)); } catch { /* Storage is optional. */ } }, 250);
}

form.addEventListener('click', (event) => {
  const button = event.target.closest('[data-remove-waypoint]');
  if (!button || !selectedId) return;
  event.preventDefault();
  post('remove-waypoint', { id: selectedId, index: Number(button.dataset.removeWaypoint) });
});

function notify(message) {
  toast.textContent = message;
  toast.classList.add('show');
  setTimeout(() => toast.classList.remove('show'), 2200);
}

function post(type, payload = {}) { worker.postMessage({ type, ...payload }); }

worker.onmessage = ({ data }) => {
  if (data.type === 'snapshot') {
    snapshot = data.snapshot;
    scheduleScenarioPersist(snapshot.scenario);
    if (pendingSelection) { selectedId = pendingSelection; pendingSelection = null; }
    consumeAnimations(snapshot.events);
    updateStatus();
    renderEvents();
    const active = document.activeElement;
    if (!form.contains(active) || active?.tagName === 'BUTTON') renderInspector();
  } else if (data.type === 'ready') {
    $('#core-status').className = 'core-status ready';
    $('#core-status').innerHTML = `<span></span>Firmware core WASM v${data.core.version}`;
    $('#play').disabled = false; $('#step').disabled = false; $('#reset').disabled = false;
  } else if (data.type === 'play-state') {
    playing = data.playing;
    $('#play').textContent = playing ? 'Pause' : 'Run';
  } else if (data.type === 'error') {
    if (data.fatal) {
      $('#core-status').className = 'core-status error';
      $('#core-status').innerHTML = `<span></span>${escapeHtml(data.message)}`;
    } else if (document.activeElement instanceof HTMLElement) {
      document.activeElement.blur();
    }
    notify(data.message);
  }
};

function escapeHtml(value) {
  return String(value).replace(/[&<>"]/g, (char) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' })[char]);
}

function consumeAnimations(events) {
  for (const event of events) {
    if (event.id <= knownEventId) continue;
    knownEventId = Math.max(knownEventId, event.id);
    if (event.type === 'radio-tx') {
      txOrigins.set(event.frameKey, event.from);
      animations.push({ kind: event.message.includes('ACK') ? 'ack' : 'wave', from: event.from, started: performance.now(), duration: 1400 });
    } else if (event.type === 'radio-rx' && txOrigins.has(event.frameKey)) {
      const device = snapshot?.devices.find((item) => item.id === event.deviceId);
      if (device) animations.push({ kind: event.message.includes('ACK') ? 'ack' : 'path', from: txOrigins.get(event.frameKey), to: device, started: performance.now(), duration: 1200 });
    }
  }
}

function formatTime(seconds, startHour = 0) {
  const absolute = Math.floor(startHour * 3600 + seconds);
  const day = Math.floor(absolute / 86400) + 1;
  const within = ((absolute % 86400) + 86400) % 86400;
  const h = String(Math.floor(within / 3600)).padStart(2, '0');
  const m = String(Math.floor(within % 3600 / 60)).padStart(2, '0');
  const s = String(within % 60).padStart(2, '0');
  return { full: `Day ${day} · ${h}:${m}:${s}`, short: `${h}:${m}:${s}` };
}

function updateStatus() {
  if (!snapshot) return;
  $('#scenario-name').textContent = snapshot.scenario.name;
  $('#clock').textContent = formatTime(snapshot.timeS, snapshot.scenario.clock.startHour).full;
  $('#weather').textContent = `${snapshot.environment.temperatureC.toFixed(1)} °C · ${snapshot.environment.humidityPct.toFixed(0)}% RH · wetness ${snapshot.environment.wetness.toFixed(2)}`;
  $('#mqtt').textContent = `MQTT ${snapshot.mqtt.online ? 'online' : 'offline'} · ${snapshot.mqtt.archivedPoints} archived`;
  $('#daylight-label').textContent = snapshot.environment.daylight > .1 ? 'Day' : 'Night';
  const e = snapshot.scenario.environment;
  setValue('#day-temp', e.dayTemperatureC); setValue('#night-temp', e.nightTemperatureC);
  setValue('#day-humidity', e.dayHumidityPct); setValue('#night-humidity', e.nightHumidityPct);
  setValue('#wetness', e.foliageWetness); $('#mqtt-online').checked = snapshot.mqtt.online;
  setValue('#mqtt-latency', snapshot.mqtt.latencyMs);
  setValue('#archive-latency', snapshot.mqtt.archiveLatencyMs);
  const map = snapshot.scenario.map;
  setValue('#map-mode', map.mode); setValue('#map-latitude', map.centerLat); setValue('#map-longitude', map.centerLng); setValue('#map-zoom', map.zoom);
  $('#map-location').textContent = `${map.mode === 'satellite' ? 'Satellite imagery: Esri World Imagery.' : 'Metre grid.'} Centre ${map.centerLat.toFixed(6)}, ${map.centerLng.toFixed(6)} · points use great-circle distances.`;
  updateHistoryControls();
}

function setOptions(element, items, selected) {
  const current = selected && items.some((item) => item.id === selected) ? selected : items[0]?.id ?? '';
  if (element.value !== current || element.options.length !== items.length) element.innerHTML = items.map((item) => `<option value="${escapeHtml(item.id)}">${escapeHtml(item.name)}</option>`).join('');
  element.value = current;
  return current;
}

function updateHistoryControls() {
  const trackers = snapshot.devices.filter((device) => device.role === 'tracker').map((device) => ({ id: device.id, name: device.name }));
  const gateways = snapshot.devices.filter((device) => device.role === 'receiver').map((device) => ({ id: device.id, name: device.name }));
  trackDeviceId = setOptions($('#track-device'), trackers, trackDeviceId);
  archiveTrackerId = setOptions($('#archive-tracker'), trackers, archiveTrackerId ?? trackDeviceId);
  archiveGatewayId = setOptions($('#archive-gateway'), gateways, archiveGatewayId);
  trackTimeS ??= snapshot.timeS;
  trackTimeS = Math.min(trackTimeS, snapshot.timeS);
  const slider = $('#track-time'); slider.max = String(Math.max(1, Math.floor(snapshot.timeS))); slider.value = String(trackTimeS);
  const local = snapshot.trackHistory[trackDeviceId] ?? [];
  const visible = local.filter((point) => point.timeS <= trackTimeS);
  $('#track-summary').textContent = `${visible.length}/${local.length} points · ${formatTime(trackTimeS, snapshot.scenario.clock.startHour).short}`;
  const archived = snapshot.archive.filter((point) => point.gatewayId === archiveGatewayId && point.deviceId === archiveTrackerId);
  $('#archive-summary').textContent = `${archived.length} committed`;
}

function setValue(selector, value) {
  const element = $(selector);
  if (document.activeElement !== element) element.value = value;
}

function renderEvents() {
  if (!snapshot) return;
  const selected = snapshot.events.filter((event) => event.id > visibleAfterEventId).slice(-120).reverse();
  eventsElement.innerHTML = selected.map((event) => {
    const time = formatTime(event.timeS, snapshot.scenario.clock.startHour).short;
    return `<div class="event ${escapeHtml(event.severity ?? '')}"><time>${time}</time><span class="kind">${escapeHtml(event.type)}</span><span>${escapeHtml(event.message)}</span><span class="device">${escapeHtml(event.deviceId ?? '')}</span></div>`;
  }).join('') || '<div class="event"><span></span><span class="kind">empty</span><span>No events in this view</span></div>';
  if ($('#follow-log').checked) eventsElement.scrollTop = 0;
}

function worldPoint(event) {
  const rect = canvas.getBoundingClientRect();
  const sx = rect.width / snapshot.scenario.world.widthM * view.zoom;
  const sy = rect.height / snapshot.scenario.world.heightM * view.zoom;
  return {
    x: clamp((event.clientX - rect.left - rect.width / 2 - view.panX) / sx + snapshot.scenario.world.widthM / 2, 0, snapshot.scenario.world.widthM),
    y: clamp((event.clientY - rect.top - rect.height / 2 - view.panY) / sy + snapshot.scenario.world.heightM / 2, 0, snapshot.scenario.world.heightM),
  };
}

function hitTest(point) {
  let best = null;
  let bestDistance = 28 / (canvas.clientWidth / snapshot.scenario.world.widthM * view.zoom);
  for (const device of snapshot.devices) {
    const gap = Math.hypot(device.x - point.x, device.y - point.y);
    if (gap < bestDistance) { best = device.id; bestDistance = gap; }
  }
  if (best) return best;
  for (const obstacle of [...snapshot.scenario.obstacles].reverse()) {
    if (obstacle.type === 'tree') {
      if (Math.hypot(obstacle.x - point.x, obstacle.y - point.y) <= obstacle.radius * 1.8) return obstacle.id;
    } else if (pointInObstacle(point, obstacle)) return obstacle.id;
  }
  return null;
}

function pointHit(point, target, radiusPx = 12) {
  return Math.hypot(point.x - target.x, point.y - target.y) <= radiusPx / (canvas.clientWidth / snapshot.scenario.world.widthM * view.zoom);
}

function polygonEdgeHit(point, points) {
  const threshold = 12 / (canvas.clientWidth / snapshot.scenario.world.widthM * view.zoom);
  for (let index = 0; index < points.length; index += 1) {
    const a = points[index]; const b = points[(index + 1) % points.length];
    const mid = { x: (a.x + b.x) / 2, y: (a.y + b.y) / 2 };
    if (pointHit(point, mid, 12)) return { index, point: mid };
    const dx = b.x - a.x; const dy = b.y - a.y;
    const lengthSq = dx * dx + dy * dy;
    const t = lengthSq ? clamp(((point.x - a.x) * dx + (point.y - a.y) * dy) / lengthSq, 0, 1) : 0;
    if (Math.hypot(point.x - (a.x + dx * t), point.y - (a.y + dy * t)) <= threshold) return { index, point: { x: a.x + dx * t, y: a.y + dy * t } };
  }
  return null;
}

canvas.addEventListener('pointerdown', (event) => {
  if (!snapshot) return;
  if (tool === 'pan' || event.button === 1 || event.button === 2) {
    panning = true; panOrigin = { x: event.clientX, y: event.clientY }; canvas.setPointerCapture(event.pointerId); return;
  }
  const point = worldPoint(event);
  if (tool === 'select') {
    const selectedDevice = snapshot.devices.find((item) => item.id === selectedId && item.role === 'tracker');
    if (selectedDevice) {
      const waypointIndex = selectedDevice.waypoints.findIndex((waypoint) => pointHit(point, waypoint));
      if (waypointIndex >= 0) { dragging = true; editTarget = { kind: 'waypoint', id: selectedDevice.id, index: waypointIndex }; canvas.setPointerCapture(event.pointerId); return; }
    }
    const selectedObstacle = snapshot.scenario.obstacles.find((item) => item.id === selectedId && isPolygonObstacle(item));
    if (selectedObstacle) {
      const pointIndex = selectedObstacle.points.findIndex((corner) => pointHit(point, corner));
      if (pointIndex >= 0) { dragging = true; editTarget = { kind: 'polygon', id: selectedObstacle.id, index: pointIndex }; canvas.setPointerCapture(event.pointerId); return; }
      const edge = polygonEdgeHit(point, selectedObstacle.points);
      if (edge) {
        const points = copy(selectedObstacle.points); points.splice(edge.index + 1, 0, edge.point);
        editTarget = { kind: 'polygon', id: selectedObstacle.id, index: edge.index + 1 };
        dragging = true; canvas.setPointerCapture(event.pointerId);
        post('update-entity', { id: selectedObstacle.id, changes: { points } }); return;
      }
    }
    selectedId = hitTest(point);
    dragging = Boolean(selectedId && snapshot.devices.some((item) => item.id === selectedId));
    canvas.setPointerCapture(event.pointerId);
    renderInspector();
  } else if (tool === 'waypoint') {
    const selected = snapshot.devices.find((item) => item.id === selectedId);
    if (!selected || selected.role !== 'tracker') return notify('Select a tracker before adding waypoints');
    post('add-waypoint', { id: selected.id, point });
  } else {
    addAt(tool, point);
  }
});

canvas.addEventListener('pointermove', (event) => {
  if (panning && panOrigin) {
    view.panX += event.clientX - panOrigin.x; view.panY += event.clientY - panOrigin.y; panOrigin = { x: event.clientX, y: event.clientY }; persistView(); return;
  }
  if (!dragging || !selectedId || !snapshot) return;
  const point = worldPoint(event);
  if (editTarget) {
    if (editTarget.kind === 'waypoint') {
      const device = snapshot.devices.find((item) => item.id === editTarget.id);
      if (device) device.waypoints[editTarget.index] = point;
    } else {
      const obstacle = snapshot.scenario.obstacles.find((item) => item.id === editTarget.id);
      if (obstacle) obstacle.points[editTarget.index] = point;
    }
    return;
  }
  const device = snapshot.devices.find((item) => item.id === selectedId);
  if (!device) return;
  device.x = point.x; device.y = point.y;
});
canvas.addEventListener('pointerup', (event) => {
  if (panning) { panning = false; panOrigin = null; persistView(); return; }
  if (dragging && selectedId) {
    const point = worldPoint(event);
    if (editTarget?.kind === 'waypoint') post('update-waypoint', { id: editTarget.id, index: editTarget.index, point });
    else if (editTarget?.kind === 'polygon') {
      const obstacle = snapshot.scenario.obstacles.find((item) => item.id === editTarget.id);
      post('update-entity', { id: editTarget.id, changes: { points: obstacle.points } });
    } else post('update-entity', { id: selectedId, changes: point });
  }
  dragging = false; editTarget = null;
});
canvas.addEventListener('contextmenu', (event) => event.preventDefault());
function zoomMap(factor, clientX, clientY) {
  if (!snapshot) return;
  const rect = canvas.getBoundingClientRect();
  const point = worldPoint({ clientX, clientY });
  view.zoom = clamp(view.zoom * factor, .35, 8);
  const sx = rect.width / snapshot.scenario.world.widthM * view.zoom;
  const sy = rect.height / snapshot.scenario.world.heightM * view.zoom;
  view.panX = clientX - rect.left - rect.width / 2 - (point.x - snapshot.scenario.world.widthM / 2) * sx;
  view.panY = clientY - rect.top - rect.height / 2 - (point.y - snapshot.scenario.world.heightM / 2) * sy;
  persistView();
}
canvas.addEventListener('wheel', (event) => {
  if (!snapshot) return;
  event.preventDefault();
  zoomMap(event.deltaY < 0 ? 1.15 : 1 / 1.15, event.clientX, event.clientY);
}, { passive: false });

function nextId(prefix) {
  const all = [...snapshot.devices.map((item) => item.id), ...snapshot.scenario.obstacles.map((item) => item.id)];
  let index = 1;
  while (all.includes(`${prefix}-${index}`)) index += 1;
  return `${prefix}-${index}`;
}

function addAt(kind, point) {
  let entity;
  if (['tracker', 'repeater', 'receiver'].includes(kind)) {
    const id = nextId(kind === 'repeater' ? 'relay' : kind === 'receiver' ? 'gateway' : 'tracker');
    entity = { id, role: kind, name: kind === 'repeater' ? 'New relay' : kind === 'receiver' ? 'New receiver' : 'New tracker',
      ...point, antennaGainDbi: 2.15, cableLossDb: 0, antennaHeightM: kind === 'tracker' ? 1.2 : 4, radio: radioDefaults() };
    if (kind === 'tracker') Object.assign(entity, { config: trackerDefaults(), speedKmh: 4, waypoints: [point], pathMode: 'loop', batteryPct: 100 });
    if (kind === 'repeater') entity.config = repeaterDefaults();
    if (kind === 'receiver') entity.config = {
      mqttConnected: true,
      registeredTrackerIds: snapshot.devices.filter((item) => item.role === 'tracker').map((item) => item.id),
    };
  } else {
    const id = nextId(kind.replace('building-', 'building'));
    if (kind === 'tree') entity = { id, type: kind, ...point, radius: 9, label: 'Tree' };
    else { const halfWidth = kind === 'forest' ? 90 : kind === 'building-large' ? 60 : 35; const halfHeight = kind === 'forest' ? 65 : kind === 'building-large' ? 45 : 27.5; entity = { id, type: kind,
      points: [{ x: point.x - halfWidth, y: point.y - halfHeight }, { x: point.x + halfWidth, y: point.y - halfHeight }, { x: point.x + halfWidth, y: point.y + halfHeight }, { x: point.x - halfWidth, y: point.y + halfHeight }],
      density: kind === 'forest' ? .7 : undefined,
      label: kind === 'forest' ? 'Forest' : kind === 'building-large' ? 'Large building' : 'Small building' }; }
  }
  pendingSelection = entity.id;
  post('add-entity', { entity });
  setTool('select');
}

document.querySelectorAll('.tool').forEach((button) => button.addEventListener('click', () => setTool(button.dataset.tool)));
function setTool(value) {
  tool = value;
  document.querySelectorAll('.tool').forEach((button) => button.classList.toggle('active', button.dataset.tool === value));
  canvas.style.cursor = value === 'pan' ? 'grab' : value === 'select' ? 'default' : 'crosshair';
}

$('#play').addEventListener('click', () => post(playing ? 'pause' : 'play'));
$('#step').addEventListener('click', () => post('step', { seconds: 60 }));
$('#speed').addEventListener('change', (event) => post('speed', { speed: Number(event.target.value) }));
$('#reset').addEventListener('click', () => { knownEventId = 0; selectedId = null; post('reset', { scenario: initialScenario }); });
$('#clear-log').addEventListener('click', () => { visibleAfterEventId = knownEventId; renderEvents(); });
$('#reset-view').addEventListener('click', () => { view = { zoom: 1, panX: 0, panY: 0 }; persistView(); });
for (const [selector, factor] of [['#zoom-in', 1.25], ['#zoom-out', .8]]) $(selector).addEventListener('click', () => {
  const rect = canvas.getBoundingClientRect(); zoomMap(factor, rect.left + rect.width / 2, rect.top + rect.height / 2);
});

for (const [selector, path] of [['#day-temp', 'dayTemperatureC'], ['#night-temp', 'nightTemperatureC'], ['#day-humidity', 'dayHumidityPct'], ['#night-humidity', 'nightHumidityPct'], ['#wetness', 'foliageWetness']]) {
  $(selector).addEventListener('change', (event) => post('environment', { patch: { environment: { [path]: Number(event.target.value) } } }));
}
$('#mqtt-online').addEventListener('change', (event) => post('environment', { patch: { mqtt: { online: event.target.checked } } }));
$('#mqtt-latency').addEventListener('change', (event) => post('environment', { patch: { mqtt: { latencyMs: Number(event.target.value) } } }));
$('#archive-latency').addEventListener('change', (event) => post('environment', { patch: { mqtt: { archiveLatencyMs: Number(event.target.value) } } }));
for (const [selector, key] of [['#map-mode', 'mode'], ['#map-latitude', 'centerLat'], ['#map-longitude', 'centerLng'], ['#map-zoom', 'zoom']]) {
  $(selector).addEventListener('change', (event) => post('environment', { patch: { map: { [key]: key === 'mode' ? event.target.value : Number(event.target.value) } } }));
}
$('#locate').addEventListener('click', () => {
  if (!navigator.geolocation) return notify('This browser does not provide location access');
  navigator.geolocation.getCurrentPosition((position) => {
    post('environment', { patch: { map: { centerLat: position.coords.latitude, centerLng: position.coords.longitude, mode: 'satellite' } } });
  }, (error) => notify(`Location unavailable: ${error.message}`), { enableHighAccuracy: true, timeout: 15000, maximumAge: 300000 });
});
$('#track-device').addEventListener('change', (event) => { trackDeviceId = event.target.value; updateHistoryControls(); });
$('#track-time').addEventListener('input', (event) => { trackTimeS = Number(event.target.value); updateHistoryControls(); });
$('#archive-gateway').addEventListener('change', (event) => { archiveGatewayId = event.target.value; updateHistoryControls(); });
$('#archive-tracker').addEventListener('change', (event) => { archiveTrackerId = event.target.value; updateHistoryControls(); });

$('#export').addEventListener('click', () => {
  if (!snapshot) return;
  const blob = new Blob([JSON.stringify(snapshot.scenario, null, 2)], { type: 'application/json' });
  const link = document.createElement('a'); link.href = URL.createObjectURL(blob);
  link.download = `${snapshot.scenario.name.toLowerCase().replace(/[^a-z0-9]+/g, '-')}.json`;
  link.click(); URL.revokeObjectURL(link.href);
});
$('#import').addEventListener('change', async (event) => {
  const file = event.target.files[0]; if (!file) return;
  try {
    const scenario = JSON.parse(await file.text());
    initialScenario = copy(scenario); selectedId = null; knownEventId = 0;
    post('replace-scenario', { scenario });
  } catch (error) { notify(`Invalid scenario: ${error.message}`); }
  event.target.value = '';
});
$('#background').addEventListener('change', (event) => {
  const file = event.target.files[0]; if (!file) return;
  const reader = new FileReader();
  reader.onload = () => { const image = new Image(); image.onload = () => { backgroundImage = image; }; image.src = reader.result; };
  reader.readAsDataURL(file);
});

function field(label, path, value, options = {}) {
  const type = options.type ?? 'number';
  const wide = options.wide ? 'wide' : '';
  if (type === 'checkbox') return `<label class="switch ${wide}"><input data-path="${path}" type="checkbox" ${value ? 'checked' : ''}><span>${label}</span></label>`;
  if (type === 'select') return `<label class="${wide}">${label}<select data-path="${path}">${options.values.map((item) => `<option value="${item}" ${item === value ? 'selected' : ''}>${item}</option>`).join('')}</select></label>`;
  return `<label class="${wide}">${label}<input data-path="${path}" type="${type}" value="${escapeHtml(Array.isArray(value) ? value.join(', ') : value)}" ${options.step ? `step="${options.step}"` : ''}></label>`;
}

function renderInspector() {
  if (!snapshot) return;
  const entity = snapshot.devices.find((item) => item.id === selectedId) ?? snapshot.scenario.obstacles.find((item) => item.id === selectedId);
  $('#selection-empty').hidden = Boolean(entity);
  form.hidden = !entity;
  $('#selection-title').textContent = entity?.name ?? entity?.label ?? 'Nothing selected';
  $('#selection-status').textContent = entity?.role ?? entity?.type ?? '';
  if (!entity) { form.innerHTML = ''; $('#link-detail').hidden = true; return; }
  if (entity.role) renderDeviceForm(entity); else renderObstacleForm(entity);
  if (entity.role) renderLocalState(entity);
  const locationPoint = entity.role || entity.type === 'tree' ? entity : polygonCenter(entity.points);
  const location = geoPosition(snapshot.scenario, locationPoint);
  if (location) form.insertAdjacentHTML('beforeend', `<div class="wide point-location">Map point ${location.latitude.toFixed(6)}, ${location.longitude.toFixed(6)} · ${exactDistanceM(snapshot.scenario, locationPoint, { x: snapshot.scenario.world.widthM / 2, y: snapshot.scenario.world.heightM / 2 }).toFixed(1)} m from map centre</div><button class="remove-entity wide" type="button" id="remove-entity">Remove ${escapeHtml(entity.name ?? entity.label ?? entity.id)}</button>`);
  $('#remove-entity')?.addEventListener('click', () => {
    if (entity.role === 'receiver' && snapshot.devices.filter((item) => item.role === 'receiver').length <= 1) return notify('A scenario needs at least one receiver');
    selectedId = null; post('remove-entity', { id: entity.id });
  });
  const latestLink = [...snapshot.events].reverse().find((event) => event.deviceId === entity.id && event.link);
  if (latestLink) {
    const link = latestLink.link;
    $('#link-detail').hidden = false;
    $('#link-detail').innerHTML = `<strong>Latest link budget</strong><br>${link.rangeM.toFixed(0)} m · ${link.rxPowerDbm.toFixed(1)} dBm received · ${link.marginDb.toFixed(1)} dB margin<br>Free space ${link.freeSpaceLoss.toFixed(1)} dB · ground ${link.excessGroundLoss.toFixed(1)} dB · height ${link.heightBenefitDb.toFixed(1)} dB benefit · forest ${link.forestLoss.toFixed(1)} dB · buildings ${link.buildingLoss.toFixed(1)} dB · trees ${link.treeLoss.toFixed(1)} dB · seeded fading ${link.fadingDb.toFixed(1)} dB · atmosphere ${link.atmosphereLoss.toFixed(4)} dB`;
  } else $('#link-detail').hidden = true;
}

function renderLocalState(device) {
  const runtime = device.runtime;
  const state = {
    status: runtime.status,
    nextWakeS: runtime.nextWakeS,
    queueDepth: runtime.queue.length,
    airtimeTokensMs: Math.round(runtime.airtimeTokensMs),
    airtimeCapacityMs: Math.round(runtime.airtimeCapacityMs),
    failures: runtime.failures,
    noFixCycles: runtime.noFixCycles,
    stationaryStreak: runtime.stationaryStreak,
    pendingRelays: Object.keys(runtime.pending).length,
    seenFrames: Object.keys(runtime.seen).length,
    inflightFrames: Object.keys(runtime.inflight).length,
    statistics: runtime.stats,
    queue: runtime.queue.map(({ seq, timeS, batteryPct }) => ({ seq, timeS, batteryPct })),
  };
  form.insertAdjacentHTML('beforeend', `<details class="local-state wide"><summary>Local device state</summary><pre>${escapeHtml(JSON.stringify(state, null, 2))}</pre></details>`);
}

function renderDeviceForm(device) {
  const radio = device.radio;
  let html = '<h3>Placement & antenna</h3>' +
    field('Name', 'name', device.name, { type: 'text', wide: true }) + field('X (m)', 'x', device.x, { step: '.1' }) + field('Y (m)', 'y', device.y, { step: '.1' }) +
    field('Antenna height (m)', 'antennaHeightM', device.antennaHeightM, { step: '.1' }) + field('Antenna gain (dBi)', 'antennaGainDbi', device.antennaGainDbi, { step: '.1' }) + field('Cable loss (dB)', 'cableLossDb', device.cableLossDb, { step: '.1' });
  html += '<h3>LoRa radio · Germany 868 MHz</h3>' + field('Frequency (Hz)', 'radio.frequencyHz', radio.frequencyHz) + field('Bandwidth (Hz)', 'radio.bandwidthHz', radio.bandwidthHz) +
    field('Conducted power (dBm)', 'radio.txPowerDbm', radio.txPowerDbm) + field('Spreading factor', 'radio.spreadingFactor', radio.spreadingFactor) +
    field('Coding denominator', 'radio.codingRateDenominator', radio.codingRateDenominator) + field('Preamble symbols', 'radio.preambleSymbols', radio.preambleSymbols) +
    field('Sync word', 'radio.syncWord', radio.syncWord) + field('Relay hop limit', 'radio.relayHopLimit', radio.relayHopLimit);
  if (device.role === 'tracker') {
    const c = device.config;
    html += '<h3>Tracker behavior</h3>' + field('Speed (km/h)', 'speedKmh', device.speedKmh, { step: '.1' }) + field('Path mode', 'pathMode', device.pathMode, { type: 'select', values: ['loop', 'reverse', 'stop'] }) +
      field('Moving sleep (s)', 'config.movingSleepS', c.movingSleepS) + field('Stationary sleep (s)', 'config.stationarySleepS', c.stationarySleepS) +
      field('Long stationary sleep (s)', 'config.longStationarySleepS', c.longStationarySleepS) + field('No-fix sleeps (s)', 'config.noFixSleepS', c.noFixSleepS, { type: 'text' }) +
      field('Stationary threshold', 'config.stationaryFixesForLongSleep', c.stationaryFixesForLongSleep) + field('Max-sleep threshold', 'config.stationaryFixesForMaxSleep', c.stationaryFixesForMaxSleep) +
      field('TX interval (s)', 'config.txIntervalS', c.txIntervalS) + field('Minimum batch points', 'config.txMinPoints', c.txMinPoints) +
      field('ACK timeout (ms)', 'config.ackTimeoutMs', c.ackTimeoutMs) + field('Retry backoffs (s)', 'config.retryBackoffS', c.retryBackoffS, { type: 'text' }) +
      field('Point spacing (m)', 'config.historyPointSpacingM', c.historyPointSpacingM, { step: '.1' }) + field('Minimum satellites', 'config.minSatellites', c.minSatellites) +
      field('Maximum HDOP', 'config.maxHdop', c.maxHdop, { step: '.1' }) + '<h3>Battery model</h3>' +
      field('Capacity (mAh)', 'config.batteryCapacityMah', c.batteryCapacityMah) + field('Sleep current (mA)', 'config.sleepCurrentMa', c.sleepCurrentMa, { step: '.1' }) +
      field('GNSS current (mA)', 'config.gnssCurrentMa', c.gnssCurrentMa, { step: '.1' }) + field('TX current (mA)', 'config.txCurrentMa', c.txCurrentMa, { step: '.1' });
    const routeDistance = device.waypoints.reduce((total, waypoint, index) => index ? total + exactDistanceM(snapshot.scenario, device.waypoints[index - 1], waypoint) : total, 0) + (device.pathMode === 'loop' && device.waypoints.length > 1 ? exactDistanceM(snapshot.scenario, device.waypoints.at(-1), device.waypoints[0]) : 0);
    html += `<h3>Waypoints · ${routeDistance.toFixed(1)} m route</h3><div class="waypoint-list wide">${device.waypoints.map((point, index) => `<span>${index + 1}. ${point.x.toFixed(1)}, ${point.y.toFixed(1)} <button type="button" data-remove-waypoint="${index}" ${device.waypoints.length <= 1 ? 'disabled' : ''}>Remove</button></span>`).join('')}</div>`;
    $('#selection-status').textContent = `${device.batteryPct.toFixed(1)}% · queue ${device.runtime.queue.length}`;
  } else if (device.role === 'repeater') {
    const c = device.config;
    html += '<h3>Relay policy</h3>' + field('Base delay (ms)', 'config.forwardingBaseDelayMs', c.forwardingBaseDelayMs) + field('Slot width (ms)', 'config.forwardingSlotWidthMs', c.forwardingSlotWidthMs) +
      field('Slot count', 'config.forwardingSlotCount', c.forwardingSlotCount) + field('Duplicate cache (s)', 'config.duplicateCacheTtlS', c.duplicateCacheTtlS) +
      field('Airtime budget (ms/h)', 'config.airtimeBudgetMsPerHour', c.airtimeBudgetMsPerHour);
  } else {
    html += '<h3>Gateway & archive</h3>' + field('MQTT connected', 'config.mqttConnected', device.config.mqttConnected, { type: 'checkbox' }) +
      field('Registered tracker IDs', 'config.registeredTrackerIds', device.config.registeredTrackerIds, { type: 'text', wide: true });
  }
  form.innerHTML = html;
  bindForm(device);
}

function renderObstacleForm(obstacle) {
  let html = field('Label', 'label', obstacle.label, { type: 'text', wide: true });
  if (obstacle.type === 'tree') html += field('Canopy radius (m)', 'radius', obstacle.radius, { step: '.1' });
  else {
    const center = polygonCenter(obstacle.points);
    html += `<div class="wide hint">${obstacle.points.length} corners · centre ${center.x.toFixed(1)}, ${center.y.toFixed(1)} m. Drag corners on the map; click an edge to insert a corner.</div>`;
  }
  if (obstacle.type === 'forest') html += field('Density 0–1', 'density', obstacle.density, { step: '.05' });
  form.innerHTML = html;
  bindForm(obstacle);
}

function setPath(object, path, value) {
  const parts = path.split('.'); let target = object;
  for (const part of parts.slice(0, -1)) target = target[part];
  target[parts.at(-1)] = value;
}

function bindForm(entity) {
  form.querySelectorAll('[data-path]').forEach((input) => input.addEventListener('change', () => {
    const path = input.dataset.path;
    let value = input.type === 'checkbox' ? input.checked : input.type === 'number' ? Number(input.value) : input.value;
    if (['config.noFixSleepS', 'config.retryBackoffS'].includes(path)) value = input.value.split(',').map((item) => Number(item.trim())).filter(Number.isFinite).slice(0, 4);
    if (path === 'config.registeredTrackerIds') value = input.value.split(',').map((item) => item.trim()).filter(Boolean);
    const changes = copy(entity);
    delete changes.runtime; delete changes.batteryPct;
    setPath(changes, path, value);
    post('update-entity', { id: entity.id, changes });
  }));
}

function resizeCanvas() {
  const rect = mapWrap.getBoundingClientRect();
  const ratio = Math.min(window.devicePixelRatio || 1, 2);
  canvas.width = Math.round(rect.width * ratio); canvas.height = Math.round(rect.height * ratio);
  context.setTransform(ratio, 0, 0, ratio, 0, 0);
}
new ResizeObserver(resizeCanvas).observe(mapWrap);

function mercatorPixel(latitude, longitude, zoom) {
  const scale = 256 * 2 ** zoom;
  const sin = Math.sin(clamp(latitude, -85.05112878, 85.05112878) * Math.PI / 180);
  return { x: (longitude + 180) / 360 * scale, y: (0.5 - Math.log((1 + sin) / (1 - sin)) / (4 * Math.PI)) * scale };
}

function satelliteTile(xTile, yTile, zoom) {
  const max = 2 ** zoom;
  if (yTile < 0 || yTile >= max) return null;
  const x = ((xTile % max) + max) % max;
  const key = `${zoom}/${x}/${yTile}`;
  if (!satelliteTiles.has(key)) {
    const image = new Image();
    image.crossOrigin = 'anonymous';
    image.src = `https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/${zoom}/${yTile}/${x}`;
    satelliteTiles.set(key, image);
  }
  return satelliteTiles.get(key);
}

function drawSatelliteBackground(width, height, sx) {
  const map = snapshot.scenario.map;
  if (map.mode !== 'satellite') return false;
  const centre = mercatorPixel(map.centerLat, map.centerLng, map.zoom);
  const metersPerPixel = 156543.03392 * Math.cos(map.centerLat * Math.PI / 180) / 2 ** map.zoom;
  const scale = sx * metersPerPixel;
  const tileX = Math.floor(centre.x / 256); const tileY = Math.floor(centre.y / 256);
  const radiusX = Math.ceil(width / (256 * scale) / 2) + 1;
  const radiusY = Math.ceil(height / (256 * scale) / 2) + 1;
  context.fillStyle = '#12251d'; context.fillRect(0, 0, width, height);
  for (let ty = tileY - radiusY; ty <= tileY + radiusY; ty += 1) for (let tx = tileX - radiusX; tx <= tileX + radiusX; tx += 1) {
    const image = satelliteTile(tx, ty, map.zoom);
    if (image?.complete && image.naturalWidth) context.drawImage(image, width / 2 + view.panX + (tx * 256 - centre.x) * scale, height / 2 + view.panY + (ty * 256 - centre.y) * scale, 256 * scale, 256 * scale);
  }
  return true;
}

function draw() {
  requestAnimationFrame(draw);
  if (!snapshot) return;
  const width = canvas.clientWidth; const height = canvas.clientHeight;
  const world = snapshot.scenario.world;
  const sx = width / world.widthM * view.zoom; const sy = height / world.heightM * view.zoom;
  const x = (value) => (value - world.widthM / 2) * sx + width / 2 + view.panX;
  const y = (value) => (value - world.heightM / 2) * sy + height / 2 + view.panY;
  context.clearRect(0, 0, width, height);
  const satellite = drawSatelliteBackground(width, height, sx);
  if (!satellite) { context.fillStyle = '#0c1b16'; context.fillRect(0, 0, width, height); }
  if (backgroundImage) { context.globalAlpha = .28; context.drawImage(backgroundImage, 0, 0, width, height); context.globalAlpha = 1; }
  context.strokeStyle = satellite ? 'rgba(237,246,242,.16)' : 'rgba(116,153,139,.12)'; context.lineWidth = 1;
  for (let gx = 0; gx <= world.widthM; gx += world.gridM) { context.beginPath(); context.moveTo(x(gx), 0); context.lineTo(x(gx), height); context.stroke(); }
  for (let gy = 0; gy <= world.heightM; gy += world.gridM) { context.beginPath(); context.moveTo(0, y(gy)); context.lineTo(width, y(gy)); context.stroke(); }

  for (const obstacle of snapshot.scenario.obstacles) drawObstacle(obstacle, x, y, sx, sy);
  drawHistoryOverlays(x, y);
  drawOutOfRangeLinks(x, y);
  for (const device of snapshot.devices.filter((item) => item.role === 'tracker')) {
    context.strokeStyle = 'rgba(92,224,160,.35)'; context.lineWidth = 1.5; context.setLineDash([5, 5]); context.beginPath();
    device.waypoints.forEach((point, index) => index ? context.lineTo(x(point.x), y(point.y)) : context.moveTo(x(point.x), y(point.y)));
    if (device.pathMode === 'loop' && device.waypoints.length > 1) context.closePath(); context.stroke(); context.setLineDash([]);
    for (const point of device.waypoints) { context.fillStyle = '#5ce0a0'; context.beginPath(); context.arc(x(point.x), y(point.y), 2.5, 0, Math.PI * 2); context.fill(); }
  }
  drawAnimations(x, y, sx, sy);
  for (const device of snapshot.devices) drawDevice(device, x, y);
  const selected = snapshot.devices.find((item) => item.id === selectedId);
  if (selected) { context.strokeStyle = '#edf6f2'; context.lineWidth = 1.5; context.beginPath(); context.arc(x(selected.x), y(selected.y), 17, 0, Math.PI * 2); context.stroke(); }
}

function drawHistoryOverlays(x, y) {
  const local = (snapshot.trackHistory[trackDeviceId] ?? []).filter((point) => point.timeS <= trackTimeS);
  const archived = snapshot.archive.filter((point) => point.gatewayId === archiveGatewayId && point.deviceId === archiveTrackerId);
  const drawPath = (points, stroke, marker, label) => {
    if (!points.length) return;
    context.save(); context.strokeStyle = stroke; context.lineWidth = 2; context.setLineDash([4, 3]); context.beginPath();
    points.forEach((point, index) => index ? context.lineTo(x(point.x), y(point.y)) : context.moveTo(x(point.x), y(point.y))); context.stroke(); context.setLineDash([]);
    for (const point of points) { context.fillStyle = marker; context.beginPath(); context.arc(x(point.x), y(point.y), 3, 0, Math.PI * 2); context.fill(); }
    const last = points.at(-1); context.fillStyle = marker; context.font = '10px system-ui'; context.fillText(label, x(last.x) + 7, y(last.y) + 12); context.restore();
  };
  drawPath(local, 'rgba(92,224,160,.92)', '#5ce0a0', 'local track');
  drawPath(archived, 'rgba(104,184,255,.92)', '#68b8ff', 'archive');
}

function drawOutOfRangeLinks(x, y) {
  if (!selectedId) return;
  const from = snapshot.devices.find((device) => device.id === selectedId);
  if (!from) return;
  for (const link of snapshot.links.filter((item) => item.fromId === from.id && item.marginDb < 0)) {
    const to = snapshot.devices.find((device) => device.id === link.toId);
    if (!to) continue;
    context.save(); context.strokeStyle = 'rgba(255,117,109,.9)'; context.lineWidth = 2; context.setLineDash([7, 5]); context.beginPath(); context.moveTo(x(from.x), y(from.y)); context.lineTo(x(to.x), y(to.y)); context.stroke(); context.setLineDash([]);
    context.beginPath(); context.moveTo(x(to.x) - 7, y(to.y) - 7); context.lineTo(x(to.x) + 7, y(to.y) + 7); context.moveTo(x(to.x) + 7, y(to.y) - 7); context.lineTo(x(to.x) - 7, y(to.y) + 7); context.stroke();
    context.fillStyle = '#ff756d'; context.font = '10px system-ui'; context.fillText(`out of range ${link.marginDb.toFixed(1)} dB`, x((from.x + to.x) / 2) + 4, y((from.y + to.y) / 2) - 4); context.restore();
  }
}

function drawObstacle(obstacle, x, y, sx, sy) {
  const selected = obstacle.id === selectedId;
  if (obstacle.type === 'tree') {
    context.fillStyle = selected ? '#73c98e' : '#2f744b'; context.beginPath(); context.arc(x(obstacle.x), y(obstacle.y), Math.max(4, obstacle.radius * sx), 0, Math.PI * 2); context.fill();
    context.fillStyle = '#684b32'; context.fillRect(x(obstacle.x) - 1.5, y(obstacle.y), 3, 7);
    return;
  }
  const colors = { forest: 'rgba(38,103,67,.58)', 'building-small': '#735f4b', 'building-large': '#80654d' };
  const points = obstacle.points;
  context.fillStyle = colors[obstacle.type]; context.beginPath(); points.forEach((point, index) => index ? context.lineTo(x(point.x), y(point.y)) : context.moveTo(x(point.x), y(point.y))); context.closePath(); context.fill();
  context.strokeStyle = selected ? '#edf6f2' : obstacle.type === 'forest' ? 'rgba(92,224,160,.45)' : '#b59778'; context.lineWidth = selected ? 2 : 1;
  context.stroke();
  if (obstacle.type === 'forest') {
    context.fillStyle = 'rgba(92,224,160,.22)';
    const count = Math.round(12 + obstacle.density * 20);
    const minX = Math.min(...points.map((point) => point.x)); const maxX = Math.max(...points.map((point) => point.x)); const minY = Math.min(...points.map((point) => point.y)); const maxY = Math.max(...points.map((point) => point.y));
    for (let i = 0; i < count; i += 1) { const candidate = { x: minX + ((i * 47) % 97) / 97 * (maxX - minX), y: minY + ((i * 31) % 89) / 89 * (maxY - minY) }; if (pointInPolygon(candidate, points)) { context.beginPath(); context.arc(x(candidate.x), y(candidate.y), 2, 0, Math.PI * 2); context.fill(); } }
  }
  if (selected) for (let index = 0; index < points.length; index += 1) { const point = points[index]; const next = points[(index + 1) % points.length]; context.fillStyle = '#edf6f2'; context.fillRect(x(point.x) - 4, y(point.y) - 4, 8, 8); context.fillStyle = '#5ce0a0'; context.beginPath(); context.arc(x((point.x + next.x) / 2), y((point.y + next.y) / 2), 3.5, 0, Math.PI * 2); context.fill(); }
  const center = polygonCenter(points); context.fillStyle = 'rgba(237,246,242,.82)'; context.font = '11px system-ui'; context.fillText(obstacle.label, x(center.x) + 6, y(center.y) + 4);
}

function drawDevice(device, x, y) {
  const colors = { tracker: '#5ce0a0', repeater: '#ffbd59', receiver: '#68b8ff' };
  const px = x(device.x), py = y(device.y); context.fillStyle = colors[device.role]; context.strokeStyle = '#07120e'; context.lineWidth = 2;
  context.beginPath();
  if (device.role === 'tracker') context.arc(px, py, 9, 0, Math.PI * 2);
  else if (device.role === 'repeater') { context.moveTo(px, py - 11); context.lineTo(px + 10, py + 8); context.lineTo(px - 10, py + 8); context.closePath(); }
  else { context.rect(px - 9, py - 9, 18, 18); }
  context.fill(); context.stroke();
  if (device.role === 'tracker') { context.strokeStyle = device.batteryPct < 20 ? '#ff756d' : '#edf6f2'; context.lineWidth = 2; context.beginPath(); context.arc(px, py, 13, -Math.PI / 2, -Math.PI / 2 + Math.PI * 2 * device.batteryPct / 100); context.stroke(); }
  context.font = '600 11px system-ui'; context.fillStyle = '#edf6f2'; context.fillText(device.name, px + 14, py - 6);
  context.font = '10px ui-monospace'; context.fillStyle = '#9db3aa'; context.fillText(device.role === 'tracker' ? `${device.batteryPct.toFixed(0)}% · q${device.runtime.queue.length}` : `${device.runtime.stats.rx} rx · ${device.runtime.stats.tx} tx`, px + 14, py + 8);
}

function drawAnimations(x, y, sx, sy) {
  const now = performance.now();
  for (let index = animations.length - 1; index >= 0; index -= 1) if (now - animations[index].started > animations[index].duration) animations.splice(index, 1);
  for (const animation of animations) {
    const progress = clamp((now - animation.started) / animation.duration, 0, 1);
    context.globalAlpha = 1 - progress;
    context.strokeStyle = animation.kind === 'ack' ? '#bd91ff' : '#68b8ff'; context.lineWidth = 2;
    if (animation.kind === 'wave') { context.beginPath(); context.arc(x(animation.from.x), y(animation.from.y), progress * Math.max(canvas.clientWidth, canvas.clientHeight) * .65, 0, Math.PI * 2); context.stroke(); }
    else if (animation.to) { const tx = x(animation.from.x), ty = y(animation.from.y), rx = x(animation.to.x), ry = y(animation.to.y); context.beginPath(); context.moveTo(tx, ty); context.lineTo(tx + (rx - tx) * progress, ty + (ry - ty) * progress); context.stroke(); }
    context.globalAlpha = 1;
  }
}

requestAnimationFrame(draw);
post('init', { scenario: initialScenario });
