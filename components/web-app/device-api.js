import { CapacitorHttp } from "@capacitor/core";
import { isNativeApp } from "./platform.js";

export function normalizeDeviceAddress(value) {
  let address = String(value || "").trim();
  if (!address) throw new Error("Enter the device IP address or hostname");
  if (/^[a-z][a-z0-9+.-]*:\/\//i.test(address) && !/^https?:\/\//i.test(address)) {
    throw new Error("Use an HTTP or HTTPS address");
  }
  if (!/^https?:\/\//i.test(address)) address = `http://${address}`;
  const url = new URL(address);
  if (!/^https?:$/.test(url.protocol)) throw new Error("Use an HTTP or HTTPS address");
  url.pathname = "";
  url.search = "";
  url.hash = "";
  return url.toString().replace(/\/$/, "");
}

function bearerAuthorization(ownerKey) {
  if (!ownerKey) throw new Error("This device's owner key is not available");
  return `Bearer ${ownerKey}`;
}

function parseBody(data) {
  if (typeof data !== "string") return data;
  try {
    return JSON.parse(data);
  } catch {
    return data;
  }
}

export class WifiDeviceTransport {
  constructor(address, ownerKey = null, { timeoutMs = 10000 } = {}) {
    this.address = normalizeDeviceAddress(address);
    this.ownerKey = ownerKey;
    this.timeoutMs = timeoutMs;
  }

  async request(path, { method = "GET", fields, authenticated = true } = {}) {
    const headers = {
      Accept: "application/json",
    };
    if (authenticated) headers.Authorization = bearerAuthorization(this.ownerKey);
    let data;
    if (fields) {
      headers["Content-Type"] = "application/x-www-form-urlencoded";
      data = new URLSearchParams(fields).toString();
    }
    let status;
    let body;
    if (isNativeApp) {
      const response = await CapacitorHttp.request({
        url: `${this.address}${path}`,
        method,
        headers,
        data,
        connectTimeout: Math.min(5000, this.timeoutMs),
        readTimeout: this.timeoutMs,
        responseType: "text",
      });
      status = response.status;
      body = parseBody(response.data);
    } else {
      const response = await fetch(`${this.address}${path}`, {
        method,
        headers,
        body: data,
        signal: AbortSignal.timeout(this.timeoutMs),
      });
      status = response.status;
      body = parseBody(await response.text());
    }
    if (status < 200 || status >= 300) {
      const detail = body?.error || body?.detail || `HTTP ${status}`;
      const error = new Error(String(detail));
      error.status = status;
      error.response = body;
      throw error;
    }
    return body;
  }

  async getInfo() {
    const info = await this.request("/api/v1/onboarding", { authenticated: false });
    this.role = info?.role;
    return info;
  }

  claim(ownerKey) {
    return this.request("/api/v1/claim", {
      method: "POST",
      fields: { owner_key: ownerKey },
      authenticated: false,
    }).then((result) => {
      this.ownerKey = ownerKey;
      return result;
    });
  }

  getConfig() {
    return this.request("/api/v1/config");
  }

  async patchConfig(expectedRevision, fields) {
    if (this.role === "repeater") {
      return this.request("/save", {
        method: "POST",
        fields: { expected_revision: expectedRevision, ...fields },
      });
    }
    return this.request("/api/v1/config", {
      method: "POST",
      fields: { expected_revision: expectedRevision, ...fields },
    });
  }

  async registerTracker(tracker) {
    return this.request("/api/v1/trackers", {
      method: "POST",
      fields: {
        device_id: tracker.device_id,
        device_name: tracker.device_name,
        lora_aead_key: tracker.lora_aead_key,
      },
    });
  }

  pairGateway(gatewayId) {
    return this.request("/api/v1/gateway-pairing", {
      method: "POST",
      fields: { gateway_id: gatewayId },
    });
  }

  unpairGateway() {
    return this.request("/api/v1/gateway-pairing", { method: "DELETE" });
  }

  rollback(expectedRevision) {
    return this.request("/api/v1/config/rollback", {
      method: "POST",
      fields: { expected_revision: expectedRevision },
    });
  }

  reboot() {
    return this.request("/api/v1/reboot", { method: "POST" });
  }

  enterConfigMode() {
    return this.request("/api/v1/config-mode", { method: "POST" });
  }

  factoryReset() {
    return this.request("/api/v1/factory-reset", {
      method: "POST",
      fields: { confirm: "FACTORY_RESET" },
    });
  }
}
