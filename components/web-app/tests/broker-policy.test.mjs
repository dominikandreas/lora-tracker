import test from "node:test";
import assert from "node:assert/strict";
import {
  evaluateBrokerTransport,
  normalizeBrokerWebSocketUrl,
} from "../broker-policy.js";

test("host-only broker URLs receive project WebSocket ports", () => {
  assert.equal(
    normalizeBrokerWebSocketUrl("ws://192.168.1.217"),
    "ws://192.168.1.217:1884/",
  );
  assert.equal(
    normalizeBrokerWebSocketUrl("wss://broker.example/mqtt"),
    "wss://broker.example:8884/mqtt",
  );
});

test("explicit WebSocket ports are preserved", () => {
  assert.equal(
    normalizeBrokerWebSocketUrl("ws://broker.local:9001/mqtt"),
    "ws://broker.local:9001/mqtt",
  );
  assert.equal(
    normalizeBrokerWebSocketUrl("wss://[2001:db8::1]:443/mqtt"),
    "wss://[2001:db8::1]:443/mqtt",
  );
});

test("native app permits explicitly handled private ws brokers", () => {
  const policy = evaluateBrokerTransport("ws://192.168.1.10:9001/mqtt", {
    native: true,
    pageProtocol: "https:",
  });
  assert.equal(policy.allowed, true);
  assert.equal(policy.insecure, true);
});

test("hosted HTTPS PWA still blocks mixed-content MQTT", () => {
  const policy = evaluateBrokerTransport("ws://broker.local:9001/mqtt", {
    native: false,
    pageProtocol: "https:",
  });
  assert.equal(policy.allowed, false);
});

test("secure MQTT is accepted on every application surface", () => {
  assert.equal(
    evaluateBrokerTransport("wss://broker.example/mqtt", {
      native: false,
      pageProtocol: "https:",
    }).allowed,
    true,
  );
});
