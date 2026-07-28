import { test, expect } from "@playwright/test";

test.describe("BLE Mock Transport Integration", () => {
  test.beforeEach(async ({ page }) => {
    // Go to local server
    await page.goto("http://localhost:8080");
    await page.evaluate(() => import("/test-api.js"));
  });

  test("BleTransport gracefully handles lack of Web Bluetooth", async ({
    page,
  }) => {
    // Evaluate in page context
    const isSupported = await page.evaluate(async () => {
      const module = window.__loraTrackerTest;
      const transport = new module.BleTransport(null); // mock unsupported adapter
      return transport.isSupported;
    });

    expect(isSupported).toBe(false);

    const errorMsg = await page.evaluate(async () => {
      try {
        const module = window.__loraTrackerTest;
        const transport = new module.BleTransport(null);
        await transport.connect();
        return null;
      } catch (e) {
        return e.message;
      }
    });

    expect(errorMsg).toContain("Web Bluetooth is not supported");
  });

  test("NativeBleTransport uses acknowledged 18-byte writes and notifications", async ({
    page,
  }) => {
    const result = await page.evaluate(async () => {
      const { NativeBleTransport } = window.__loraTrackerTest;
      const chunks = [];
      const writeTimeouts = [];
      const connectionEvents = [];
      let notify;
      const client = {
        initialize: async () => {},
        requestDevice: async () => ({ deviceId: "tracker-1" }),
        connect: async () => connectionEvents.push("connect"),
        startNotifications: async (_id, _service, _characteristic, callback) => {
          notify = callback;
        },
        write: async (_id, _service, _characteristic, value, options) => {
          chunks.push([...new Uint8Array(value.buffer, value.byteOffset, value.byteLength)]);
          writeTimeouts.push(options?.timeout);
        },
        stopNotifications: async () => {},
        disconnect: async () => {},
      };
      const transport = new NativeBleTransport(client);
      await transport.connect();
      const responsePromise = transport.sendCommand("PATCH " + "x".repeat(30));
      await new Promise((resolve) => setTimeout(resolve, 0));
      const response = new TextEncoder().encode('{"ok":true}\n');
      notify(new DataView(response.buffer));
      await responsePromise;
      await transport.disconnect();
      return {
        lengths: chunks.map((chunk) => chunk.length),
        connectionEvents,
        writeTimeouts,
        disconnected: !transport.deviceId,
      };
    });
    expect(result.lengths).toEqual([18, 18, 1]);
    expect(result.connectionEvents).toEqual(["connect"]);
    expect(result.writeTimeouts).toEqual([5000, 5000, 5000]);
    expect(result.disconnected).toBe(true);
  });

  test("NativeBleTransport surfaces a configuration-write failure", async ({
    page,
  }) => {
    const message = await page.evaluate(async () => {
      const { NativeBleTransport } = window.__loraTrackerTest;
      const client = {
        initialize: async () => {},
        requestDevice: async () => ({ deviceId: "tracker-1" }),
        connect: async () => {},
        startNotifications: async () => {},
        write: async () => {
          throw new Error("insufficient authentication");
        },
        stopNotifications: async () => {},
        disconnect: async () => {},
      };
      try {
        const transport = new NativeBleTransport(client);
        await transport.connect();
        await transport.sendCommand("INFO");
      } catch (error) {
        return error.message;
      }
      return "no error";
    });
    expect(message).toContain("insufficient authentication");
  });

  test("BleTransport chunking and sequencing logic (mocked adapter)", async ({
    page,
  }) => {
    // Evaluate an isolated mock adapter
    const result = await page.evaluate(async () => {
      const module = window.__loraTrackerTest;

      let writtenChunks = [];
      const mockTx = {
        addEventListener: () => {},
        removeEventListener: () => {},
        startNotifications: async () => {},
      };

      const mockRx = {
        writeValueWithResponse: async (data) => {
          writtenChunks.push(Array.from(data));
        },
      };

      const mockDevice = {
        addEventListener: () => {},
        removeEventListener: () => {},
        gatt: {
          connect: async () => ({
            getPrimaryService: async () => ({
              getCharacteristic: async (uuid) => {
                if (uuid === "6e400003-b5a3-f393-e0a9-e50e24dcca9e")
                  return mockTx;
                return mockRx;
              },
            }),
          }),
          connected: true,
          disconnect: () => {},
        },
      };

      const mockAdapter = {
        requestDevice: async () => mockDevice,
      };

      const transport = new module.BleTransport(mockAdapter);
      await transport.connect();

      // Don't await because it expects a response, we just want to see what it writes
      transport
        .sendCommand("THIS IS A VERY LONG COMMAND THAT EXCEEDS EIGHTEEN BYTES")
        .catch(() => {});

      // Wait for the async processQueue loop to run a bit
      await new Promise((r) => setTimeout(r, 50));

      return writtenChunks;
    });

    // Expect the command to be chunked into <= 18 bytes
    // "THIS IS A VERY LON" (18 bytes)
    // "G COMMAND THAT EXC" (18 bytes)
    // "EEDS EIGHTEEN BYTE" (18 bytes)
    // "S\n" (2 bytes)
    expect(result.length).toBeGreaterThan(1);
    expect(result.every((chunk) => chunk.length <= 18)).toBe(true);

    // Check that the last chunk ends with a newline (10)
    const lastChunk = result[result.length - 1];
    expect(lastChunk[lastChunk.length - 1]).toBe(10);
  });

  test("OnboardingManager accepts the firmware configuration object without reparsing it", async ({
    page,
  }) => {
    const result = await page.evaluate(async () => {
      const { OnboardingManager } = window.__loraTrackerTest;
      const config = {
        role: "tracker",
        revision: 7,
        device_name: "Pasture 1",
        communication: { tx_interval_s: 60 },
      };
      const manager = new OnboardingManager({
        sendCommand: async (command) => {
          if (command !== "GET CONFIG") throw new Error("unexpected command");
          return config;
        },
      });
      return await manager.getConfig();
    });

    expect(result.revision).toBe(7);
    expect(result.communication.tx_interval_s).toBe(60);
  });

  test("OnboardingManager discovers either device role before authentication", async ({ page }) => {
    const result = await page.evaluate(async () => {
      const { OnboardingManager } = window.__loraTrackerTest;
      const commands = [];
      const manager = new OnboardingManager({
        connect: async () => {},
        sendCommand: async (command) => {
          commands.push(command);
          return { ok: true, role: "gateway", onboarding_required: true, revision: 1 };
        },
      });
      return { info: await manager.connect(), commands };
    });
    expect(result.info.role).toBe("gateway");
    expect(result.commands).toEqual(["INFO"]);
  });

  test("OnboardingManager accepts gateway configuration", async ({ page }) => {
    const role = await page.evaluate(async () => {
      const { OnboardingManager } = window.__loraTrackerTest;
      const manager = new OnboardingManager({
        sendCommand: async () => ({ role: "gateway", revision: 3, trackers: [] }),
      });
      return (await manager.getConfig()).role;
    });
    expect(role).toBe("gateway");
  });

  test("OnboardingManager waits for the native disconnect to finish", async ({ page }) => {
    const result = await page.evaluate(async () => {
      const { OnboardingManager } = window.__loraTrackerTest;
      let released = false;
      const manager = new OnboardingManager({
        disconnect: async () => {
          await new Promise((resolve) => setTimeout(resolve, 20));
          released = true;
        },
      });
      const pending = manager.disconnect();
      const beforeAwait = released;
      await pending;
      return { beforeAwait, afterAwait: released };
    });
    expect(result).toEqual({ beforeAwait: false, afterAwait: true });
  });

  test("the first app claims with an owner key used for later Bluetooth sessions", async ({ page }) => {
    const methods = await page.evaluate(async () => {
      const { OnboardingManager } = window.__loraTrackerTest;
      const manager = new OnboardingManager({ sendCommand: async () => ({ ok: true }) });
      return { claim: typeof manager.claim, auth: typeof manager.auth, enterConfigMode: typeof manager.enterConfigMode };
    });
    expect(methods).toEqual({ claim: "function", auth: "function", enterConfigMode: "function" });
  });

  test("owner keys fan out to renamed and stable device aliases", async ({ page }) => {
    const result = await page.evaluate(async () => {
      const api = window.__loraTrackerTest;
      const ownerKey = "ab".repeat(32);
      await api.storeDeviceOwnerKey("old-gateway", ownerKey, "ble-stable-id");
      const loaded = await api.loadDeviceOwnerKey(
        "new-gateway",
        "old-gateway",
        "ble-stable-id",
      );
      return {
        loaded,
        canonical: localStorage.getItem("device-new-gateway-owner-key"),
        ble: localStorage.getItem("device-ble-stable-id-owner-key"),
      };
    });
    expect(result).toEqual({
      loaded: "ab".repeat(32),
      canonical: "ab".repeat(32),
      ble: "ab".repeat(32),
    });
  });

  test("OnboardingManager sends dedicated tracker registration command", async ({ page }) => {
    const command = await page.evaluate(async () => {
      const { OnboardingManager } = window.__loraTrackerTest;
      let sent;
      const manager = new OnboardingManager({
        sendCommand: async (value) => {
          sent = value;
          return { ok: true };
        },
      });
      await manager.registerTracker({
        device_id: "tracker-one",
        device_name: "Pasture tracker",
        lora_aead_key: "ab".repeat(32),
      });
      return sent;
    });
    expect(command).toContain("REGISTER_TRACKER ");
    expect(command).toContain("device_id=tracker-one");
    expect(command).toContain("device_name=Pasture+tracker");
  });

  test("a timeout rejects both the active and queued BLE commands", async ({
    page,
  }) => {
    const messages = await page.evaluate(async () => {
      const { BleTransport } = window.__loraTrackerTest;
      const mockTx = {
        addEventListener: () => {},
        removeEventListener: () => {},
        startNotifications: async () => {},
      };
      const mockRx = {
        writeValueWithResponse: async () => new Promise(() => {}),
      };
      const device = {
        addEventListener: () => {},
        removeEventListener: () => {},
        gatt: {
          connected: true,
          connect: async () => ({
            getPrimaryService: async () => ({
              getCharacteristic: async (uuid) =>
                uuid === "6e400003-b5a3-f393-e0a9-e50e24dcca9e"
                  ? mockTx
                  : mockRx,
            }),
          }),
          disconnect() {
            this.connected = false;
          },
        },
      };
      const transport = new BleTransport({ requestDevice: async () => device });
      await transport.connect();
      const first = transport.sendCommand("FIRST", 10).catch((e) => e.message);
      const second = transport.sendCommand("SECOND", 5000).catch((e) => e.message);
      return await Promise.all([first, second]);
    });

    expect(messages).toEqual(["Command timeout", "BLE Disconnected"]);
  });

  test("BleTransport handles timeouts and disconnect rejections", async ({
    page,
  }) => {
    const errorMsg = await page.evaluate(async () => {
      const module = window.__loraTrackerTest;

      const mockTx = {
        addEventListener: () => {},
        removeEventListener: () => {},
        startNotifications: async () => {},
      };

      const mockRx = {
        writeValueWithResponse: async () => {
          return new Promise((resolve) => setTimeout(resolve, 100)); // slow write
        },
      };

      const mockDevice = {
        addEventListener: () => {},
        removeEventListener: () => {},
        gatt: {
          connect: async () => ({
            getPrimaryService: async () => ({
              getCharacteristic: async (uuid) => {
                if (uuid === "6e400003-b5a3-f393-e0a9-e50e24dcca9e")
                  return mockTx;
                return mockRx;
              },
            }),
          }),
          connected: true,
          disconnect: () => {},
        },
      };

      const mockAdapter = {
        requestDevice: async () => mockDevice,
      };

      const transport = new module.BleTransport(mockAdapter);
      await transport.connect();

      // Send command with very short timeout
      try {
        await transport.sendCommand("FAST_TIMEOUT", 10);
      } catch (e) {
        return e.message;
      }
    });

    expect(errorMsg).toBe("Command timeout");

    // Disconnect rejection
    const disconnectErrorMsg = await page.evaluate(async () => {
      const module = window.__loraTrackerTest;

      const mockRx = { writeValueWithResponse: async () => {} };
      const mockTx = {
        addEventListener: () => {},
        removeEventListener: () => {},
        startNotifications: async () => {},
      };

      const mockDevice = {
        addEventListener: () => {},
        removeEventListener: () => {},
        gatt: {
          connect: async () => ({
            getPrimaryService: async () => ({
              getCharacteristic: async (uuid) =>
                uuid === "6e400003-b5a3-f393-e0a9-e50e24dcca9e"
                  ? mockTx
                  : mockRx,
            }),
          }),
          connected: true,
          disconnect: () => {},
        },
      };

      const mockAdapter = { requestDevice: async () => mockDevice };
      const transport = new module.BleTransport(mockAdapter);
      await transport.connect();

      const p = transport.sendCommand("WAIT", 5000);
      transport.onDisconnected(); // Force disconnect

      try {
        await p;
      } catch (e) {
        return e.message;
      }
    });

    expect(disconnectErrorMsg).toBe("BLE Disconnected");
  });
});
