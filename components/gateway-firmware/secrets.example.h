#pragma once
// Factory provisioning seeds only. Onboarding stores replacements in NVS.
const char* ssid = "";
const char* password = "";
const char* mqtt_server = "broker.local";
const uint16_t mqtt_port = 8883;
const char* mqtt_user = "";
const char* mqtt_pass = "";
// PEM-encoded root CA used to verify the MQTT broker. Never call setInsecure().
const char* mqtt_ca_certificate = "";
// Plain MQTT is blocked unless explicitly enabled for an isolated test network.
const bool allow_insecure_mqtt = false;
// Use a unique password per gateway (12+ characters).
const char* onboarding_ap_password = "replace-with-a-unique-password";
// SHA-256 of a separate OTA password. Empty disables OTA safely.
const char* ota_password_hash = "";
