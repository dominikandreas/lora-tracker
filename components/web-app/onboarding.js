export class BleTransport {
  constructor(adapter = navigator.bluetooth) {
    this.adapter = adapter;
    this.device = null;
    this.rx = null;
    this.tx = null;
    this.queue = [];
    this.activeCommand = null;
    this.buffer = "";
    this.disconnectHandler = this.onDisconnected.bind(this);
    this.notificationHandler = this.onNotification.bind(this);
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
      filters: [{ namePrefix: "EqTrk-" }],
      optionalServices: ["6e400001-b5a3-f393-e0a9-e50e24dcca9e"],
    });

    this.device.addEventListener(
      "gattserverdisconnected",
      this.disconnectHandler,
    );

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
  }

  disconnect() {
    if (this.device && this.device.gatt.connected) {
      this.device.gatt.disconnect();
    }
    this.onDisconnected();
  }

  onDisconnected() {
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

    const error = new Error("BLE Disconnected");
    if (this.activeCommand) {
      clearTimeout(this.activeCommand.timer);
      this.activeCommand.reject(error);
      this.activeCommand = null;
    }
    for (const cmd of this.queue) cmd.reject(error);
    this.queue = [];
  }

  onNotification(event) {
    const value = new TextDecoder().decode(event.target.value);
    this.buffer += value;

    if (this.buffer.length > 4096) {
      this.buffer = ""; // Prevent infinite growth on malformed data
      return;
    }

    let newlineIdx;
    while ((newlineIdx = this.buffer.indexOf("\n")) >= 0) {
      const line = this.buffer.slice(0, newlineIdx).trim();
      this.buffer = this.buffer.slice(newlineIdx + 1);

      if (line.length > 0 && this.activeCommand) {
        try {
          const parsed = JSON.parse(line);
          clearTimeout(this.activeCommand.timer);
          const cmd = this.activeCommand;
          this.activeCommand = null;
          cmd.resolve(parsed);
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
    if (cmd.length > 1024) throw new Error("Command too long");

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
        if (this.device && this.device.gatt && this.device.gatt.connected) {
          this.device.gatt.disconnect();
        } else {
          this.onDisconnected();
        }
      }
    }, cmd.timeoutMs);

    try {
      const payload = new TextEncoder().encode(cmd.cmd);
      let offset = 0;
      while (offset < payload.length) {
        const chunkLen = Math.min(18, payload.length - offset);
        const chunk = new Uint8Array(
          chunkLen + (offset + chunkLen === payload.length ? 1 : 0),
        );
        chunk.set(payload.subarray(offset, offset + chunkLen));
        if (offset + chunkLen === payload.length) {
          chunk[chunkLen] = 10; // '\n'
        }
        await this.rx.writeValueWithResponse(chunk);
        offset += chunkLen;
      }
    } catch (e) {
      if (this.activeCommand === cmd) {
        clearTimeout(cmd.timer);
        this.activeCommand = null;
        cmd.reject(e);
        this.processQueue();
      }
    }
  }
}

export class OnboardingManager {
  constructor(transport = new BleTransport()) {
    this.transport = transport;
  }

  async connectTracker() {
    await this.transport.connect();
  }

  disconnect() {
    this.transport.disconnect();
  }

  async claim(password) {
    return this.transport.sendCommand(`CLAIM ${password}`);
  }

  async auth(password) {
    return this.transport.sendCommand(`AUTH ${password}`);
  }

  async getConfig() {
    this.lastConfig = await this.transport.sendCommand("GET CONFIG");
    return this.lastConfig;
  }

  async patchConfig(expectedRevision, fields) {
    const params = new URLSearchParams({
      expected_revision: expectedRevision,
      ...fields,
    }).toString();
    return this.transport.sendCommand(`PATCH ${params}`);
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
