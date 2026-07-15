#pragma once
// Factory provisioning seeds only. Onboarding stores replacements in NVS.
const char* ssid = "";
const char* password = "";
// Use a unique password per device (12+ characters).
const char* onboarding_ap_password = "replace-with-a-unique-password";
// SHA-256 of a separate OTA password. Empty disables OTA safely.
const char* ota_password_hash = "";
