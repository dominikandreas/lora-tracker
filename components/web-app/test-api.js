// Narrow browser-test surface. Keeping test construction here lets the
// production bundle tree-shake and hash modules without tests importing source
// paths that do not exist in dist/.
import {
  BleDeviceScanner,
  BleTransport,
  NativeBleTransport,
  OnboardingManager,
} from "./onboarding.js";
import { MqttWebSocketClient } from "./mqtt.js";
import {
  loadDeviceOwnerKey,
  removeDeviceOwnerKey,
  storeDeviceOwnerKey,
} from "./platform.js";

window.__loraTrackerTest = Object.freeze({
  BleDeviceScanner,
  BleTransport,
  NativeBleTransport,
  OnboardingManager,
  MqttWebSocketClient,
  loadDeviceOwnerKey,
  removeDeviceOwnerKey,
  storeDeviceOwnerKey,
});
