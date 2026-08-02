import { normalizeDeviceAddress } from "./device-api.js";

const OWNER_KEY_PATTERN = /^[0-9a-f]{64}$/i;
const SETUP_AP_HOSTS = new Set(["192.168.4.1"]);
const NON_ADDRESS_VALUES = new Set(["off", "none", "unknown", "unavailable"]);
const COMMAND = "registry.upsert";
const API_VERSION = 1;
const SCHEMA_VERSION = 1;

function bytesToHex(bytes) {
  return [...bytes].map((value) => value.toString(16).padStart(2, "0")).join("");
}

function hexToBytes(value) {
  if (!OWNER_KEY_PATTERN.test(String(value || ""))) {
    throw new Error("The gateway owner key is missing or invalid");
  }
  return Uint8Array.from(
    String(value).match(/../g).map((pair) => Number.parseInt(pair, 16)),
  );
}

function bytesToBase64(bytes) {
  let binary = "";
  for (const value of bytes) binary += String.fromCharCode(value);
  return btoa(binary);
}

function commandAad(requestId) {
  return new TextEncoder().encode(
    `lora-tracker|${API_VERSION}|${SCHEMA_VERSION}|${requestId}|${COMMAND}`,
  );
}

export function gatewayHashFromId(deviceId) {
  let hash = 14695981039346656037n;
  const prime = 1099511628211n;
  for (const value of new TextEncoder().encode(String(deviceId || ""))) {
    hash ^= BigInt(value);
    hash = BigInt.asUintN(64, hash * prime);
  }
  return hash.toString(16).padStart(16, "0");
}

export function isSetupApAddress(value) {
  if (!value) return false;
  try {
    return SETUP_AP_HOSTS.has(new URL(normalizeDeviceAddress(value)).hostname);
  } catch {
    return false;
  }
}

export function durableGatewayAddresses(gateway, explicitAddress = "") {
  const candidates = [
    explicitAddress,
    ...(gateway?.addresses || []),
    gateway?.address,
    gateway?.id ? `http://lora-gateway-${gateway.id}.local` : "",
  ];
  const result = [];
  for (const candidate of candidates) {
    if (!candidate) continue;
    const text = String(candidate).trim();
    if (!text || NON_ADDRESS_VALUES.has(text.toLowerCase())) continue;
    try {
      const normalized = normalizeDeviceAddress(text);
      if (isSetupApAddress(normalized) || result.includes(normalized)) continue;
      result.push(normalized);
    } catch {
      // A malformed saved address should not prevent trying other transports.
    }
  }
  return result;
}

export function gatewayRecordAddress(config, sessionAddress, previous = {}) {
  const candidates = [
    config?.network_ip,
    sessionAddress,
    previous.address,
    ...(previous.addresses || []),
  ];
  const addresses = durableGatewayAddresses(
    { id: config?.gateway_id || previous.id, addresses: candidates },
  );
  return {
    address: addresses[0] || null,
    addresses,
  };
}

export async function encryptGatewayRegistration(
  ownerKey,
  requestId,
  tracker,
  expectedRevision,
  cryptoApi = globalThis.crypto,
) {
  if (!cryptoApi?.subtle || !cryptoApi?.getRandomValues) {
    throw new Error("Secure command encryption is unavailable");
  }
  const key = await cryptoApi.subtle.importKey(
    "raw",
    hexToBytes(ownerKey),
    "AES-GCM",
    false,
    ["encrypt"],
  );
  const nonce = cryptoApi.getRandomValues(new Uint8Array(12));
  const plaintext = new TextEncoder().encode(JSON.stringify({
    device_id: tracker.device_id,
    device_name: tracker.device_name,
    lora_aead_key: tracker.lora_aead_key,
    expected_revision: expectedRevision,
  }));
  const encrypted = new Uint8Array(await cryptoApi.subtle.encrypt(
    { name: "AES-GCM", iv: nonce, additionalData: commandAad(requestId), tagLength: 128 },
    key,
    plaintext,
  ));
  return {
    nonce: bytesToHex(nonce),
    ciphertext: bytesToBase64(encrypted),
  };
}

function requestId() {
  const random = globalThis.crypto?.randomUUID?.().replaceAll("-", "");
  return random || `${Date.now().toString(36)}${Math.random().toString(36).slice(2)}`;
}

export class GatewayMqttTransport {
  constructor({
    mqtt,
    baseTopic,
    gatewayId,
    ownerKey,
    expectedRevision,
    timeoutMs = 15000,
  }) {
    this.mqtt = mqtt;
    this.baseTopic = String(baseTopic || "lora-tracker").replace(/^\/+|\/+$/g, "");
    this.gatewayId = gatewayId;
    this.gatewayHash = gatewayHashFromId(gatewayId);
    this.ownerKey = ownerKey;
    this.expectedRevision = expectedRevision;
    this.timeoutMs = timeoutMs;
  }

  async registerTracker(tracker) {
    if (!this.mqtt?.mqttConnected) throw new Error("MQTT is not connected");
    if (!Number.isSafeInteger(this.expectedRevision) || this.expectedRevision < 1) {
      throw new Error("Gateway revision is unknown; wait for its MQTT status or use LAN/Bluetooth");
    }
    const id = requestId();
    const prefix = `${this.baseTopic}/v1/gateways/${this.gatewayHash}`;
    const responseTopic = `${prefix}/commands/response/${id}`;
    const envelope = await encryptGatewayRegistration(
      this.ownerKey,
      id,
      tracker,
      this.expectedRevision,
    );
    const payload = JSON.stringify({
      api_version: API_VERSION,
      schema_version: SCHEMA_VERSION,
      request_id: id,
      command: COMMAND,
      ...envelope,
    });

    return new Promise((resolve, reject) => {
      const cleanup = () => {
        clearTimeout(timer);
        this.mqtt.removeEventListener("message", onMessage);
      };
      const onMessage = (event) => {
        if (event.detail?.topic !== responseTopic) return;
        let response;
        try {
          response = JSON.parse(event.detail.payload);
        } catch {
          cleanup();
          reject(new Error("Gateway returned an invalid MQTT response"));
          return;
        }
        if (
          response.api_version !== API_VERSION ||
          response.schema_version !== SCHEMA_VERSION ||
          response.request_id !== id ||
          response.command !== COMMAND
        ) {
          cleanup();
          reject(new Error("Gateway returned a mismatched MQTT response"));
          return;
        }
        cleanup();
        if (!response.ok) {
          reject(new Error(response.message || response.error || "Gateway rejected registration"));
          return;
        }
        resolve(response);
      };
      const timer = setTimeout(() => {
        cleanup();
        reject(new Error(`No MQTT reply from ${this.gatewayId} within ${Math.round(this.timeoutMs / 1000)} seconds`));
      }, this.timeoutMs);

      this.mqtt.addEventListener("message", onMessage);
      // Subscribe once per gateway rather than retaining one unique topic for
      // every request across MQTT reconnects.
      this.mqtt.subscribe(`${prefix}/commands/response/+`);
      try {
        this.mqtt.publish(`${prefix}/commands/request`, payload);
      } catch (error) {
        cleanup();
        reject(error);
      }
    });
  }

  async close() {}
}
