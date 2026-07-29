import test from "node:test";
import assert from "node:assert/strict";
import {
  applicationFieldsForMqttProfile,
  configurationFieldsForProfile,
  mqttWebSocketUrlForProfile,
  normalizeConfigurationProfiles,
  profileKindsForRole,
  removeConfigurationProfile,
  upsertConfigurationProfile,
} from "../configuration-profiles.js";

test("normalizes malformed profile storage without exposing extra fields", () => {
  const profiles = normalizeConfigurationProfiles({
    wifi: [{ id: "home", name: "Home", ssid: "pasture", password: "secret", ignored: true }],
    mqtt: "invalid",
  });
  assert.deepEqual(profiles, {
    wifi: [{ id: "home", name: "Home", ssid: "pasture", password: "secret" }],
    mqtt: [],
  });
});

test("upserts and removes reusable profiles by stable id", () => {
  let profiles = upsertConfigurationProfile(
    { wifi: [], mqtt: [] },
    "wifi",
    { id: "home", name: "Home", ssid: "first", password: "one" },
  );
  profiles = upsertConfigurationProfile(profiles, "wifi", {
    id: "home",
    name: "Home updated",
    ssid: "second",
    password: "two",
  });
  assert.equal(profiles.wifi.length, 1);
  assert.equal(profiles.wifi[0].ssid, "second");
  assert.equal(removeConfigurationProfile(profiles, "wifi", "home").wifi.length, 0);
});

test("maps MQTT profiles onto firmware configuration fields", () => {
  assert.deepEqual(
    configurationFieldsForProfile("mqtt", {
      host: "broker.lan",
      port: 1883,
      tls_enabled: false,
      username: "tracker",
      password: "secret",
      base_topic: "farm",
      ca_certificate: "",
    }),
    {
      mqtt_host: "broker.lan",
      mqtt_port: "1883",
      mqtt_tls_enabled: "0",
      mqtt_username: "tracker",
      mqtt_password: "secret",
      mqtt_base_topic: "farm",
      mqtt_ca_certificate: "",
    },
  );
});

test("uses one MQTT profile for the gateway and application transports", () => {
  const profile = {
    host: "broker.lan",
    port: 1883,
    tls_enabled: false,
    username: "tracker",
    password: "secret",
    base_topic: "farm",
  };
  assert.equal(mqttWebSocketUrlForProfile(profile), "ws://broker.lan:1884");
  assert.deepEqual(applicationFieldsForMqttProfile(profile), {
    brokerUrl: "ws://broker.lan:1884",
    baseTopic: "farm",
    username: "tracker",
    password: "secret",
  });
  assert.equal(
    mqttWebSocketUrlForProfile({ ...profile, tls_enabled: true }),
    "wss://broker.lan:8884",
  );
  assert.equal(
    mqttWebSocketUrlForProfile({
      ...profile,
      websocket_url: "wss://mqtt.example.test/custom",
    }),
    "wss://mqtt.example.test/custom",
  );
});

test("rejects non-WebSocket app URLs in MQTT profiles", () => {
  assert.throws(
    () =>
      upsertConfigurationProfile({ wifi: [], mqtt: [] }, "mqtt", {
        id: "broker",
        name: "Broker",
        host: "broker.lan",
        websocket_url: "https://broker.lan/mqtt",
      }),
    /must use ws:\/\//,
  );
});

test("exposes only connection profiles implemented by each firmware role", () => {
  assert.deepEqual(profileKindsForRole("tracker"), {
    wifi: true,
    mqtt: false,
  });
  assert.deepEqual(profileKindsForRole("gateway"), {
    wifi: true,
    mqtt: true,
  });
  assert.deepEqual(profileKindsForRole("repeater"), {
    wifi: false,
    mqtt: false,
  });
});
