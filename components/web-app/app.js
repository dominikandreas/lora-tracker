import "./styles.css";
import "leaflet/dist/leaflet.css";
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
import {
  evaluateBrokerTransport,
  normalizeBrokerWebSocketUrl,
} from "./broker-policy.js";
import { generateOwnerKey, ownerKeyAction } from "./credential-policy.js";
import {
  OnboardingManager,
  createBleDeviceScanner,
  createBleTransport,
} from "./onboarding.js";
import { WifiDeviceTransport } from "./device-api.js";
import {
  createDeviceTransfer,
  renderDeviceTransferQr,
  scanDeviceTransferImage,
} from "./device-sharing.js";
import {
  collectDeviceConfig,
  renderDeviceConfig,
} from "./device-config.js";
import { pairTrackerTransaction } from "./pairing-transaction.js";
import {
  PlatformNotificationService,
  addAppStateListener,
  isNativeApp,
  loadDeviceOwnerKey,
  loadSettings,
  loadDevices,
  loadStoredMqttPassword,
  removeDeviceOwnerKey,
  saveDevices,
  savePlatformSettings,
  storeDeviceOwnerKey,
  storeDeviceSecret,
  storeMqttPassword,
} from "./platform.js";

const els = Object.fromEntries(
  [...document.querySelectorAll("[id]")].map((el) => [el.id, el]),
);
const mapManager = new MapManager("map");
const notificationService = new PlatformNotificationService();
try {
  await notificationService.initialize();
} catch (error) {
  console.warn("Notifications are unavailable", error);
}
const alertsManager = new AlertsManager(notificationService);
const mqtt = new MqttWebSocketClient();
const trackers = new Map();
let selectedHash = null;
let connected = false;
let pendingHistoryRequest = null;

let devices = await loadDevices();
let deviceSession = null;
const unpairedBleDevices = new Map();
const bleScanner = createBleDeviceScanner();

const saved = await loadSettings();
let startupWarning = "";
els.brokerUrl.value = normalizeBrokerWebSocketUrl(saved.brokerUrl || "");
els.baseTopic.value = saved.baseTopic || "lora-tracker";
els.username.value = saved.username || "";
if (isNativeApp) {
  document.documentElement.classList.add("native-app");
  els.credentialStorageHint.textContent =
    "The password is saved automatically in Android Keystore-backed encrypted storage.";
} else {
  els.credentialStorageHint.textContent =
    "The password is saved in this site's local browser storage. Use only a trusted browser profile.";
}
try {
  const storedPassword = await loadStoredMqttPassword();
  if (storedPassword) {
    els.password.value = storedPassword;
  }
} catch (error) {
  console.warn("Credential storage is unavailable", error);
  startupWarning =
    "Credential storage is unavailable; the password will remain in memory only.";
}

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
    await mapManager.setLayer(mapLayerType);
    await saveSettings();
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
    await saveSettings();
  } catch (error) {
    mapLayerType = previous;
    els.mapLayer.value = previous;
    els.connectionMessage.textContent = `Map layer failed: ${error.message}`;
  }
});

els.brokerUrl.addEventListener("change", () => {
  els.brokerUrl.value = normalizeBrokerWebSocketUrl(els.brokerUrl.value);
});

// --- Onboarding Logic ---
function setBleStatus(msg) {
  els.bleStatus.textContent = msg;
  const connected = msg.startsWith("Connected");
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

function deviceIdFromConfig(config) {
  return config?.device_id || config?.gateway_id || config?.repeater_id;
}

function deviceNameFromConfig(config) {
  return config?.device_name || config?.gateway_name || config?.repeater_name || deviceIdFromConfig(config);
}

function currentBleId(manager) {
  return manager?.transport?.deviceId || manager?.transport?.device?.id || null;
}

function uniqueValues(...values) {
  return [...new Set(values.flat().filter(Boolean).map(String))];
}

async function persistCurrentDevice() {
  const config = deviceSession?.config;
  if (!config) return;
  const id = deviceIdFromConfig(config);
  const previousId = deviceSession.record?.id;
  const previous = devices.find((device) => device.id === id) ||
    devices.find((device) => device.id === previousId) ||
    deviceSession.record || {};
  const record = {
    ...previous,
    id,
    name: deviceNameFromConfig(config),
    role: config.role,
    bleId: currentBleId(deviceSession.manager) || previous.bleId || null,
    address: config.network_ip && config.network_ip !== "off"
      ? config.network_ip
      : deviceSession.address || previous.address || null,
    credentialAliases: uniqueValues(
      previous.credentialAliases || [],
      previousId,
      previous.bleId,
      currentBleId(deviceSession.manager),
    ),
    lastSeen: new Date().toISOString(),
  };
  devices = [
    ...devices.filter((device) => device.id !== id && device.id !== previousId),
    record,
  ];
  await saveDevices(devices);
  if (deviceSession.ownerKey) {
    await storeDeviceOwnerKey(
      id,
      deviceSession.ownerKey,
      record.bleId,
      previousId,
      record.credentialAliases,
    );
  }
  if (config.role === "tracker" && config.lora_aead_key) {
    await storeDeviceSecret(id, config.lora_aead_key, "lora-key");
  }
  deviceSession.record = record;
  unpairedBleDevices.delete(record.bleId);
  renderDeviceInventory();
  renderUnpairedDevices();
}

function renderDeviceInventory() {
  els.deviceInventory.replaceChildren();
  if (!devices.length) {
    els.deviceInventory.textContent = "No saved devices yet.";
    return;
  }
  for (const device of devices) {
    const button = document.createElement("button");
    button.className = "device-card";
    const title = document.createElement("strong");
    title.textContent = device.name || device.id;
    const detail = document.createElement("span");
    detail.textContent = `${device.role} · ${device.address || "Bluetooth"}`;
    button.append(title, detail);
    button.addEventListener("click", () => openSavedDevice(device));
    els.deviceInventory.append(button);
  }
}

function renderUnpairedDevices() {
  els.unpairedDeviceInventory.replaceChildren();
  const available = [...unpairedBleDevices.values()].filter(
    (candidate) => !devices.some((device) => device.bleId === candidate.deviceId),
  );
  if (!available.length) {
    els.unpairedDeviceInventory.textContent = "Scanning for unclaimed devices…";
    return;
  }
  for (const candidate of available) {
    const button = document.createElement("button");
    button.className = "device-card";
    const title = document.createElement("strong");
    title.textContent = candidate.name || "LoRa tracker";
    const detail = document.createElement("span");
    detail.textContent = "Nearby device · Bluetooth";
    button.append(title, detail);
    button.addEventListener("click", async () => {
      try {
        await connectBle(candidate.deviceId);
      } catch (error) {
        setBleStatus(`Error: ${error.message}`);
      }
    });
    els.unpairedDeviceInventory.append(button);
  }
}

async function startUnpairedDeviceScan() {
  try {
    await bleScanner.start((candidate) => {
      if (!candidate?.deviceId) return;
      unpairedBleDevices.set(candidate.deviceId, candidate);
      renderUnpairedDevices();
    });
    renderUnpairedDevices();
  } catch (error) {
    console.warn("Bluetooth discovery is unavailable", error);
    if (!unpairedBleDevices.size) {
      els.unpairedDeviceInventory.textContent = "Bluetooth discovery is unavailable.";
    }
  }
}

async function resetDeviceSession() {
  const previousSession = deviceSession;
  deviceSession = null;
  try {
    await previousSession?.manager?.disconnect();
  } catch (error) {
    console.warn("Could not close the previous Bluetooth session", error);
  }
  els.deviceSession.hidden = false;
  els.deviceAddressRow.hidden = false;
  els.deviceConfigPanel.hidden = true;
  els.bleOutput.textContent = "";
  setBleStatus("Disconnected");
}

function summarizeDevice(config, transport) {
  const state = config.role === "tracker"
    ? `${config.config_complete ? "Configured" : "Needs configuration"} · ${config.gateway_paired ? `Added to ${(config.paired_gateway_ids || [config.paired_gateway_id]).filter(Boolean).join(", ")}` : "Not added to a gateway"}`
    : config.role === "gateway"
      ? `${config.onboarding_required ? "Onboarding incomplete" : "Operational"} · ${config.trackers?.length || 0} tracker(s)`
      : `${config.onboarding_required ? "Onboarding incomplete" : "Operational"} · Wi-Fi configuration portal`;
  els.deviceStateSummary.textContent = `${deviceNameFromConfig(config)} · ${config.role} · ${transport} · revision ${config.revision} · ${state}`;
}

async function showDeviceConfig(config) {
  deviceSession.config = config;
  renderDeviceConfig(els.deviceConfigFields, config);
  summarizeDevice(config, deviceSession.mode === "wifi" ? "Wi-Fi" : "Bluetooth");
  els.deviceConfigPanel.hidden = false;
  els.trackerPairingPanel.hidden = config.role !== "tracker";
  els.rollbackDevice.hidden = config.role === "repeater";
  if (config.role === "tracker") {
    els.pairingGateway.replaceChildren();
    for (const gateway of devices.filter((device) => device.role === "gateway")) {
      const option = document.createElement("option");
      option.value = gateway.id;
      option.textContent = gateway.name || gateway.id;
      els.pairingGateway.append(option);
    }
    const selectedGateway = devices.find(
      (device) => device.id === els.pairingGateway.value,
    );
    els.pairingGatewayAddress.value = selectedGateway?.address || "";
    els.pairTrackerGateway.disabled =
      !els.pairingGateway.options.length || !config.config_complete;
    els.pairTrackerGateway.title = config.config_complete
      ? ""
      : "Save a complete tracker configuration before gateway pairing";
  }
  await persistCurrentDevice();
}

async function fetchBleConfig() {
  const config = await deviceSession.manager.getConfig();
  appendBleOutput(`Loaded ${config.role} configuration revision ${config.revision}.`);
  await showDeviceConfig(config);
}

async function connectBle(bleId = null, knownRecord = null) {
  await bleScanner.stop();
  await resetDeviceSession();
  const manager = new OnboardingManager(createBleTransport());
  deviceSession = { mode: "ble", manager, ownerKey: null, config: null };
  manager.transport.onDisconnect = () => {
    if (deviceSession?.manager === manager) setBleStatus("Disconnected");
  };
  try {
    setBleStatus("Connecting…");
    const info = await manager.connect(bleId);
    deviceSession.info = info;
    deviceSession.record = knownRecord;
    setBleStatus(`Connected · ${info.role}`);
    const identity = info.device_id || knownRecord?.id;
    if (!identity) throw new Error("Device did not provide its identity");
    const bleIdentity = currentBleId(manager);
    let ownerKey = await loadDeviceOwnerKey(
      identity,
      knownRecord?.id,
      knownRecord?.bleId,
      knownRecord?.credentialAliases,
      bleIdentity,
    );
    const keyAction = ownerKeyAction(info, ownerKey);
    if (keyAction === "claim") {
      ownerKey ||= generateOwnerKey();
      els.deviceStep.textContent = "Claiming this factory-reset device…";
      // Persist before the one-way claim. If the app is killed or the response
      // is lost, reconnecting reuses this key and retries CLAIM.
      await storeDeviceOwnerKey(
        identity,
        ownerKey,
        knownRecord?.id,
        knownRecord?.bleId,
        knownRecord?.credentialAliases,
        bleIdentity,
      );
      await manager.claim(ownerKey);
    } else if (keyAction === "auth") {
      els.deviceStep.textContent = "Authenticating with the saved owner key…";
      await manager.auth(ownerKey);
    } else {
      throw new Error("This device is already claimed by another app. Connect using the app that set it up, or factory reset the device.");
    }
    deviceSession.ownerKey = ownerKey;
    await fetchBleConfig();
    return info;
  } catch (error) {
    if (deviceSession?.manager === manager) deviceSession = null;
    await manager.disconnect();
    if (!els.onboardingPanel.hidden) await startUnpairedDeviceScan();
    throw error;
  }
}

async function connectWifi(address, ownerKey, record = null) {
  const transport = new WifiDeviceTransport(address, ownerKey);
  const info = await transport.getInfo();
  const config = await transport.getConfig();
  deviceSession = {
    mode: "wifi",
    wifi: transport,
    address,
    ownerKey,
    record,
    info,
    config,
  };
  setBleStatus(`Connected · ${info.role} · Wi-Fi`);
  await showDeviceConfig(config);
}

async function openSavedDevice(record) {
  await resetDeviceSession();
  els.deviceStep.textContent = `Connecting to ${record.name || record.id}…`;
  const ownerKey = await loadDeviceOwnerKey(
    record.id,
    record.bleId,
    record.credentialAliases,
  );
  try {
    if (record.address && ownerKey) {
      await connectWifi(record.address, ownerKey, record);
      return;
    }
  } catch (error) {
    appendBleOutput(`Wi-Fi unavailable: ${error.message}. Trying Bluetooth.`);
  }
  try {
    await connectBle(record.bleId, record);
  } catch (error) {
    setBleStatus(`Error: ${error.message}`);
    els.deviceAddressRow.hidden = false;
    els.deviceAddress.value = record.address || "";
  }
}

els.onboardingButton.addEventListener("click", () => {
  els.onboardingPanel.hidden = !els.onboardingPanel.hidden;
  if (!els.onboardingPanel.hidden) {
    renderDeviceInventory();
    startUnpairedDeviceScan();
  }
});

els.addBleDevice.addEventListener("click", async () => {
  try {
    await connectBle();
  } catch (error) {
    setBleStatus(`Error: ${error.message}`);
  }
});

els.connectIpDevice.addEventListener("click", async () => {
  try {
    // Identify the endpoint before selecting a local key. Reusing the active
    // session's key for an arbitrary typed IP can suppress a legitimate first
    // claim or authenticate against the wrong device.
    const probe = new WifiDeviceTransport(els.deviceAddress.value);
    const info = await probe.getInfo();
    const matchingRecord = devices.find((device) => device.id === info.device_id);
    let ownerKey = await loadDeviceOwnerKey(
      info.device_id,
      matchingRecord?.bleId,
      matchingRecord?.credentialAliases,
    );
    const keyAction = ownerKeyAction(info, ownerKey);
    if (keyAction === "claim") {
      ownerKey ||= generateOwnerKey();
      await storeDeviceOwnerKey(info.device_id, ownerKey);
      await probe.claim(ownerKey);
    }
    if (keyAction === "unavailable") throw new Error("This device is already claimed and its owner key is not in this app");
    await connectWifi(els.deviceAddress.value, ownerKey, matchingRecord);
  } catch (error) {
    appendBleOutput(`Connection failed: ${error.message}`);
  }
});

async function patchCurrentDevice(fields) {
  const revision = deviceSession.config.revision;
  if (deviceSession.mode === "wifi") {
    return deviceSession.wifi.patchConfig(revision, fields);
  }
  return deviceSession.manager.patchConfig(revision, fields);
}

els.saveDeviceConfig.addEventListener("click", async () => {
  try {
    const fields = collectDeviceConfig(els.deviceConfigFields);
    appendBleOutput("Validating and saving the complete configuration…");
    const result = await patchCurrentDevice(fields);
    appendBleOutput(result);
    if (result.reboot_required) {
      if (deviceSession.config.role === "tracker") {
        if (fields.device_id) deviceSession.config.device_id = fields.device_id;
        if (fields.device_name) deviceSession.config.device_name = fields.device_name;
      } else {
        if (fields.gateway_id) deviceSession.config.gateway_id = fields.gateway_id;
        if (fields.gateway_name) deviceSession.config.gateway_name = fields.gateway_name;
        if (fields.wifi_ssid) {
          deviceSession.address = `http://lora-gateway-${deviceSession.config.gateway_id}.local`;
          deviceSession.config.network_ip = "off";
        }
      }
      await persistCurrentDevice();
      appendBleOutput("Saved. The device is rebooting; reconnect from the saved device list when it is ready.");
      return;
    }
    const config = deviceSession.mode === "wifi"
      ? await deviceSession.wifi.getConfig()
      : await deviceSession.manager.getConfig();
    await showDeviceConfig(config);
  } catch (error) {
    appendBleOutput(`Save failed: ${error.message}`);
  }
});

els.refreshDeviceConfig.addEventListener("click", async () => {
  try {
    const config = deviceSession.mode === "wifi"
      ? await deviceSession.wifi.getConfig()
      : await deviceSession.manager.getConfig();
    await showDeviceConfig(config);
  } catch (error) {
    appendBleOutput(`Refresh failed: ${error.message}`);
  }
});

els.enableDeviceConfigMode.addEventListener("click", async () => {
  try {
    const result = deviceSession.mode === "wifi"
      ? await deviceSession.wifi.enterConfigMode()
      : await deviceSession.manager.enterConfigMode();
    appendBleOutput(result);
    appendBleOutput(result.reboot_required
      ? "The tracker is rebooting into configuration mode. Wait for Wi-Fi, then upload with PlatformIO within the setup window."
      : "Configuration and authenticated PlatformIO OTA are enabled for 10 minutes.");
  } catch (error) {
    appendBleOutput(`Could not enable configuration mode: ${error.message}`);
  }
});

els.exportDeviceQr.addEventListener("click", async () => {
  try {
    const record = deviceSession?.record;
    const ownerKey = deviceSession?.ownerKey || await loadDeviceOwnerKey(
      record?.id,
      record?.bleId,
      record?.credentialAliases,
    );
    const payload = createDeviceTransfer(record, ownerKey);
    els.deviceQrImage.src = await renderDeviceTransferQr(payload);
    els.deviceQrPanel.hidden = false;
  } catch (error) {
    appendBleOutput(`QR export failed: ${error.message}`);
  }
});

els.closeDeviceQr.addEventListener("click", () => {
  els.deviceQrPanel.hidden = true;
  els.deviceQrImage.removeAttribute("src");
});

els.copyDeviceOwnerKey.addEventListener("click", async () => {
  try {
    const record = deviceSession?.record;
    const ownerKey = deviceSession?.ownerKey || await loadDeviceOwnerKey(
      record?.id,
      record?.bleId,
      record?.credentialAliases,
    );
    if (!ownerKey) throw new Error("No owner key is stored for this device");
    await navigator.clipboard.writeText(ownerKey);
    appendBleOutput("Owner key copied. Treat the clipboard as full device authority and clear it after the PlatformIO upload.");
  } catch (error) {
    appendBleOutput(`Could not copy owner key: ${error.message}`);
  }
});

els.importDeviceQrButton.addEventListener("click", () => {
  els.importDeviceQrInput.click();
});

els.importDeviceQrInput.addEventListener("change", async () => {
  const file = els.importDeviceQrInput.files?.[0];
  els.importDeviceQrInput.value = "";
  if (!file) return;
  try {
    const transfer = await scanDeviceTransferImage(file);
    const previous = devices.find((device) => device.id === transfer.device.id) || {};
    const record = {
      ...previous,
      ...transfer.device,
      bleId: previous.bleId || null,
      credentialAliases: uniqueValues(previous.credentialAliases || []),
      lastSeen: new Date().toISOString(),
    };
    devices = [...devices.filter((device) => device.id !== record.id), record];
    await saveDevices(devices);
    await storeDeviceOwnerKey(record.id, transfer.owner_key);
    renderDeviceInventory();
    appendBleOutput(`Imported owner access for ${record.name || record.id}.`);
  } catch (error) {
    appendBleOutput(`QR import failed: ${error.message}`);
  }
});

async function connectGatewayForPairing(gateway) {
  const ownerKey = await loadDeviceOwnerKey(
    gateway.id,
    gateway.bleId,
    gateway.credentialAliases,
  );
  if (!ownerKey) {
    throw new Error(
      "The saved gateway authorization is missing. Open the gateway once over Bluetooth to recover it, or factory reset and claim it again",
    );
  }
  let wifiError;
  if (gateway.address) {
    try {
      const wifi = new WifiDeviceTransport(gateway.address, ownerKey);
      const config = await wifi.getConfig();
      if (config.role !== "gateway" || config.gateway_id !== gateway.id) {
        throw new Error("the address belongs to a different device");
      }
      return {
        registerTracker: (tracker) => wifi.registerTracker(tracker),
        close: async () => {},
      };
    } catch (error) {
      wifiError = error;
      appendBleOutput(`Gateway Wi-Fi unavailable: ${error.message}. Trying Bluetooth…`);
    }
  }
  if (!gateway.bleId) {
    throw new Error(
      wifiError
        ? `Could not reach the gateway at ${gateway.address}: ${wifiError.message}`
        : "The gateway has neither a saved network address nor Bluetooth identity",
    );
  }
  const manager = new OnboardingManager(createBleTransport());
  try {
    const info = await manager.connect(gateway.bleId);
    if (info.role !== "gateway" || info.device_id !== gateway.id) {
      throw new Error("Bluetooth identity does not match the selected gateway");
    }
    await manager.auth(ownerKey);
    return {
      registerTracker: (tracker) => manager.registerTracker(tracker),
      close: () => manager.disconnect(),
    };
  } catch (error) {
    await manager.disconnect();
    throw error;
  }
}

els.pairingGateway.addEventListener("change", () => {
  const gateway = devices.find((device) => device.id === els.pairingGateway.value);
  els.pairingGatewayAddress.value = gateway?.address || "";
});

els.pairTrackerGateway.addEventListener("click", async () => {
  const trackerConfig = deviceSession?.config;
  let gateway = devices.find((device) => device.id === els.pairingGateway.value);
  if (!trackerConfig || trackerConfig.role !== "tracker" || !gateway) return;
  if (!trackerConfig.config_complete) {
    appendBleOutput("Save and verify the tracker configuration before pairing.");
    return;
  }
  const trackerTransport = deviceSession.mode === "wifi"
    ? deviceSession.wifi
    : deviceSession.manager;
  try {
    els.pairTrackerGateway.disabled = true;
    const enteredAddress = els.pairingGatewayAddress.value.trim();
    if (enteredAddress && enteredAddress !== gateway.address) {
      gateway = { ...gateway, address: enteredAddress };
      devices = devices.map((device) =>
        device.id === gateway.id ? gateway : device,
      );
      await saveDevices(devices);
      renderDeviceInventory();
    }
    const result = await pairTrackerTransaction({
      trackerConfig,
      gatewayRecord: gateway,
      openGateway: connectGatewayForPairing,
      confirmTracker: (gatewayId) => trackerTransport.pairGateway(gatewayId),
      onStep: (_step, message) => appendBleOutput(`${message}…`),
    });
    deviceSession.config = {
      ...trackerConfig,
      gateway_paired: true,
      paired_gateway_id: result.confirmation.gateway_id,
      paired_gateway_ids: [
        ...new Set([
          ...(trackerConfig.paired_gateway_ids || []),
          result.confirmation.gateway_id,
        ]),
      ],
      onboarding_required: Boolean(result.confirmation.onboarding_required),
    };
    await persistCurrentDevice();
    summarizeDevice(
      deviceSession.config,
      deviceSession.mode === "wifi" ? "Wi-Fi" : "Bluetooth",
    );
    appendBleOutput("Pairing complete. The tracker may now leave setup mode and begin tracking.");
  } catch (error) {
    appendBleOutput(`Pairing incomplete: ${error.message}.`);
  } finally {
    els.pairTrackerGateway.disabled = false;
  }
});

els.rollbackDevice.addEventListener("click", async () => {
  if (!confirm("Restore the previous configuration and reboot?")) return;
  try {
    const result = deviceSession.mode === "wifi"
      ? await deviceSession.wifi.rollback(deviceSession.config.revision)
      : await deviceSession.manager.transport.sendCommand(`ROLLBACK ${deviceSession.config.revision}`);
    appendBleOutput(result);
  } catch (error) { appendBleOutput(`Rollback failed: ${error.message}`); }
});

els.rebootDevice.addEventListener("click", async () => {
  if (!confirm("Reboot this device?")) return;
  try {
    const result = deviceSession.mode === "wifi"
      ? await deviceSession.wifi.reboot()
      : await deviceSession.manager.transport.sendCommand("REBOOT");
    appendBleOutput(result);
  } catch (error) { appendBleOutput(`Reboot failed: ${error.message}`); }
});

els.factoryResetDevice.addEventListener("click", async () => {
  if (!confirm("Factory reset this device and erase its configuration?")) return;
  try {
    const result = deviceSession.mode === "wifi"
      ? await deviceSession.wifi.factoryReset()
      : await deviceSession.manager.transport.sendCommand("FACTORY_RESET FACTORY_RESET");
    appendBleOutput(result);
    const id = deviceSession?.record?.id || deviceIdFromConfig(deviceSession?.config);
    const aliases = [
      id,
      deviceSession?.record?.bleId,
      deviceSession?.record?.credentialAliases,
      currentBleId(deviceSession?.manager),
    ];
    devices = devices.filter((device) => device.id !== id);
    await saveDevices(devices);
    await removeDeviceOwnerKey(...aliases);
    if (id) await storeDeviceSecret(id, null, "lora-key");
    await resetDeviceSession();
    renderDeviceInventory();
    renderUnpairedDevices();
  } catch (error) { appendBleOutput(`Factory reset failed: ${error.message}`); }
});

els.forgetDevice.addEventListener("click", async () => {
  const id = deviceSession?.record?.id || deviceIdFromConfig(deviceSession?.config);
  if (!id || !confirm(`Forget ${id} from this app? This does not reset the hardware.`)) return;
  devices = devices.filter((device) => device.id !== id);
  await saveDevices(devices);
  await removeDeviceOwnerKey(
    id,
    deviceSession?.record?.bleId,
    deviceSession?.record?.credentialAliases,
  );
  await storeDeviceSecret(id, null, "lora-key");
  await resetDeviceSession();
  renderDeviceInventory();
});

renderDeviceInventory();
renderUnpairedDevices();
startUnpairedDeviceScan();

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
  await saveSettings();
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
  try {
    if (await alertsManager.requestPermission()) {
      updateAlertsButtonState();
    }
  } catch (error) {
    console.warn("Could not enable alerts", error);
    els.connectionMessage.textContent = `Could not enable alerts: ${error.message}`;
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

async function saveSettings() {
  await savePlatformSettings({
    brokerUrl: els.brokerUrl.value.trim(),
    baseTopic: els.baseTopic.value.trim() || "lora-tracker",
    username: els.username.value.trim(),
    mapLayerType,
  });
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
  const insecureTransport = els.brokerUrl.value
    .trim()
    .toLowerCase()
    .startsWith("ws://");
  connected = state === "online";
  setConnectionState(
    state,
    state === "reconnecting"
      ? `${event.detail.error || "Connection failed."} Retrying in ${Math.round(event.detail.delay / 1000)} seconds…`
      : state === "online"
        ? insecureTransport
          ? "Connected over unencrypted MQTT WebSocket. Traffic and credentials are visible on the network."
          : "Subscribed to tracker telemetry and history responses."
        : state === "connecting"
          ? event.detail.previousError
            ? `${event.detail.previousError} Retrying now…`
            : insecureTransport
              ? "Opening unencrypted MQTT WebSocket…"
              : "Opening secure MQTT WebSocket…"
          : event.detail.message || "Not connected.",
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

els.connectButton.addEventListener("click", async () => {
  if (connected || els.connectButton.textContent === "Disconnect") {
    mqtt.disconnect();
    connected = false;
    setConnectionState("offline", "Disconnected by user.");
    return;
  }
  const url = normalizeBrokerWebSocketUrl(els.brokerUrl.value);
  els.brokerUrl.value = url;
  const transportPolicy = evaluateBrokerTransport(url, {
    native: isNativeApp,
    pageProtocol: location.protocol,
  });
  if (!transportPolicy.allowed) {
    els.connectionMessage.textContent = transportPolicy.message;
    return;
  }
  if (
    isNativeApp && transportPolicy.insecure &&
    !confirm(
      "This MQTT connection is not encrypted. Your broker password, locations, history and commands may be read or changed by devices on the network. Continue?",
    )
  ) {
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
  try {
    await saveSettings();
    await storeMqttPassword(els.password.value);
  } catch (error) {
    console.warn("Could not persist application settings", error);
    els.connectionMessage.textContent =
      "Could not save settings securely. Connection was not started.";
    return;
  }
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
try {
  await addAppStateListener((isActive) => {
    if (isActive) {
      mapManager.invalidateSize();
      startUnpairedDeviceScan();
    } else {
      bleScanner.stop().catch((error) => console.warn("Could not pause Bluetooth discovery", error));
    }
  });
} catch (error) {
  console.warn("Application lifecycle integration is unavailable", error);
}
if (!isNativeApp && "serviceWorker" in navigator)
  navigator.serviceWorker.register("./sw.js").catch(console.warn);
setConnectionState("offline", startupWarning || "Not connected.");
