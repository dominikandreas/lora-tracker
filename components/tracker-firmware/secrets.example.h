#pragma once
// Factory provisioning seeds only. Onboarding stores replacements in NVS.
const char* ssid = "";
const char* password = "";
// Optional factory seed. Leave empty in generic release images; the device then
// generates and stores a unique 20-character credential on first boot.
const char* factory_admin_password = "";
// SHA-256 of a separate OTA password. Empty disables OTA safely.
const char* ota_password_hash = "";
