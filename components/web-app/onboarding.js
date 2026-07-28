import { Capacitor } from "@capacitor/core";
import { BleClient } from "@capacitor-community/bluetooth-le";

const SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
const RX_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";
const TX_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";
export class BleTransport {
  constructor(adapter = navigator.bluetooth) {
    this.adapter = adapter;
    this.device = null;
    this.rx = null;
    this.tx = null;
    this.queue = [];
    this.activeCommand = null;
    this.buffer = "";
    this.decoder = new TextDecoder();
    this.disconnectHandler = this.onDisconnected.bind(this);
    this.notificationHandler = this.onNotification.bind(this);
    this.onDisconnect = null;
    this.disconnected = true;
  }

  get isSupported() {
    return Boolean(this.adapter);
  }

  async connect() {
    if (!this.isSupported)
      throw new Error(
        "Web Bluetooth is not supported in this browser (e.g., iOS Safari).",
      );

    this.device = await this.adapter.requestDevice({
      filters: [{ services: ["6e400001-b5a3-f393-e0a9-e50e24dcca9e"] }],
      optionalServices: ["6e400001-b5a3-f393-e0a9-e50e24dcca9e"],
    });
    this.disconnected = false;

    this.device.addEventListener(
      "gattserverdisconnected",
      this.disconnectHandler,
    );

    try {
      const server = await this.device.gatt.connect();
      const service = await server.getPrimaryService(
        "6e400001-b5a3-f393-e0a9-e50e24dcca9e",
      );

      this.rx = await service.getCharacteristic(
        "6e400002-b5a3-f393-e0a9-e50e24dcca9e",
      );
      this.tx = await service.getCharacteristic(
        "6e400003-b5a3-f393-e0a9-e50e24dcca9e",
      );

      this.tx.addEventListener(
        "characteristicvaluechanged",
        this.notificationHandler,
      );
      await this.tx.startNotifications();
    } catch (error) {
      this.onDisconnected();
      throw error;
    }
  }

  disconnect() {
    if (this.device && this.device.gatt.connected) {
      this.device.gatt.disconnect();
    }
    this.onDisconnected();
  }

  onDisconnected() {
    if (this.disconnected) return;
    this.disconnected = true;
    if (this.tx) {
      this.tx.removeEventListener(
        "characteristicvaluechanged",
        this.notificationHandler,
      );
    }
    if (this.device) {
      this.device.removeEventListener(
        "gattserverdisconnected",
        this.disconnectHandler,
      );
    }
    this.device = null;
    this.rx = null;
    this.tx = null;
    this.buffer = "";
    this.decoder = new TextDecoder();

    const error = new Error("BLE Disconnected");
    if (this.activeCommand) {
      clearTimeout(this.activeCommand.timer);
      this.activeCommand.reject(error);
      this.activeCommand = null;
    }
    for (const cmd of this.queue) cmd.reject(error);
    this.queue = [];
    this.onDisconnect?.();
  }

  onNotification(event) {
    const value = this.decoder.decode(event.target.value, { stream: true });
    this.buffer += value;

    if (this.buffer.length > 32768) {
      this.buffer = ""; // Prevent infinite growth on malformed data
      return;
    }

    let newlineIdx;
    while ((newlineIdx = this.buffer.indexOf("\n")) >= 0) {
      const line = this.buffer.slice(0, newlineIdx).trim();
      this.buffer = this.buffer.slice(newlineIdx + 1);

      if (line.length > 0 && this.activeCommand && line.startsWith("{")) {
        try {
          const parsed = JSON.parse(line);
          clearTimeout(this.activeCommand.timer);
          const cmd = this.activeCommand;
          this.activeCommand = null;
          if (parsed?.ok === false) {
            const detail = parsed.detail ? `: ${parsed.detail}` : "";
            cmd.reject(
              new Error(
                `${parsed.error || "Device rejected command"}${detail}`,
              ),
            );
          } else {
            cmd.resolve(parsed);
          }
        } catch (e) {
          clearTimeout(this.activeCommand.timer);
          const cmd = this.activeCommand;
          this.activeCommand = null;
          cmd.reject(new Error(`Invalid JSON response: ${line}`));
        }
        this.processQueue();
      } else if (line.length > 0) {
        console.warn("Unsolicited BLE message:", line);
      }
    }
  }

  async sendCommand(cmd, timeoutMs = 5000) {
    if (!this.rx) throw new Error("Not connected");
    if (new TextEncoder().encode(cmd).length > 16382)
      throw new Error("Command too long");

    return new Promise((resolve, reject) => {
      this.queue.push({ cmd, resolve, reject, timeoutMs });
      if (!this.activeCommand) this.processQueue();
    });
  }

  async processQueue() {
    if (this.activeCommand || this.queue.length === 0) return;

    const cmd = this.queue.shift();
    this.activeCommand = cmd;
    cmd.timer = setTimeout(() => {
      if (this.activeCommand === cmd) {
        this.activeCommand = null;
        cmd.reject(new Error("Command timeout"));
        if (this.device?.gatt?.connected) this.device.gatt.disconnect();
        this.onDisconnected();
      }
    }, cmd.timeoutMs);

    try {
      const payload = new TextEncoder().encode(`${cmd.cmd}\n`);
      let offset = 0;
      while (offset < payload.length) {
        const chunkLen = Math.min(18, payload.length - offset);
        const chunk = payload.slice(offset, offset + chunkLen);
        await this.rx.writeValueWithResponse(chunk);
        offset += chunkLen;
      }
    } catch (e) {
      if (this.activeCommand === cmd) {
        clearTimeout(cmd.timer);
        this.activeCommand = null;
        cmd.reject(e);
      }
      this.disconnect();
    }
  }
}

export class NativeBleTransport extends BleTransport {
  constructor(client = BleClient) {
    super(null);
    this.client = client;
    this.deviceId = null;
  }

  get isSupported() {
    return Capacitor.isNativePlatform();
  }

  async connect(deviceId = null) {
    await this.client.initialize({ androidNeverForLocation: true });
    if (deviceId) {
      this.deviceId = deviceId;
    } else {
      const device = await this.client.requestDevice({ services: [SERVICE_UUID] });
      this.deviceId = device.deviceId;
    }
    this.disconnected = false;
    try {
      await this.client.connect(
        this.deviceId,
        () => this.onDisconnected(),
        { timeout: 10000 },
      );
      await this.client.startNotifications(
        this.deviceId,
        SERVICE_UUID,
        TX_UUID,
        (value) => this.onNotification({ target: { value } }),
        { timeout: 5000 },
      );
      // The base transport uses rx as its connected/write-ready marker.
      this.rx = true;
    } catch (error) {
      await this.disconnect();
      throw error instanceof Error ? error : new Error(String(error));
    }
  }

  async disconnect() {
    const deviceId = this.deviceId;
    this.deviceId = null;
    if (deviceId) {
      try {
        await this.client.stopNotifications(deviceId, SERVICE_UUID, TX_UUID);
      } catch {}
      try {
        await this.client.disconnect(deviceId);
      } catch {}
    }
    this.finishDisconnect();
  }

  onDisconnected() {
    this.deviceId = null;
    this.finishDisconnect();
  }

  finishDisconnect() {
    if (this.disconnected) return;
    this.disconnected = true;
    this.rx = null;
    this.buffer = "";
    this.decoder = new TextDecoder();
    const error = new Error("BLE Disconnected");
    if (this.activeCommand) {
      clearTimeout(this.activeCommand.timer);
      this.activeCommand.reject(error);
      this.activeCommand = null;
    }
    for (const cmd of this.queue) cmd.reject(error);
    this.queue = [];
    this.onDisconnect?.();
  }

  async processQueue() {
    if (this.activeCommand || this.queue.length === 0) return;
    if (!this.deviceId) {
      this.finishDisconnect();
      return;
    }

    const cmd = this.queue.shift();
    this.activeCommand = cmd;
    cmd.timer = setTimeout(() => {
      if (this.activeCommand === cmd) {
        this.activeCommand = null;
        cmd.reject(new Error("Command timeout"));
        this.disconnect();
      }
    }, cmd.timeoutMs);

    try {
      const payload = new TextEncoder().encode(`${cmd.cmd}\n`);
      for (let offset = 0; offset < payload.length; offset += 18) {
        const chunk = payload.slice(offset, offset + 18);
        await this.client.write(
          this.deviceId,
          SERVICE_UUID,
          RX_UUID,
          new DataView(chunk.buffer, chunk.byteOffset, chunk.byteLength),
          { timeout: 5000 },
        );
      }
    } catch (error) {
      if (this.activeCommand === cmd) {
        clearTimeout(cmd.timer);
        this.activeCommand = null;
        cmd.reject(error);
      }
      await this.disconnect();
    }
  }
}

export function createBleTransport() {
  return Capacitor.isNativePlatform() ? new NativeBleTransport() : new BleTransport();
}

// Discovery is deliberately separate from connecting.  A scan must never try
// to connect to every advertisement: that would wake trackers and repeatedly
// trigger device connections. The app keeps the scan running at the platform-selected
// low-power rate and lets the operator choose an unpaired tracker.
export class BleDeviceScanner {
  constructor(client = BleClient) {
    this.client = client;
    this.running = false;
  }

  get isSupported() {
    return Capacitor.isNativePlatform() || Boolean(navigator.bluetooth?.requestLEScan);
  }

  async start(onDevice) {
    if (this.running || !this.isSupported) return;
    if (Capacitor.isNativePlatform()) {
      await this.client.initialize({ androidNeverForLocation: true });
      await this.client.requestLEScan(
        { services: [SERVICE_UUID], allowDuplicates: false },
        (result) => onDevice?.(result.device || result),
      );
      this.running = true;
      return;
    }

    const scan = await navigator.bluetooth.requestLEScan({
      filters: [{ services: [SERVICE_UUID] }],
      keepRepeatedDevices: false,
    });
    const listener = (event) => onDevice?.({
      deviceId: event.device.id,
      name: event.device.name,
    });
    navigator.bluetooth.addEventListener("advertisementreceived", listener);
    this.stopWebScan = () => {
      navigator.bluetooth.removeEventListener("advertisementreceived", listener);
      scan.stop();
    };
    this.running = true;
  }

  async stop() {
    if (!this.running) return;
    this.running = false;
    if (Capacitor.isNativePlatform()) {
      await this.client.stopLEScan();
    } else {
      this.stopWebScan?.();
      this.stopWebScan = null;
    }
  }
}

export function createBleDeviceScanner() {
  return new BleDeviceScanner();
}

export class OnboardingManager {
  constructor(transport = new BleTransport()) {
    this.transport = transport;
  }

  async connect(deviceId = null) {
    await this.transport.connect(deviceId);
    this.info = await this.transport.sendCommand("INFO");
    if (!this.info || !["tracker", "gateway"].includes(this.info.role)) {
      throw new Error("Unsupported device configuration service");
    }
    return this.info;
  }

  disconnect() {
    return this.transport.disconnect();
  }

  async claim(ownerKey) {
    return this.transport.sendCommand(`CLAIM ${ownerKey}`);
  }

  async auth(ownerKey) {
    return this.transport.sendCommand(`AUTH ${ownerKey}`);
  }

  async enterConfigMode() {
    return this.transport.sendCommand("ENTER_CONFIG_MODE", 15000);
  }

  async getConfig() {
    this.lastConfig = await this.transport.sendCommand("GET CONFIG", 15000);
    if (
      !this.lastConfig ||
      !["tracker", "gateway"].includes(this.lastConfig.role) ||
      !Number.isSafeInteger(this.lastConfig.revision)
    ) {
      this.lastConfig = null;
      throw new Error("Device returned an invalid configuration response");
    }
    return this.lastConfig;
  }

  async patchConfig(expectedRevision, fields) {
    const params = new URLSearchParams({
      expected_revision: expectedRevision,
      ...fields,
    }).toString();
    return this.transport.sendCommand(`PATCH ${params}`, 30000);
  }

  async pairGateway(gatewayId) {
    return this.transport.sendCommand(`PAIR_GATEWAY ${gatewayId}`);
  }

  async registerTracker(tracker) {
    const params = new URLSearchParams({
      device_id: tracker.device_id,
      device_name: tracker.device_name,
      lora_aead_key: tracker.lora_aead_key,
    }).toString();
    return this.transport.sendCommand(`REGISTER_TRACKER ${params}`, 30000);
  }

  async unpairGateway() {
    return this.transport.sendCommand("UNPAIR_GATEWAY");
  }

  async rollback() {
    if (!this.lastConfig) throw new Error("Must fetch config before rollback");
    if (
      confirm(
        "Are you sure you want to rollback to the previous configuration?",
      )
    ) {
      return this.transport.sendCommand(`ROLLBACK ${this.lastConfig.revision}`);
    }
  }

  async reboot() {
    if (confirm("Reboot the tracker?")) {
      return this.transport.sendCommand("REBOOT");
    }
  }

  async factoryReset() {
    if (
      confirm("WARNING: Factory reset will erase all configuration. Continue?")
    ) {
      return this.transport.sendCommand("FACTORY_RESET FACTORY_RESET");
    }
  }
}
