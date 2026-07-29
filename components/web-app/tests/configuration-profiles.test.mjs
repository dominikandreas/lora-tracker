import test from "node:test";
import assert from "node:assert/strict";
import {
  configurationFieldsForProfile,
  normalizeConfigurationProfiles,
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
