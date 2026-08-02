import test from "node:test";
import assert from "node:assert/strict";
import { webcrypto } from "node:crypto";

import {
  durableGatewayAddresses,
  encryptGatewayRegistration,
  gatewayHashFromId,
  gatewayRecordAddress,
  GatewayMqttTransport,
  isSetupApAddress,
} from "../gateway-pairing.js";

test("gateway identity hash matches the firmware FNV-1a implementation", () => {
  assert.equal(gatewayHashFromId("gateway-15b80c"), "48e8f374775d7445");
});

test("setup hotspot addresses are never retained as gateway LAN addresses", () => {
  assert.equal(isSetupApAddress("http://192.168.4.1"), true);
  const resolved = gatewayRecordAddress(
    { gateway_id: "gateway-one", network_ip: "192.168.4.1" },
    "http://192.168.4.1",
    { address: "192.168.4.1" },
  );
  assert.equal(resolved.address, "http://lora-gateway-gateway-one.local");
  assert.deepEqual(resolved.addresses, ["http://lora-gateway-gateway-one.local"]);
});

test("offline gateway status does not become an http://off address", () => {
  const resolved = gatewayRecordAddress(
    { gateway_id: "gateway-one", network_ip: "off" },
    null,
    {},
  );
  assert.equal(resolved.address, "http://lora-gateway-gateway-one.local");
  assert.deepEqual(resolved.addresses, ["http://lora-gateway-gateway-one.local"]);
});

test("manual LAN address is tried before saved addresses and mDNS", () => {
  assert.deepEqual(
    durableGatewayAddresses(
      { id: "gateway-one", address: "192.168.1.20", addresses: ["192.168.1.21"] },
      "192.168.1.22",
    ),
    [
      "http://192.168.1.22",
      "http://192.168.1.21",
      "http://192.168.1.20",
      "http://lora-gateway-gateway-one.local",
    ],
  );
});

test("MQTT registration fields are encrypted under the gateway owner key", async () => {
  const ownerKey = "ab".repeat(32);
  const requestId = "request-one";
  const tracker = {
    device_id: "tracker-one",
    device_name: "Pasture tracker",
    lora_aead_key: "cd".repeat(32),
  };
  const envelope = await encryptGatewayRegistration(
    ownerKey,
    requestId,
    tracker,
    7,
    webcrypto,
  );
  assert.equal(envelope.nonce.length, 24);
  assert.doesNotMatch(envelope.ciphertext, /tracker-one|Pasture/);

  const key = await webcrypto.subtle.importKey(
    "raw",
    Uint8Array.from(ownerKey.match(/../g).map((pair) => Number.parseInt(pair, 16))),
    "AES-GCM",
    false,
    ["decrypt"],
  );
  const decoded = Uint8Array.from(atob(envelope.ciphertext), (char) => char.charCodeAt(0));
  const clear = await webcrypto.subtle.decrypt({
    name: "AES-GCM",
    iv: Uint8Array.from(envelope.nonce.match(/../g).map((pair) => Number.parseInt(pair, 16))),
    additionalData: new TextEncoder().encode(
      `lora-tracker|1|1|${requestId}|registry.upsert`,
    ),
    tagLength: 128,
  }, key, decoded);
  assert.deepEqual(JSON.parse(new TextDecoder().decode(clear)), {
    ...tracker,
    expected_revision: 7,
  });
});

test("MQTT transport subscribes before publishing and accepts only its response", async () => {
  class FakeMqtt extends EventTarget {
    mqttConnected = true;
    actions = [];
    subscribe(topic) {
      this.actions.push(["subscribe", topic]);
    }
    publish(topic, payload) {
      this.actions.push(["publish", topic, JSON.parse(payload)]);
      const request = this.actions.at(-1)[2];
      queueMicrotask(() => {
        const event = new Event("message");
        event.detail = {
          topic: `${topic.replace("commands/request", "commands/response")}/${request.request_id}`,
          payload: JSON.stringify({
            api_version: 1,
            schema_version: 1,
            request_id: request.request_id,
            command: "registry.upsert",
            ok: true,
            tracker_id: "tracker-one",
            transport: "MQTT",
          }),
        };
        this.dispatchEvent(event);
      });
    }
  }
  const mqtt = new FakeMqtt();
  const transport = new GatewayMqttTransport({
    mqtt,
    baseTopic: "lora-tracker",
    gatewayId: "gateway-15b80c",
    ownerKey: "ab".repeat(32),
    expectedRevision: 7,
    timeoutMs: 1000,
  });
  const response = await transport.registerTracker({
    device_id: "tracker-one",
    device_name: "Pasture tracker",
    lora_aead_key: "cd".repeat(32),
  });
  assert.equal(response.tracker_id, "tracker-one");
  assert.equal(mqtt.actions[0][0], "subscribe");
  assert.equal(mqtt.actions[1][0], "publish");
  assert.equal(
    mqtt.actions[1][1],
    "lora-tracker/v1/gateways/48e8f374775d7445/commands/request",
  );
  assert.equal(mqtt.actions[1][2].command, "registry.upsert");
  assert.equal("owner_key" in mqtt.actions[1][2], false);
});
