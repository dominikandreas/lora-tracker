#pragma once
// Optional factory network seeds. Leave SSID empty to skip station Wi-Fi.
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
