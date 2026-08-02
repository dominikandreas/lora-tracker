import test from "node:test";
import assert from "node:assert/strict";

import { normalizeDeviceAddress, WifiDeviceTransport } from "../device-api.js";
import { ownerKeyAction } from "../credential-policy.js";
import { pairTrackerTransaction } from "../pairing-transaction.js";

test("local device addresses default to HTTP and discard paths", () => {
  assert.equal(normalizeDeviceAddress("192.168.1.42"), "http://192.168.1.42");
  assert.equal(
    normalizeDeviceAddress("http://gateway.local/api/v1/config?x=1"),
    "http://gateway.local",
  );
});

test("unsupported local device protocols are rejected", () => {
  assert.throws(() => normalizeDeviceAddress("ftp://gateway.local"), /HTTP/);
});

test("an interrupted claim retries CLAIM with the locally staged key", () => {
  const stagedKey = "ab".repeat(32);
  assert.equal(ownerKeyAction({ owner_key_configured: false }, stagedKey), "claim");
  assert.equal(ownerKeyAction({ owner_key_configured: true }, stagedKey), "auth");
  assert.equal(ownerKeyAction({ owner_key_configured: true }, null), "unavailable");
});

test("device discovery is public but configuration uses the owner key", async () => {
  const originalFetch = globalThis.fetch;
  let request;
  globalThis.fetch = async (url, options) => {
    request = { url, options };
    return { status: 200, text: async () => '{"role":"tracker"}' };
  };
  try {
    const device = new WifiDeviceTransport("tracker.local", "ab".repeat(32));
    await device.getInfo();
    assert.equal(request.options.headers.Authorization, undefined);
    await device.getConfig();
  } finally {
    globalThis.fetch = originalFetch;
  }
  assert.equal(request.url, "http://tracker.local/api/v1/config");
  assert.equal(request.options.headers.Authorization, `Bearer ${"ab".repeat(32)}`);
});

test("Wi-Fi tracker registration uses the narrow idempotent endpoint", async () => {
  const originalFetch = globalThis.fetch;
  let request;
  globalThis.fetch = async (url, options) => {
    request = { url, options };
    return { status: 200, text: async () => '{"ok":true,"tracker_id":"tracker-two"}' };
  };
  try {
    const device = new WifiDeviceTransport("gateway.local", "paired-token");
    await device.registerTracker({
      device_id: "tracker-two",
      device_name: "Pasture tracker",
      lora_aead_key: "ab".repeat(32),
    });
  } finally {
    globalThis.fetch = originalFetch;
  }
  assert.equal(request.url, "http://gateway.local/api/v1/trackers");
  assert.equal(request.options.method, "POST");
  assert.match(request.options.body, /device_id=tracker-two/);
  assert.match(request.options.body, /lora_aead_key=/);
});

test("pairing keeps one tracker session and commits gateway before tracker", async () => {
  const steps = [];
  const progress = [];
  let gatewayClosed = false;
  const result = await pairTrackerTransaction({
    trackerConfig: {
      device_id: "tracker-one",
      device_name: "Pasture tracker",
      lora_aead_key: "ab".repeat(32),
    },
    gatewayRecord: { id: "gateway-one" },
    openGateway: async () => ({
      registerTracker: async () => {
        steps.push("register");
        return { ok: true, tracker_id: "tracker-one" };
      },
      close: async () => { gatewayClosed = true; },
    }),
    confirmTracker: async (gatewayId) => {
      steps.push(`confirm:${gatewayId}`);
      return { ok: true, gateway_paired: true, gateway_id: gatewayId };
    },
    onStep: (step) => progress.push(step),
  });
  assert.deepEqual(steps, ["register", "confirm:gateway-one"]);
  assert.deepEqual(progress, ["locate", "gateway", "tracker", "complete"]);
  assert.equal(gatewayClosed, true);
  assert.equal(result.gatewayId, "gateway-one");
});

test("pairing reports a safely retryable partial commit", async () => {
  await assert.rejects(
    pairTrackerTransaction({
      trackerConfig: {
        device_id: "tracker-one",
        device_name: "Pasture tracker",
        lora_aead_key: "ab".repeat(32),
      },
      gatewayRecord: { id: "gateway-one" },
      openGateway: async () => ({
        registerTracker: async () => ({ ok: true, tracker_id: "tracker-one" }),
      }),
      confirmTracker: async () => { throw new Error("radio disconnected"); },
    }),
    (error) => error.gatewayRegistrationCommitted === true && /Retry to finish/.test(error.message),
  );
});
