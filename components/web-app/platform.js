import { Capacitor } from "@capacitor/core";
import { App } from "@capacitor/app";
import { Preferences } from "@capacitor/preferences";
import { LocalNotifications } from "@capacitor/local-notifications";
import { SecureStorage } from "@aparajita/capacitor-secure-storage";

const SETTINGS_KEY = "lora-tracker.web.settings";
const PASSWORD_KEY = "mqtt-password";
const WEB_PASSWORD_KEY = "lora-tracker.mqtt-password";
const DEVICES_KEY = "lora-tracker.devices.v1";
export const isNativeApp = Capacitor.isNativePlatform();

export async function loadSettings() {
  let raw = null;
  if (isNativeApp) {
    raw = (await Preferences.get({ key: SETTINGS_KEY })).value;
  } else {
    raw = localStorage.getItem(SETTINGS_KEY);
  }
  if (!raw) return {};
  try {
    return JSON.parse(raw);
  } catch {
    await clearSettings();
    return {};
  }
}

export async function savePlatformSettings(settings) {
  const value = JSON.stringify(settings);
  if (isNativeApp) await Preferences.set({ key: SETTINGS_KEY, value });
  else localStorage.setItem(SETTINGS_KEY, value);
}

export async function clearSettings() {
  if (isNativeApp) await Preferences.remove({ key: SETTINGS_KEY });
  else localStorage.removeItem(SETTINGS_KEY);
}

let secureStorageReady;
async function prepareSecureStorage() {
  if (!isNativeApp) return;
  secureStorageReady ||= SecureStorage.setKeyPrefix("lora-tracker_");
  await secureStorageReady;
}

export async function loadStoredMqttPassword() {
  if (!isNativeApp) return localStorage.getItem(WEB_PASSWORD_KEY);
  await prepareSecureStorage();
  return SecureStorage.getItem(PASSWORD_KEY);
}

export async function storeMqttPassword(password) {
  if (!isNativeApp) {
    if (password) localStorage.setItem(WEB_PASSWORD_KEY, password);
    else localStorage.removeItem(WEB_PASSWORD_KEY);
    return;
  }
  await prepareSecureStorage();
  if (password) await SecureStorage.setItem(PASSWORD_KEY, password);
  else await SecureStorage.removeItem(PASSWORD_KEY);
}

export async function loadDevices() {
  let raw = null;
  if (isNativeApp) raw = (await Preferences.get({ key: DEVICES_KEY })).value;
  else raw = localStorage.getItem(DEVICES_KEY);
  if (!raw) return [];
  try {
    const devices = JSON.parse(raw);
    return Array.isArray(devices) ? devices : [];
  } catch {
    return [];
  }
}

export async function saveDevices(devices) {
  const value = JSON.stringify(devices);
  if (isNativeApp) await Preferences.set({ key: DEVICES_KEY, value });
  else localStorage.setItem(DEVICES_KEY, value);
}

function deviceSecretKey(deviceId, kind) {
  const safe = String(deviceId).replace(/[^a-zA-Z0-9_-]/g, "_");
  return `device-${safe}-${kind}`;
}

export async function loadDeviceSecret(deviceId, kind = "owner-key") {
  const key = deviceSecretKey(deviceId, kind);
  if (!isNativeApp) return localStorage.getItem(key);
  await prepareSecureStorage();
  return SecureStorage.getItem(key);
}

export async function storeDeviceSecret(deviceId, value, kind = "owner-key") {
  const key = deviceSecretKey(deviceId, kind);
  if (!isNativeApp) {
    if (value) localStorage.setItem(key, value);
    else localStorage.removeItem(key);
    return;
  }
  await prepareSecureStorage();
  if (value) await SecureStorage.setItem(key, value);
  else await SecureStorage.removeItem(key);
}

function uniqueDeviceIds(deviceIds) {
  return [...new Set(deviceIds.flat().filter(Boolean).map((id) => String(id)))];
}

// Device IDs are editable, so store the same owner key under every known local
// alias (canonical ID and platform BLE identifier).
export async function loadDeviceOwnerKey(...deviceIds) {
  const aliases = uniqueDeviceIds(deviceIds);
  for (const alias of aliases) {
    const ownerKey = await loadDeviceSecret(alias, "owner-key");
    if (!ownerKey) continue;
    await Promise.all(
      aliases.map((id) => storeDeviceSecret(id, ownerKey, "owner-key")),
    );
    return ownerKey;
  }
  return null;
}

export async function storeDeviceOwnerKey(deviceId, value, ...aliases) {
  await Promise.all(
    uniqueDeviceIds([deviceId, ...aliases]).map((id) =>
      storeDeviceSecret(id, value, "owner-key"),
    ),
  );
}

export async function removeDeviceOwnerKey(...deviceIds) {
  await Promise.all(
    uniqueDeviceIds(deviceIds).map((id) =>
      storeDeviceSecret(id, null, "owner-key"),
    ),
  );
}

export class PlatformNotificationService {
  constructor() {
    this.enabled = false;
    this.nextId = Math.floor(Date.now() % 2_000_000_000);
  }

  async initialize() {
    if (isNativeApp) {
      const status = await LocalNotifications.checkPermissions();
      this.enabled = status.display === "granted";
      if (Capacitor.getPlatform() === "android") {
        await LocalNotifications.createChannel({
          id: "tracker-alerts",
          name: "Tracker alerts",
          description: "Stale tracker, battery and unusual movement alerts",
          importance: 4,
          visibility: 1,
        });
      }
    } else {
      this.enabled =
        "Notification" in window && Notification.permission === "granted";
    }
    return this.enabled;
  }

  async requestPermission() {
    if (isNativeApp) {
      const status = await LocalNotifications.requestPermissions();
      this.enabled = status.display === "granted";
      return this.enabled;
    }
    if (!("Notification" in window)) return false;
    this.enabled = (await Notification.requestPermission()) === "granted";
    return this.enabled;
  }

  async notify(message) {
    if (!this.enabled) return;
    if (isNativeApp) {
      this.nextId = (this.nextId + 1) % 2_000_000_000;
      await LocalNotifications.schedule({
        notifications: [{
          id: this.nextId,
          title: "LoRa Tracker Alert",
          body: message,
          channelId: "tracker-alerts",
        }],
      });
    } else {
      new Notification("LoRa Tracker Alert", { body: message, icon: "icon.svg" });
    }
  }
}

export async function addAppStateListener(callback) {
  if (!isNativeApp) {
    const listener = () => callback(!document.hidden);
    document.addEventListener("visibilitychange", listener);
    return { remove: () => document.removeEventListener("visibilitychange", listener) };
  }
  return App.addListener("appStateChange", ({ isActive }) => callback(isActive));
}
