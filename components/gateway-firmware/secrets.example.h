#pragma once
// Factory provisioning seeds only. Onboarding stores replacements in NVS.
const char* ssid = "";
const char* password = "";
const char* mqtt_server = "broker.local";
const uint16_t mqtt_port = 8883;
const char* mqtt_user = "";
const char* mqtt_pass = "";
// Optional per-device factory seed. Generic images provision this PEM root CA
// through the authenticated runtime API. Never call setInsecure().
const char* mqtt_ca_certificate = "";
// Plain MQTT is blocked unless explicitly enabled for an isolated test network.
const bool allow_insecure_mqtt = false;
// Optional factory seed. Leave empty in generic release images; the gateway
// generates and stores a unique 20-character credential on first boot.
const char* factory_admin_password = "";
// SHA-256 of a separate OTA password. Empty disables OTA safely.
const char* ota_password_hash = "";
