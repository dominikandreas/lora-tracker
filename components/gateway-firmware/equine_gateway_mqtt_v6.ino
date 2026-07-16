#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <SPI.h>
#include <LoRa.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <stdarg.h>
#include <time.h>
#include <WebServer.h>
#include "secrets.h"
#include "equine_protocol.h"
#include "equine_relay.h"
#include "equine_crypto.h"
#include "equine_config.h"
#include "equine_config_api.h"
#include "equine_mqtt_api.h"

// ==========================================
// VERSIONED PERSISTENT GATEWAY CONFIGURATION
// ==========================================
EquineConfig::GatewayConfigV1 gateway_config{};
Preferences configPrefs;
char admin_password[25]{};
String runtime_mqtt_ca_certificate;

#define USER_BTN_PIN 0
bool gateway_onboarding_required = false;
bool gateway_config_mode = false;
bool gateway_ap_active = false;
bool gateway_config_reboot_requested = false;
bool gateway_factory_reset_requested = false;
uint32_t gateway_config_mode_deadline_ms = 0;
String gateway_network_ip = "off";
bool gateway_button_previous = false;
bool gateway_button_hold_handled = false;
uint32_t gateway_button_press_start_ms = 0;


#define GATEWAY_ID                  (gateway_config.gateway_id)
#define GATEWAY_NAME                (gateway_config.gateway_name)
#define GATEWAY_TRACKER_COUNT       (tracker_runtime_count)
#define MQTT_BUFFER_SIZE            ((uint16_t)gateway_config.mqtt_buffer_size)
#define WIFI_RETRY_INTERVAL_MS      ((uint32_t)gateway_config.wifi_retry_interval_ms)
#define MQTT_RETRY_INTERVAL_MS      ((uint32_t)gateway_config.mqtt_retry_interval_ms)
#define DEDUP_SAVE_INTERVAL         ((uint32_t)gateway_config.dedup_save_interval)
#define LORA_FREQ                   ((double)gateway_config.lora.frequency_hz)
#define LORA_TX_POWER_DBM           ((int)gateway_config.lora.tx_power_dbm)
#define LORA_SPREADING_FACTOR       ((int)gateway_config.lora.spreading_factor)
#define LORA_SIGNAL_BANDWIDTH       ((long)gateway_config.lora.bandwidth_hz)
#define LORA_CODING_RATE            ((int)gateway_config.lora.coding_rate_denominator)
#define LORA_PREAMBLE_LENGTH        ((int)gateway_config.lora.preamble_length)
#define LORA_SYNC_WORD              ((int)gateway_config.lora.sync_word)
#define LORA_RELAY_HOP_LIMIT        ((uint8_t)gateway_config.lora.relay_hop_limit)

struct TrackerRuntime {
  const EquineConfig::GatewayTrackerConfigV1* config;
  uint64_t device_id_hash;
  char hash_text[17];
  char state_topic[96];
  char point_event_topic[112];
  char availability_topic[96];
  char dedup_namespace[16];

  bool has_processed_point;
  uint32_t last_processed_boot_id;
  uint32_t last_processed_seq;
  uint32_t unsaved_dedup_updates;

  bool has_been_seen;
  uint32_t last_seen_ms;
  int16_t last_rssi;
};

TrackerRuntime tracker_runtime[EquineConfig::MAX_GATEWAY_TRACKERS];
size_t tracker_runtime_count = 0;
char gateway_hash_text[17];
char gateway_availability_topic[96];
char gateway_status_topic[112];
char gateway_command_topic[112];
char gateway_archive_ack_topic[112];
char gateway_response_prefix[112];
char ota_hostname[64];

// --- Heltec V2 Internal LoRa Pins ---
#define SCK   5
#define MISO  19
#define MOSI  27
#define SS    18
#define RST   14
#define DIO0  26

const int32_t DELTA_UNIT_MICRODEG = 10;
const uint32_t LOG_HEARTBEAT_INTERVAL_MS = 30000;
const uint8_t LOG_HISTORY_LINES = 25;
const size_t LOG_HISTORY_LINE_LENGTH = 160;

// ==========================================
// VERSIONED PAYLOAD FORMAT
// ==========================================
using AckPayload = EquineProtocol::AckPayloadV1;
using SecureFrameHeader = EquineProtocol::SecureFrameHeaderV2;
using HistoryPayload = EquineProtocol::HistoryPayloadV2;
using AnchorPoint = EquineProtocol::AnchorPointV1;

struct DecodedHistoryHeader {
  uint8_t transport_version;
  uint8_t schema_version;
  uint64_t device_id_hash;
  uint32_t boot_id;
  uint32_t first_seq;
  uint32_t root_unix_time_s;
  bool timestamps_present;
  uint16_t total_dist_dam;
  uint8_t batt_pct;
};

WiFiClient plainMqttClient;
WiFiClientSecure secureMqttClient;
PubSubClient client;
#ifdef LORA_TRACKER_ENABLE_INSECURE_TELNET
constexpr bool REMOTE_LOGGING_ENABLED = true;
#else
constexpr bool REMOTE_LOGGING_ENABLED = false;
#endif
WiFiServer telnetServer(23);
WiFiClient telnetClient;
WebServer webServer(80);
char logHistory[LOG_HISTORY_LINES][LOG_HISTORY_LINE_LENGTH];
uint8_t logHistoryHead = 0;
uint8_t logHistoryCount = 0;
unsigned long lastHeartbeatMs = 0;
unsigned long lastLoRaPacketMs = 0;
unsigned long lastWiFiReconnectAttemptMs = 0;
unsigned long lastMqttReconnectAttemptMs = 0;
unsigned long lastGatewayStatusPublishMs = 0;
uint64_t gateway_tx_nonce_prefix = 0;
double gateway_airtime_tokens_ms = 0.0;
uint32_t gateway_airtime_refill_ms = 0;
uint32_t gateway_airtime_deferrals = 0;
bool ntp_sync_started = false;
wl_status_t lastWiFiStatus = WL_IDLE_STATUS;

constexpr uint32_t GATEWAY_STATUS_INTERVAL_MS = 60000;
constexpr size_t MQTT_COMMAND_PAYLOAD_SIZE = 512;
constexpr size_t MAX_PENDING_ARCHIVE_ACKS = 80;
constexpr size_t POINT_ID_SIZE = 64;
constexpr uint32_t ARCHIVE_ACK_TIMEOUT_MS = 5000;
char pending_archive_point_ids[MAX_PENDING_ARCHIVE_ACKS][POINT_ID_SIZE]{};
bool pending_archive_confirmed[MAX_PENDING_ARCHIVE_ACKS]{};
size_t pending_archive_count = 0;

void resetPendingArchiveConfirmations() {
  pending_archive_count = 0;
  memset(pending_archive_point_ids, 0, sizeof(pending_archive_point_ids));
  memset(pending_archive_confirmed, 0, sizeof(pending_archive_confirmed));
}

bool addPendingArchiveConfirmation(const char* point_id) {
  if (!point_id || pending_archive_count >= MAX_PENDING_ARCHIVE_ACKS) {
    return false;
  }
  strlcpy(pending_archive_point_ids[pending_archive_count], point_id,
          POINT_ID_SIZE);
  pending_archive_confirmed[pending_archive_count] = false;
  pending_archive_count++;
  return true;
}

bool allArchiveConfirmationsReceived() {
  for (size_t index = 0; index < pending_archive_count; index++) {
    if (!pending_archive_confirmed[index]) return false;
  }
  return true;
}

void refillGatewayAirtime() {
  const uint32_t now = millis();
  if (gateway_airtime_refill_ms == 0) {
    gateway_airtime_refill_ms = now;
    return;
  }
  const uint32_t capacity_ms = EquineRelay::maxFrameAirtimeMs(
    gateway_config.lora.spreading_factor,
    gateway_config.lora.bandwidth_hz,
    gateway_config.lora.coding_rate_denominator,
    gateway_config.lora.preamble_length);
  gateway_airtime_tokens_ms = EquineRelay::refillRollingHourAirtimeTokens(
    gateway_airtime_tokens_ms,
    now - gateway_airtime_refill_ms,
    EquineConfig::GERMANY_AIRTIME_BUDGET_MS_PER_HOUR,
    capacity_ms);
  gateway_airtime_refill_ms = now;
}

bool reserveGatewayAirtime(size_t packet_size, uint32_t& estimated_ms) {
  estimated_ms = EquineRelay::estimateAirtimeMs(
    packet_size,
    gateway_config.lora.spreading_factor,
    gateway_config.lora.bandwidth_hz,
    gateway_config.lora.coding_rate_denominator,
    gateway_config.lora.preamble_length);
  refillGatewayAirtime();
  return EquineRelay::consumeAirtimeTokens(
    gateway_airtime_tokens_ms, estimated_ms);
}

bool isValidSha256Hex(const char* value) {
  if (!value || strlen(value) != 64) return false;
  for (size_t i = 0; i < 64; i++) {
    const char c = value[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
          (c >= 'A' && c <= 'F'))) return false;
  }
  return true;
}

// ==========================================
// HELPERS
// ==========================================
void escapeJsonText(const char* input, char* output, size_t output_size) {
  if (!output || output_size == 0) return;
  size_t used = 0;
  output[0] = '\0';
  EquineMqttApi::appendJsonEscaped(output, output_size, used, input);
}

void rememberLogLine(const char* message) {
  if (!message || !message[0]) {
    return;
  }

  size_t start = 0;
  const size_t messageLength = strlen(message);

  while (start < messageLength) {
    size_t end = start;

    while (end < messageLength && message[end] != '\n' && message[end] != '\r') {
      end++;
    }

    if (end > start) {
      size_t lineLength = end - start;
      if (lineLength >= LOG_HISTORY_LINE_LENGTH) {
        lineLength = LOG_HISTORY_LINE_LENGTH - 1;
      }

      memcpy(logHistory[logHistoryHead], message + start, lineLength);
      logHistory[logHistoryHead][lineLength] = '\0';

      logHistoryHead = (logHistoryHead + 1) % LOG_HISTORY_LINES;
      if (logHistoryCount < LOG_HISTORY_LINES) {
        logHistoryCount++;
      }
    }

    while (end < messageLength && (message[end] == '\n' || message[end] == '\r')) {
      end++;
    }

    start = end;
  }
}

void replayRecentLogsToClient(WiFiClient& clientRef) {
  if (logHistoryCount == 0) {
    clientRef.println("No buffered logs yet.");
    return;
  }

  clientRef.println("Recent logs:");

  const uint8_t oldestIndex = (logHistoryHead + LOG_HISTORY_LINES - logHistoryCount) % LOG_HISTORY_LINES;
  for (uint8_t i = 0; i < logHistoryCount; i++) {
    const uint8_t index = (oldestIndex + i) % LOG_HISTORY_LINES;
    clientRef.println(logHistory[index]);
  }
}

void logPrint(const char* message) {
  Serial.print(message);
  rememberLogLine(message);

  if (telnetClient && telnetClient.connected()) {
    String telnetS = message;
    telnetS.replace("\r\n", "\n");
    telnetS.replace("\n", "\r\n");
    telnetClient.print(telnetS);
  }
}

void logPrintln(const char* message) {
  Serial.println(message);
  rememberLogLine(message);

  if (telnetClient && telnetClient.connected()) {
    String telnetS = message;
    telnetS += "\n";
    telnetS.replace("\r\n", "\n");
    telnetS.replace("\n", "\r\n");
    telnetClient.print(telnetS);
  }
}

void logPrintf(const char* format, ...) {
  char buffer[384];

  va_list args;
  va_start(args, format);
  const int written = vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  if (written <= 0) {
    return;
  }

  Serial.print(buffer);
  rememberLogLine(buffer);

  if (telnetClient && telnetClient.connected()) {
    String telnetS = buffer;
    telnetS.replace("\r\n", "\n");
    telnetS.replace("\n", "\r\n");
    telnetClient.print(telnetS);
  }
}

bool publishRetainedMessage(const char* topic, const char* payload) {
  const bool published = client.publish(topic, payload, true);

  if (!published) {
    logPrintf("Failed to publish MQTT message to %s (payload length: %u)\n",
              topic, strlen(payload));
  }

  return published;
}


// ==========================================
// CONFIGURATION STORAGE
// ==========================================
void initializeAdminCredential() {
  Preferences credentials;
  if (!credentials.begin("ltcred", false)) return;
  const bool factory_valid = factory_admin_password &&
    strlen(factory_admin_password) >= 12 &&
    strlen(factory_admin_password) < sizeof(admin_password);
  String stored = credentials.getString("admin", "");
  if (stored.length() >= 12 && stored.length() < sizeof(admin_password)) {
    strlcpy(admin_password, stored.c_str(), sizeof(admin_password));
  } else if (factory_valid) {
    strlcpy(admin_password, factory_admin_password, sizeof(admin_password));
    credentials.putString("admin", admin_password);
  } else {
    static const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    for (size_t i = 0; i < 20; i++) {
      admin_password[i] = alphabet[esp_random() % (sizeof(alphabet) - 1)];
    }
    admin_password[20] = '\0';
    credentials.putString("admin", admin_password);
  }
  credentials.end();
}

void clearAdminCredential() {
  Preferences credentials;
  if (credentials.begin("ltcred", false)) {
    credentials.clear();
    credentials.end();
  }
  memset(admin_password, 0, sizeof(admin_password));
}

constexpr size_t MAX_MQTT_CA_CERTIFICATE_SIZE = 4096;

bool isValidMqttCaCertificate(const String& certificate, bool allow_empty) {
  if (certificate.isEmpty()) return allow_empty;
  return certificate.length() <= MAX_MQTT_CA_CERTIFICATE_SIZE &&
    certificate.indexOf("-----BEGIN CERTIFICATE-----") >= 0 &&
    certificate.indexOf("-----END CERTIFICATE-----") >= 0;
}

void initializeMqttCaCertificate() {
  Preferences trust;
  if (!trust.begin("lttrust", false)) return;
  String stored = trust.getString("mqtt_ca", "");
  if (stored.isEmpty() && mqtt_ca_certificate && mqtt_ca_certificate[0]) {
    stored = mqtt_ca_certificate;
    trust.putString("mqtt_ca", stored);
  }
  if (isValidMqttCaCertificate(stored, true)) {
    runtime_mqtt_ca_certificate = stored;
  }
  trust.end();
}

bool readMqttCaCertificateBackup(String& certificate) {
  Preferences trust;
  if (!trust.begin("lttrust", true)) return false;
  const bool present = trust.isKey("mqtt_ca_bak");
  if (present) certificate = trust.getString("mqtt_ca_bak", "");
  trust.end();
  return present && isValidMqttCaCertificate(certificate, true);
}

bool writeMqttCaCertificate(
    const String& certificate,
    bool update_backup) {
  if (!isValidMqttCaCertificate(certificate, true)) return false;
  Preferences trust;
  if (!trust.begin("lttrust", false)) return false;
  bool ok = true;
  if (update_backup) {
    ok = trust.putString("mqtt_ca_bak", runtime_mqtt_ca_certificate) > 0;
  }
  if (ok) ok = trust.putString("mqtt_ca", certificate) > 0;
  trust.end();
  if (ok) runtime_mqtt_ca_certificate = certificate;
  return ok;
}

void clearMqttCaCertificateStorage() {
  Preferences trust;
  if (trust.begin("lttrust", false)) {
    trust.clear();
    trust.end();
  }
  runtime_mqtt_ca_certificate = "";
}

bool readGatewayConfigBlob(const char* key,
                           EquineConfig::GatewayConfigV1& output) {
  if (!configPrefs.begin(EquineConfig::CONFIG_NAMESPACE, true)) return false;
  const size_t length = configPrefs.getBytesLength(key);
  const bool size_ok = length == sizeof(output);
  const size_t read = size_ok
    ? configPrefs.getBytes(key, &output, sizeof(output))
    : 0;
  configPrefs.end();
  return size_ok && read == sizeof(output) &&
         EquineConfig::validateGatewayConfig(output);
}

bool writeGatewayConfigBlob(const char* key,
                            const EquineConfig::GatewayConfigV1& value) {
  if (!configPrefs.begin(EquineConfig::CONFIG_NAMESPACE, false)) return false;
  const size_t written = configPrefs.putBytes(key, &value, sizeof(value));
  configPrefs.end();
  return written == sizeof(value);
}

void makeGatewayFactoryConfig() {
  const uint32_t chip_suffix =
    static_cast<uint32_t>(ESP.getEfuseMac() & 0x00FFFFFFULL);
  char factory_id[EquineConfig::DEVICE_ID_SIZE];
  char factory_name[EquineConfig::DEVICE_NAME_SIZE];
  snprintf(factory_id, sizeof(factory_id), "gateway-%06lx",
           static_cast<unsigned long>(chip_suffix));
  snprintf(factory_name, sizeof(factory_name), "Gateway %06lX",
           static_cast<unsigned long>(chip_suffix));
  EquineConfig::makeDefaultGatewayConfig(
    gateway_config,
    factory_id,
    factory_name,
    ssid,
    password,
    mqtt_server,
    mqtt_port,
    mqtt_user,
    mqtt_pass);

  // Start with an empty allowlist; onboarding must register every tracker.
  gateway_config.tracker_count = 0;
  EquineConfig::finalize(
    gateway_config, EquineConfig::DeviceRole::GATEWAY, 1);
}

bool saveGatewayConfig(bool increment_revision = true) {
  EquineConfig::GatewayConfigV1 previous{};
  if (readGatewayConfigBlob(EquineConfig::ACTIVE_CONFIG_KEY, previous)) {
    if (!writeGatewayConfigBlob(EquineConfig::BACKUP_CONFIG_KEY, previous)) {
      logPrintln("Warning: failed to update gateway configuration backup.");
    }
  }

  uint32_t revision = gateway_config.header.revision;
  if (revision == 0) revision = 1;
  else if (increment_revision && revision < UINT32_MAX) revision++;
  EquineConfig::finalize(
    gateway_config, EquineConfig::DeviceRole::GATEWAY, revision);

  if (!EquineConfig::validateGatewayConfig(gateway_config)) {
    logPrintln("Refusing to save invalid gateway configuration.");
    return false;
  }
  if (!writeGatewayConfigBlob(
        EquineConfig::ACTIVE_CONFIG_KEY, gateway_config)) {
    logPrintln("Failed to save gateway configuration.");
    return false;
  }

  logPrintf("Saved gateway config schema=%u revision=%lu id=%s trackers=%u.\n",
            gateway_config.header.schema_version,
            (unsigned long)gateway_config.header.revision,
            gateway_config.gateway_id,
            gateway_config.tracker_count);
  return true;
}


bool readGatewayProvisionedFlag(bool& provisioned) {
  provisioned = false;
  if (!configPrefs.begin(EquineConfig::CONFIG_NAMESPACE, true)) return false;
  const bool present = configPrefs.isKey(EquineConfigApi::PROVISIONED_KEY);
  if (present) {
    provisioned = configPrefs.getBool(EquineConfigApi::PROVISIONED_KEY, false);
  }
  configPrefs.end();
  return present;
}

bool writeGatewayProvisionedFlag(bool provisioned) {
  if (!configPrefs.begin(EquineConfig::CONFIG_NAMESPACE, false)) return false;
  const size_t written = configPrefs.putBool(
    EquineConfigApi::PROVISIONED_KEY, provisioned);
  configPrefs.end();
  return written == sizeof(uint8_t);
}

bool loadGatewayConfig() {
  const char* source = "active";
  if (!readGatewayConfigBlob(
        EquineConfig::ACTIVE_CONFIG_KEY, gateway_config)) {
    source = "backup";
    if (!readGatewayConfigBlob(
          EquineConfig::BACKUP_CONFIG_KEY, gateway_config)) {
      source = "factory";
      makeGatewayFactoryConfig();
      if (!saveGatewayConfig(false) || !writeGatewayProvisionedFlag(false)) {
        return false;
      }
    } else {
      writeGatewayConfigBlob(EquineConfig::ACTIVE_CONFIG_KEY, gateway_config);
    }
  }

  bool provisioned = false;
  const bool has_provisioned_marker = readGatewayProvisionedFlag(provisioned);
  gateway_onboarding_required = !has_provisioned_marker || !provisioned;
  logPrintf("Loaded gateway config from %s: schema=%u revision=%lu id=%s trackers=%u onboarding=%s.\n",
            source,
            gateway_config.header.schema_version,
            (unsigned long)gateway_config.header.revision,
            gateway_config.gateway_id,
            gateway_config.tracker_count,
            gateway_onboarding_required ? "required" : "complete");
  return true;
}

void clearGatewayConfigStorage() {
  if (configPrefs.begin(EquineConfig::CONFIG_NAMESPACE, false)) {
    configPrefs.clear();
    configPrefs.end();
  }
  clearAdminCredential();
  clearMqttCaCertificateStorage();
}


bool commitGatewayConfigCandidate(
    EquineConfig::GatewayConfigV1 candidate,
    bool mark_provisioned,
    char* error,
    size_t error_size,
    const String* mqtt_ca_candidate = nullptr) {
  const uint32_t next_revision =
    gateway_config.header.revision < UINT32_MAX
      ? gateway_config.header.revision + 1
      : gateway_config.header.revision;
  EquineConfig::finalize(
    candidate, EquineConfig::DeviceRole::GATEWAY, next_revision);
  if (!EquineConfig::validateGatewayConfig(candidate)) {
    snprintf(error, error_size, "candidate violates cross-field validation");
    return false;
  }
  const String previous_ca = runtime_mqtt_ca_certificate;
  const String& next_ca = mqtt_ca_candidate
    ? *mqtt_ca_candidate : runtime_mqtt_ca_certificate;
  if (candidate.mqtt_tls_enabled && !isValidMqttCaCertificate(next_ca, false)) {
    snprintf(error, error_size, "TLS requires a valid PEM root CA certificate");
    return false;
  }
  if (!candidate.mqtt_tls_enabled && !allow_insecure_mqtt) {
    snprintf(error, error_size, "plaintext MQTT is disabled by this firmware");
    return false;
  }
  if (!writeMqttCaCertificate(next_ca, true)) {
    snprintf(error, error_size, "failed to write MQTT CA certificate");
    return false;
  }
  const EquineConfig::GatewayConfigV1 previous = gateway_config;
  gateway_config = candidate;
  if (!saveGatewayConfig(false)) {
    gateway_config = previous;
    writeMqttCaCertificate(previous_ca, false);
    snprintf(error, error_size, "failed to write active configuration");
    return false;
  }
  if (mark_provisioned) {
    if (!writeGatewayProvisionedFlag(true)) {
      snprintf(error, error_size, "failed to persist onboarding completion");
      gateway_onboarding_required = true;
      return false;
    }
    gateway_onboarding_required = false;
  }
  return true;
}

bool rollbackGatewayConfig(char* error, size_t error_size) {
  EquineConfig::GatewayConfigV1 backup{};
  if (!readGatewayConfigBlob(EquineConfig::BACKUP_CONFIG_KEY, backup)) {
    snprintf(error, error_size, "no valid rollback configuration");
    return false;
  }
  String backup_ca;
  if (!readMqttCaCertificateBackup(backup_ca)) {
    snprintf(error, error_size, "no valid MQTT CA rollback value");
    return false;
  }
  return commitGatewayConfigCandidate(
    backup, true, error, error_size, &backup_ca);
}

String gatewayConfigJson() {
  using EquineConfigApi::appendJsonEscaped;
  String out;
  out.reserve(5200);
  out = "{\"api_version\":" + String(EquineConfigApi::API_VERSION) +
        ",\"role\":\"gateway\",\"config_schema\":" +
        String(gateway_config.header.schema_version) +
        ",\"revision\":" + String(gateway_config.header.revision) +
        ",\"onboarding_required\":" +
        String(gateway_onboarding_required ? "true" : "false") +
        ",\"config_mode\":" +
        String(gateway_config_mode ? "true" : "false") +
        ",\"gateway_id\":\"";
  appendJsonEscaped(out, gateway_config.gateway_id);
  out += "\",\"gateway_name\":\"";
  appendJsonEscaped(out, gateway_config.gateway_name);
  out += "\",\"gateway_hash\":\"" + String(gateway_hash_text) +
         "\",\"wifi_ssid\":\"";
  appendJsonEscaped(out, gateway_config.wifi_ssid);
  out += "\",\"wifi_password_set\":" +
         String(gateway_config.wifi_password[0] ? "true" : "false") +
         ",\"network_ip\":\"" + gateway_network_ip +
         "\",\"ap_active\":" + String(gateway_ap_active ? "true" : "false") +
         ",\"mqtt\":{";
  out += "\"host\":\"";
  appendJsonEscaped(out, gateway_config.mqtt_host);
  out += "\",\"port\":" + String(gateway_config.mqtt_port) +
         ",\"tls_enabled\":" +
         String(gateway_config.mqtt_tls_enabled ? "true" : "false") +
         ",\"username\":\"";
  appendJsonEscaped(out, gateway_config.mqtt_username);
  out += "\",\"password_set\":" +
         String(gateway_config.mqtt_password[0] ? "true" : "false") +
         ",\"ca_certificate_set\":" +
         String(runtime_mqtt_ca_certificate.length() ? "true" : "false") +
         ",\"base_topic\":\"";
  appendJsonEscaped(out, gateway_config.mqtt_base_topic);
  out += "\",\"buffer_size\":" + String(gateway_config.mqtt_buffer_size) + "}";
  out += ",\"lora\":{";
  out += "\"frequency_hz\":" + String(gateway_config.lora.frequency_hz) +
         ",\"bandwidth_hz\":" + String(gateway_config.lora.bandwidth_hz) +
         ",\"tx_power_dbm\":" + String(gateway_config.lora.tx_power_dbm) +
         ",\"sf\":" + String(gateway_config.lora.spreading_factor) +
         ",\"coding_rate\":" + String(gateway_config.lora.coding_rate_denominator) +
         ",\"preamble_length\":" + String(gateway_config.lora.preamble_length) +
         ",\"sync_word\":" + String(gateway_config.lora.sync_word) +
         ",\"relay_hop_limit\":" + String(gateway_config.lora.relay_hop_limit) + "}";
  out += ",\"dedup_save_interval\":" + String(gateway_config.dedup_save_interval) +
         ",\"wifi_retry_interval_ms\":" + String(gateway_config.wifi_retry_interval_ms) +
         ",\"mqtt_retry_interval_ms\":" + String(gateway_config.mqtt_retry_interval_ms) +
         ",\"trackers\":[";
  for (uint8_t i = 0; i < gateway_config.tracker_count; i++) {
    if (i) out += ',';
    const auto& tracker = gateway_config.trackers[i];
    char hash[17];
    EquineProtocol::formatDeviceHash(
      EquineProtocol::deviceIdHash(tracker.device_id), hash, sizeof(hash));
    out += "{\"index\":" + String(i) + ",\"id\":\"";
    appendJsonEscaped(out, tracker.device_id);
    out += "\",\"name\":\"";
    appendJsonEscaped(out, tracker.device_name);
    out += "\",\"device_hash\":\"" + String(hash) +
           "\",\"lora_key_set\":" +
           String(EquineConfig::hasProvisionedKey(tracker.lora_aead_key) ? "true" : "false") +
           ",\"enabled\":" + String(tracker.enabled ? "true" : "false") + "}";
  }
  out += "]}";
  return out;
}

bool applyGatewayWebPatch(EquineConfigApi::PatchStatus& status,
                          bool& reboot_requested) {
  EquineConfig::GatewayConfigV1 candidate = gateway_config;
  String mqtt_ca_candidate = runtime_mqtt_ca_certificate;
  uint32_t expected_revision = 0;
  bool has_revision = false;
  const int argument_count = webServer.args();
  for (int i = 0; i < argument_count; i++) {
    const String key_string = webServer.argName(i);
    const String value_string = webServer.arg(i);
    const char* key = key_string.c_str();
    const char* value = value_string.c_str();
    if (strcmp(key, "expected_revision") == 0) {
      has_revision = EquineConfigApi::parseUnsigned(
        value, 1, UINT32_MAX, expected_revision);
      if (!has_revision) {
        EquineConfigApi::setError(status, key, "invalid revision");
        return false;
      }
      continue;
    }
    if (strcmp(key, "reboot") == 0) {
      uint8_t parsed = 0;
      if (!EquineConfigApi::parseBool(value, parsed)) {
        EquineConfigApi::setError(status, key, "invalid boolean");
        return false;
      }
      reboot_requested = parsed != 0;
      continue;
    }
    if (EquineConfigApi::isControlField(key)) continue;
    if (strcmp(key, "mqtt_ca_certificate") == 0) {
      if (strcmp(value, EquineConfigApi::KEEP_SECRET) == 0) continue;
      const String proposed = value;
      if (!isValidMqttCaCertificate(proposed, true)) {
        EquineConfigApi::setError(
          status, key, "expected a PEM certificate no larger than 4096 bytes");
        return false;
      }
      if (mqtt_ca_candidate != proposed) {
        mqtt_ca_candidate = proposed;
        status.changed = true;
        status.reboot_required = true;
      }
      continue;
    }
    const auto result = EquineConfigApi::applyGatewayField(
      candidate, key, value, status);
    if (result == EquineConfigApi::FieldResult::UNKNOWN) {
      EquineConfigApi::setError(status, key, "unknown setting");
      return false;
    }
    if (result == EquineConfigApi::FieldResult::INVALID) return false;
  }
  if (!has_revision) {
    EquineConfigApi::setError(status, "expected_revision", "required");
    return false;
  }
  if (expected_revision != gateway_config.header.revision) {
    snprintf(status.error, sizeof(status.error),
             "revision conflict: current=%lu",
             (unsigned long)gateway_config.header.revision);
    return false;
  }
  if (!status.changed) {
    if (gateway_onboarding_required) {
      if (!writeGatewayProvisionedFlag(true)) {
        snprintf(status.error, sizeof(status.error),
                 "failed to persist onboarding completion");
        return false;
      }
      gateway_onboarding_required = false;
    }
    return true;
  }
  return commitGatewayConfigCandidate(
    candidate, true, status.error, sizeof(status.error), &mqtt_ca_candidate);
}

bool gatewayConfigWritesAllowed() {
  return gateway_onboarding_required || gateway_config_mode;
}


void buildDedupNamespace(uint64_t hash, char* output, size_t output_size) {
  // ESP32 Preferences namespaces are limited to 15 characters. Keep 56 bits
  // of the public hash, prefixed with 'd'. Registry collisions are checked at
  // startup before any state is loaded.
  const uint32_t high = static_cast<uint32_t>(hash >> 32);
  const uint32_t low = static_cast<uint32_t>(hash & 0xFFFFFFFFULL);
  snprintf(output, output_size, "d%06lx%08lx",
           static_cast<unsigned long>(high & 0x00FFFFFFUL),
           static_cast<unsigned long>(low));
}

TrackerRuntime* findTrackerByHash(uint64_t hash) {
  for (size_t i = 0; i < GATEWAY_TRACKER_COUNT; i++) {
    if (tracker_runtime[i].device_id_hash == hash) {
      return &tracker_runtime[i];
    }
  }
  return nullptr;
}

bool initializeTrackerRegistry() {
  tracker_runtime_count = 0;
  memset(tracker_runtime, 0, sizeof(tracker_runtime));

  const uint64_t gateway_hash = EquineProtocol::deviceIdHash(GATEWAY_ID);
  EquineProtocol::formatDeviceHash(
    gateway_hash, gateway_hash_text, sizeof(gateway_hash_text)
  );
  EquineMqttApi::formatGatewayTopic(
    gateway_availability_topic, sizeof(gateway_availability_topic),
    gateway_config.mqtt_base_topic, gateway_hash_text, "availability");
  EquineMqttApi::formatGatewayTopic(
    gateway_status_topic, sizeof(gateway_status_topic),
    gateway_config.mqtt_base_topic, gateway_hash_text, "status");
  EquineMqttApi::formatGatewayTopic(
    gateway_command_topic, sizeof(gateway_command_topic),
    gateway_config.mqtt_base_topic, gateway_hash_text, "commands/request");
  EquineMqttApi::formatGatewayTopic(
    gateway_response_prefix, sizeof(gateway_response_prefix),
    gateway_config.mqtt_base_topic, gateway_hash_text, "commands/response");
  EquineMqttApi::formatGatewayTopic(
    gateway_archive_ack_topic, sizeof(gateway_archive_ack_topic),
    gateway_config.mqtt_base_topic, gateway_hash_text, "archive/ack");
  snprintf(ota_hostname, sizeof(ota_hostname), "lora-gateway-%s", GATEWAY_ID);

  for (uint8_t config_index = 0;
       config_index < gateway_config.tracker_count;
       config_index++) {
    const EquineConfig::GatewayTrackerConfigV1& configured =
      gateway_config.trackers[config_index];
    if (!configured.enabled) continue;
    if (tracker_runtime_count >= EquineConfig::MAX_GATEWAY_TRACKERS) {
      logPrintln("Configuration has more enabled trackers than runtime capacity.");
      return false;
    }

    TrackerRuntime& tracker = tracker_runtime[tracker_runtime_count];
    memset(&tracker, 0, sizeof(tracker));
    tracker.config = &configured;
    tracker.device_id_hash = EquineProtocol::deviceIdHash(tracker.config->device_id);
    tracker.last_rssi = 0;

    EquineProtocol::formatDeviceHash(
      tracker.device_id_hash, tracker.hash_text, sizeof(tracker.hash_text)
    );
    buildDedupNamespace(
      tracker.device_id_hash,
      tracker.dedup_namespace,
      sizeof(tracker.dedup_namespace)
    );

    EquineMqttApi::formatTrackerTopic(
      tracker.state_topic, sizeof(tracker.state_topic),
      gateway_config.mqtt_base_topic, tracker.hash_text, "state");
    EquineMqttApi::formatTrackerTopic(
      tracker.point_event_topic, sizeof(tracker.point_event_topic),
      gateway_config.mqtt_base_topic, tracker.hash_text, "events/point");
    EquineMqttApi::formatTrackerTopic(
      tracker.availability_topic, sizeof(tracker.availability_topic),
      gateway_config.mqtt_base_topic, tracker.hash_text, "availability");

    for (size_t previous = 0; previous < tracker_runtime_count; previous++) {
      if (tracker_runtime[previous].device_id_hash == tracker.device_id_hash) {
        logPrintf("Configuration error: duplicate device hash %s for '%s' and '%s'.\n",
                  tracker.hash_text,
                  tracker_runtime[previous].config->device_id,
                  tracker.config->device_id);
        return false;
      }
      if (strcmp(tracker_runtime[previous].dedup_namespace,
                 tracker.dedup_namespace) == 0) {
        logPrintf("Configuration error: dedup namespace collision for %s.\n",
                  tracker.hash_text);
        return false;
      }
    }
    tracker_runtime_count++;
  }

  return true;
}

void publishGatewayAvailability(const char* payload) {
  publishRetainedMessage(gateway_availability_topic, payload);

  // Per-tracker availability topics remain useful to apps and preserve the
  // protocol-v1 topic layout. Home Assistant entities use the gateway LWT so
  // all registered trackers become unavailable if this gateway disappears.
  for (size_t i = 0; i < GATEWAY_TRACKER_COUNT; i++) {
    TrackerRuntime& tracker = tracker_runtime[i];
    client.publish(tracker.availability_topic, payload, true);
  }
}

bool publishTrackerPoint(TrackerRuntime& tracker, const char* payload) {
  // The non-retained point event is the canonical stream consumed by
  // archivers and apps. It determines whether a LoRa frame may be ACKed.
  // PubSubClient publishes at MQTT QoS 0; point_id makes retries idempotent,
  // while a future transport upgrade can add broker-confirmed QoS 1.
  if (!client.publish(tracker.point_event_topic, payload, false)) {
    logPrintf("Failed to publish point event topic %s.\n",
              tracker.point_event_topic);
    return false;
  }

  // Retained state is a latest-value convenience for dashboards. A failure is
  // logged but does not cause a duplicate point event on the next LoRa retry.
  if (!client.publish(tracker.state_topic, payload, true)) {
    logPrintf("Warning: failed to update retained state topic %s.\n",
              tracker.state_topic);
  }

  return true;
}

bool publishGatewayStatus(bool retained = true) {
  if (!client.connected()) return false;

  size_t seen_count = 0;
  for (size_t i = 0; i < GATEWAY_TRACKER_COUNT; i++) {
    if (tracker_runtime[i].has_been_seen) seen_count++;
  }

  char escaped_gateway_id[EquineConfig::DEVICE_ID_SIZE * 2]{};
  char escaped_gateway_name[EquineConfig::DEVICE_NAME_SIZE * 2]{};
  escapeJsonText(GATEWAY_ID, escaped_gateway_id, sizeof(escaped_gateway_id));
  escapeJsonText(GATEWAY_NAME, escaped_gateway_name, sizeof(escaped_gateway_name));

  char payload[512];
  const int written = snprintf(
    payload, sizeof(payload),
    "{\"api_version\":%u,\"schema_version\":1,"
    "\"gateway_id\":\"%s\",\"gateway_name\":\"%s\","
    "\"gateway_hash\":\"%s\",\"uptime_ms\":%lu,"
    "\"wifi_connected\":%s,\"mqtt_connected\":%s,"
    "\"free_heap\":%u,\"tracker_count\":%u,"
    "\"trackers_seen\":%u}",
    EquineMqttApi::API_VERSION,
    escaped_gateway_id,
    escaped_gateway_name,
    gateway_hash_text,
    (unsigned long)millis(),
    WiFi.status() == WL_CONNECTED ? "true" : "false",
    client.connected() ? "true" : "false",
    (unsigned)ESP.getFreeHeap(),
    (unsigned)GATEWAY_TRACKER_COUNT,
    (unsigned)seen_count);
  if (written <= 0 || written >= (int)sizeof(payload)) return false;
  const bool ok = client.publish(gateway_status_topic, payload, retained);
  if (ok) lastGatewayStatusPublishMs = millis();
  return ok;
}

bool publishCommandResponse(const char* request_id, const char* body) {
  if (!EquineMqttApi::isSafeRequestId(request_id) || !body) return false;
  char topic[176];
  snprintf(topic, sizeof(topic), "%s/%s", gateway_response_prefix, request_id);
  return client.publish(topic, body, false);
}

void publishCommandError(const char* request_id, const char* command,
                         const char* error_code, const char* message) {
  char payload[512];
  const int written = snprintf(
    payload, sizeof(payload),
    "{\"api_version\":%u,\"schema_version\":%u,"
    "\"request_id\":\"%s\",\"command\":\"%s\","
    "\"ok\":false,\"error\":\"%s\","
    "\"message\":\"%s\"}",
    EquineMqttApi::API_VERSION,
    EquineMqttApi::COMMAND_SCHEMA_VERSION,
    request_id ? request_id : "invalid",
    command ? command : "unknown",
    error_code ? error_code : "invalid_request",
    message ? message : "Invalid request");
  if (written > 0 && written < (int)sizeof(payload) && request_id &&
      EquineMqttApi::isSafeRequestId(request_id)) {
    publishCommandResponse(request_id, payload);
  }
}

void publishRegistryResponses(const char* request_id) {
  if (GATEWAY_TRACKER_COUNT == 0) {
    char payload[320];
    snprintf(payload, sizeof(payload),
      "{\"api_version\":%u,\"schema_version\":%u,"
      "\"request_id\":\"%s\",\"command\":\"registry.get\","
      "\"ok\":true,\"chunk_index\":0,\"final\":true,"
      "\"trackers\":[]}",
      EquineMqttApi::API_VERSION, EquineMqttApi::COMMAND_SCHEMA_VERSION,
      request_id);
    publishCommandResponse(request_id, payload);
    return;
  }

  for (size_t i = 0; i < GATEWAY_TRACKER_COUNT; i++) {
    const TrackerRuntime& tracker = tracker_runtime[i];
    const uint32_t last_seen_age_s = tracker.has_been_seen
      ? (millis() - tracker.last_seen_ms) / 1000UL
      : 0;
    char escaped_device_id[EquineConfig::DEVICE_ID_SIZE * 2]{};
    char escaped_device_name[EquineConfig::DEVICE_NAME_SIZE * 2]{};
    escapeJsonText(tracker.config->device_id, escaped_device_id, sizeof(escaped_device_id));
    escapeJsonText(tracker.config->device_name, escaped_device_name, sizeof(escaped_device_name));
    char payload[512];
    const int written = snprintf(
      payload, sizeof(payload),
      "{\"api_version\":%u,\"schema_version\":%u,"
      "\"request_id\":\"%s\",\"command\":\"registry.get\","
      "\"ok\":true,\"chunk_index\":%u,\"final\":%s,"
      "\"tracker\":{\"device_id\":\"%s\","
      "\"device_name\":\"%s\",\"device_hash\":\"%s\","
      "\"seen\":%s,\"last_seen_age_s\":%lu,\"rssi\":%d,"
      "\"last_boot_id\":%lu,\"last_seq\":%lu}}",
      EquineMqttApi::API_VERSION,
      EquineMqttApi::COMMAND_SCHEMA_VERSION,
      request_id,
      (unsigned)i,
      i + 1 == GATEWAY_TRACKER_COUNT ? "true" : "false",
      escaped_device_id,
      escaped_device_name,
      tracker.hash_text,
      tracker.has_been_seen ? "true" : "false",
      (unsigned long)last_seen_age_s,
      tracker.last_rssi,
      (unsigned long)tracker.last_processed_boot_id,
      (unsigned long)tracker.last_processed_seq);
    if (written > 0 && written < (int)sizeof(payload)) {
      publishCommandResponse(request_id, payload);
    }
  }
}

void mqttMessageCallback(char* topic, byte* payload_bytes, unsigned int length) {
  if (!topic) return;
  if (strcmp(topic, gateway_archive_ack_topic) == 0) {
    if (!payload_bytes || length == 0 || length >= POINT_ID_SIZE) return;
    char point_id[POINT_ID_SIZE]{};
    memcpy(point_id, payload_bytes, length);
    for (size_t index = 0; index < pending_archive_count; index++) {
      if (strcmp(point_id, pending_archive_point_ids[index]) == 0) {
        pending_archive_confirmed[index] = true;
        break;
      }
    }
    return;
  }
  if (strcmp(topic, gateway_command_topic) != 0) return;
  if (!payload_bytes || length == 0 || length >= MQTT_COMMAND_PAYLOAD_SIZE) {
    logPrintln("Rejected oversized or empty MQTT command.");
    return;
  }

  char payload[MQTT_COMMAND_PAYLOAD_SIZE];
  memcpy(payload, payload_bytes, length);
  payload[length] = '\0';

  char request_id[EquineMqttApi::MAX_REQUEST_ID_LENGTH + 1]{};
  char command[EquineMqttApi::MAX_COMMAND_LENGTH + 1]{};
  uint32_t api_version = 0;
  uint32_t schema_version = 0;
  if (!EquineMqttApi::jsonGetString(payload, "request_id", request_id,
                                    sizeof(request_id)) ||
      !EquineMqttApi::isSafeRequestId(request_id)) {
    logPrintln("Rejected MQTT command with missing/invalid request_id.");
    return;
  }
  if (!EquineMqttApi::jsonGetString(payload, "command", command,
                                    sizeof(command))) {
    publishCommandError(request_id, "unknown", "invalid_request",
                        "Missing command");
    return;
  }
  if (!EquineMqttApi::jsonGetUnsigned(payload, "api_version", api_version) ||
      api_version != EquineMqttApi::API_VERSION ||
      !EquineMqttApi::jsonGetUnsigned(payload, "schema_version", schema_version) ||
      schema_version != EquineMqttApi::COMMAND_SCHEMA_VERSION) {
    publishCommandError(request_id, command, "unsupported_version",
                        "Unsupported API or command schema version");
    return;
  }

  logPrintf("MQTT command received: request=%s command=%s\n",
            request_id, command);

  if (strcmp(command, "ping") == 0) {
    char response[320];
    snprintf(response, sizeof(response),
      "{\"api_version\":%u,\"schema_version\":%u,"
      "\"request_id\":\"%s\",\"command\":\"ping\","
      "\"ok\":true,\"gateway_hash\":\"%s\","
      "\"uptime_ms\":%lu}",
      EquineMqttApi::API_VERSION, EquineMqttApi::COMMAND_SCHEMA_VERSION,
      request_id, gateway_hash_text, (unsigned long)millis());
    publishCommandResponse(request_id, response);
  } else if (strcmp(command, "status.get") == 0) {
    publishGatewayStatus(true);
    char response[384];
    snprintf(response, sizeof(response),
      "{\"api_version\":%u,\"schema_version\":%u,"
      "\"request_id\":\"%s\",\"command\":\"status.get\","
      "\"ok\":true,\"status_topic\":\"%s\"}",
      EquineMqttApi::API_VERSION, EquineMqttApi::COMMAND_SCHEMA_VERSION,
      request_id, gateway_status_topic);
    publishCommandResponse(request_id, response);
  } else if (strcmp(command, "registry.get") == 0) {
    publishRegistryResponses(request_id);
  } else {
    publishCommandError(request_id, command, "unsupported_command",
                        "Gateway command is not supported");
  }
}

bool subscribeMqttTopics() {
  if (!client.subscribe(gateway_command_topic, 1)) {
    logPrintf("Failed to subscribe to gateway command topic %s.\n",
              gateway_command_topic);
    return false;
  }
  if (!client.subscribe(gateway_archive_ack_topic, 1)) {
    logPrintf("Failed to subscribe to archive confirmation topic %s.\n",
              gateway_archive_ack_topic);
    return false;
  }
  logPrintf("Subscribed to MQTT commands and archive confirmations: %s, %s\n",
            gateway_command_topic, gateway_archive_ack_topic);
  return true;
}

bool isValidCoordinateMicrodeg(int32_t lat, int32_t lon) {
  return lat >= -90000000 && lat <= 90000000 &&
         lon >= -180000000 && lon <= 180000000;
}

const char* wifiStatusToString(wl_status_t status) {
  switch (status) {
    case WL_CONNECTED:
      return "connected";
    case WL_NO_SSID_AVAIL:
      return "no_ssid";
    case WL_CONNECT_FAILED:
      return "connect_failed";
    case WL_CONNECTION_LOST:
      return "connection_lost";
    case WL_DISCONNECTED:
      return "disconnected";
    case WL_IDLE_STATUS:
      return "idle";
    default:
      return "unknown";
  }
}

bool isNewPoint(const TrackerRuntime& tracker, uint32_t boot_id, uint32_t seq) {
  if (!tracker.has_processed_point) {
    return true;
  }

  if (boot_id != tracker.last_processed_boot_id) {
    return boot_id > tracker.last_processed_boot_id;
  }

  return seq > tracker.last_processed_seq;
}

void loadDedupState(TrackerRuntime& tracker) {
  Preferences dedupPrefs;
  if (!dedupPrefs.begin(tracker.dedup_namespace, false)) {
    logPrintf("Failed to open dedup namespace %s for %s.\n",
              tracker.dedup_namespace, tracker.config->device_id);
    return;
  }

  tracker.has_processed_point = dedupPrefs.getBool("has", false);
  tracker.last_processed_boot_id = dedupPrefs.getULong("boot", 0);
  tracker.last_processed_seq = dedupPrefs.getULong("seq", 0);
  tracker.unsaved_dedup_updates = 0;
  dedupPrefs.end();

  logPrintf("Loaded dedup [%s/%s] -> has=%s boot_id=%lu seq=%lu\n",
            tracker.config->device_id,
            tracker.hash_text,
            tracker.has_processed_point ? "true" : "false",
            (unsigned long)tracker.last_processed_boot_id,
            (unsigned long)tracker.last_processed_seq);
}

void loadAllDedupStates() {
  for (size_t i = 0; i < GATEWAY_TRACKER_COUNT; i++) {
    loadDedupState(tracker_runtime[i]);
  }
}

void saveDedupState(TrackerRuntime& tracker, bool force = false) {
  if (!tracker.has_processed_point) return;
  if (!force && tracker.unsaved_dedup_updates < DEDUP_SAVE_INTERVAL) return;

  Preferences dedupPrefs;
  if (!dedupPrefs.begin(tracker.dedup_namespace, false)) {
    logPrintf("Failed to open dedup namespace %s for saving.\n",
              tracker.dedup_namespace);
    return;
  }

  const bool ok =
    dedupPrefs.putBool("has", tracker.has_processed_point) > 0 &&
    dedupPrefs.putULong("boot", tracker.last_processed_boot_id) > 0 &&
    dedupPrefs.putULong("seq", tracker.last_processed_seq) > 0;
  dedupPrefs.end();

  if (!ok) {
    logPrintf("Failed to save dedup state for %s.\n", tracker.config->device_id);
    return;
  }

  tracker.unsaved_dedup_updates = 0;
  logPrintf("Saved dedup [%s] -> boot_id=%lu seq=%lu\n",
            tracker.config->device_id,
            (unsigned long)tracker.last_processed_boot_id,
            (unsigned long)tracker.last_processed_seq);
}

void saveAllDedupStates(bool force = false) {
  for (size_t i = 0; i < GATEWAY_TRACKER_COUNT; i++) {
    saveDedupState(tracker_runtime[i], force);
  }
}

void updateDedupState(TrackerRuntime& tracker, uint32_t boot_id, uint32_t seq) {
  tracker.last_processed_boot_id = boot_id;
  tracker.last_processed_seq = seq;
  tracker.has_processed_point = true;
  tracker.unsaved_dedup_updates++;
}

void setupOTA() {
  ArduinoOTA.setHostname(ota_hostname);
  if (!isValidSha256Hex(ota_password_hash)) {
    logPrintln("OTA disabled: configure a 64-character SHA-256 password hash.");
    return;
  }
  ArduinoOTA.setPasswordHash(ota_password_hash);

  ArduinoOTA
    .onStart([]() {
      String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
      logPrintln((String("OTA Start updating ") + type).c_str());

      // Flush dedup state before OTA reboot
      saveAllDedupStates(true);

      // Let HA know we are going away during update
      publishGatewayAvailability("offline");
    })
    .onEnd([]() {
      logPrintln("\nOTA End");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      logPrintf("OTA Progress: %u%%\r", (progress / (total / 100)));
    })
    .onError([](ota_error_t error) {
      logPrintf("OTA Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) logPrintln("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) logPrintln("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) logPrintln("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) logPrintln("Receive Failed");
      else if (error == OTA_END_ERROR) logPrintln("End Failed");
    });

  ArduinoOTA.begin();

  logPrint("OTA ready. Hostname: ");
  logPrintln(ota_hostname);
}

void setupRemoteLogging() {
  if (!REMOTE_LOGGING_ENABLED) {
    logPrintln("Unauthenticated telnet logging is disabled.");
    return;
  }
  telnetServer.begin();
  telnetServer.setNoDelay(true);

  logPrintf("Remote logs ready on telnet://%s:23\n", ota_hostname);
}

bool requireWebAuthentication() {
  if (strlen(admin_password) < 12) {
    webServer.send(503, "application/json", "{\"error\":\"admin_credentials_not_configured\"}");
    return false;
  }
  if (webServer.authenticate("admin", admin_password)) return true;
  webServer.requestAuthentication(BASIC_AUTH, "LoRa Tracker gateway");
  return false;
}

void setupWebInterface() {
  webServer.on("/", HTTP_GET, []() {
    if (!requireWebAuthentication()) return;
    logPrintln("Web UI accessed by a client.");
    String html;
    html.reserve(6000);
    html = "<html><head><title>" + String(GATEWAY_NAME) +
           "</title><meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<style>body{font-family:sans-serif;margin:20px}table{border-collapse:collapse;width:100%;max-width:1000px}th,td{border:1px solid #ccc;padding:6px;text-align:left}th{background:#eee}code{font-size:.9em}pre{background:#f4f4f4;padding:10px;min-height:260px;overflow-y:auto}</style></head><body>";
    html += "<h1>" + String(GATEWAY_NAME) + "</h1>";
    html += "<p>Gateway ID: <code>" + String(GATEWAY_ID) +
            "</code><br>Gateway hash: <code>" + String(gateway_hash_text) +
            "</code><br>Config schema/revision: " +
            String(gateway_config.header.schema_version) + "/" +
            String(gateway_config.header.revision) +
            "<br>Registered trackers: " + String(GATEWAY_TRACKER_COUNT) + "</p>";
    html += "<h2>Tracker registry</h2><table><tr><th>Name</th><th>ID</th><th>Hash</th><th>Last seen</th><th>RSSI</th><th>Dedup cursor</th></tr>";

    const uint32_t now = millis();
    for (size_t i = 0; i < GATEWAY_TRACKER_COUNT; i++) {
      const TrackerRuntime& tracker = tracker_runtime[i];
      html += "<tr><td>" + String(tracker.config->device_name) + "</td><td><code>" +
              String(tracker.config->device_id) + "</code></td><td><code>" +
              String(tracker.hash_text) + "</code></td><td>";
      if (tracker.has_been_seen) {
        html += String((now - tracker.last_seen_ms) / 1000UL) + " s ago";
      } else {
        html += "never";
      }
      html += "</td><td>";
      html += tracker.has_been_seen ? String(tracker.last_rssi) + " dBm" : "--";
      html += "</td><td>";
      if (tracker.has_processed_point) {
        html += "boot " + String(tracker.last_processed_boot_id) +
                ", seq " + String(tracker.last_processed_seq);
      } else {
        html += "none";
      }
      html += "</td></tr>";
    }
    html += "</table>";
    html += "<h2>Configuration</h2><p>Write access: <b>" +
            String(gatewayConfigWritesAllowed() ? "enabled" : "locked") +
            "</b>. To unlock an already provisioned gateway, reboot it, release USER, then hold USER for 1.5 seconds during the serial prompt.</p>";
    if (gatewayConfigWritesAllowed()) {
      html += "<form action='/api/v1/config' method='POST'>";
      html += "<input type='hidden' name='expected_revision' value='" + String(gateway_config.header.revision) + "'>";
      html += "Gateway ID: <input name='gateway_id' value='" + String(gateway_config.gateway_id) + "'><br>";
      html += "Name: <input name='gateway_name' value='" + String(gateway_config.gateway_name) + "'><br>";
      html += "Wi-Fi SSID: <input name='wifi_ssid' value='" + String(gateway_config.wifi_ssid) + "'><br>";
      html += "Wi-Fi password: <input type='password' name='wifi_password' value='__KEEP__'><br>";
      html += "MQTT host: <input name='mqtt_host' value='" + String(gateway_config.mqtt_host) + "'><br>";
      html += "MQTT port: <input name='mqtt_port' value='" + String(gateway_config.mqtt_port) + "'><br>";
      html += "MQTT user: <input name='mqtt_username' value='" + String(gateway_config.mqtt_username) + "'><br>";
      html += "MQTT password: <input type='password' name='mqtt_password' value='__KEEP__'><br>";
      html += "MQTT root CA (PEM): <textarea name='mqtt_ca_certificate' rows='8' cols='48' placeholder='-----BEGIN CERTIFICATE----- ... -----END CERTIFICATE-----'>__KEEP__</textarea><br>";
      html += "Maximum ACK relay hops (0-4): <input type='number' min='0' max='4' name='lora_relay_hop_limit' value='" + String(gateway_config.lora.relay_hop_limit) + "'><br>";
      html += "<input type='hidden' name='reboot' value='1'><button>Validate, save and reboot</button></form>";
    }
    html += "<p>Full registry and radio configuration uses <code>POST /api/v1/config</code>; tracker fields are named <code>tracker.0.id</code>, <code>tracker.0.name</code>, and so on.</p>";
    html += "<h2>Live logs</h2><pre id='logs'>Loading...</pre>";
    html += "<script>setInterval(function(){fetch('/logs').then(r=>r.text()).then(t=>{let e=document.getElementById('logs');let a=Math.abs(e.scrollHeight-e.scrollTop-e.clientHeight)<5;e.innerText=t;if(a)e.scrollTop=e.scrollHeight;});},2000);</script>";
    html += "</body></html>";
    webServer.send(200, "text/html", html);
  });

  webServer.on("/api/v1/onboarding", HTTP_GET, []() {
    if (!requireWebAuthentication()) return;
    String out = "{\"api_version\":" + String(EquineConfigApi::API_VERSION) +
      ",\"role\":\"gateway\",\"onboarding_required\":" +
      String(gateway_onboarding_required ? "true" : "false") +
      ",\"config_mode\":" + String(gateway_config_mode ? "true" : "false") +
      ",\"revision\":" + String(gateway_config.header.revision) +
      ",\"transports\":[\"wifi\"],\"config_post_content_type\":\"application/x-www-form-urlencoded\"}";
    webServer.send(200, "application/json", out);
  });

  webServer.on("/api/v1/config", HTTP_GET, []() {
    if (!requireWebAuthentication()) return;
    webServer.send(200, "application/json", gatewayConfigJson());
  });

  webServer.on("/api/v1/config", HTTP_POST, []() {
    if (!requireWebAuthentication()) return;
    if (!gatewayConfigWritesAllowed()) {
      webServer.send(403, "application/json",
        "{\"ok\":false,\"error\":\"physical_config_mode_required\"}");
      return;
    }
    EquineConfigApi::PatchStatus status;
    bool reboot = false;
    if (!applyGatewayWebPatch(status, reboot)) {
      const int code = strstr(status.error, "revision conflict") ? 409 : 400;
      String out = "{\"ok\":false,\"error\":\"";
      EquineConfigApi::appendJsonEscaped(out, status.error);
      out += "\",\"current_revision\":" +
             String(gateway_config.header.revision) + "}";
      webServer.send(code, "application/json", out);
      return;
    }
    const bool needs_reboot = status.reboot_required || reboot;
    String out = "{\"ok\":true,\"revision\":" +
      String(gateway_config.header.revision) +
      ",\"changed\":" + String(status.changed ? "true" : "false") +
      ",\"reboot_required\":" + String(needs_reboot ? "true" : "false") + "}";
    webServer.send(200, "application/json", out);
    gateway_config_reboot_requested |= needs_reboot;
  });

  webServer.on("/api/v1/config/rollback", HTTP_POST, []() {
    if (!requireWebAuthentication()) return;
    if (!gatewayConfigWritesAllowed()) {
      webServer.send(403, "application/json",
        "{\"ok\":false,\"error\":\"physical_config_mode_required\"}");
      return;
    }
    uint32_t expected = 0;
    if (!EquineConfigApi::parseUnsigned(
          webServer.arg("expected_revision").c_str(), 1, UINT32_MAX, expected) ||
        expected != gateway_config.header.revision) {
      webServer.send(409, "application/json",
        "{\"ok\":false,\"error\":\"revision_conflict\"}");
      return;
    }
    char error[EquineConfigApi::ERROR_SIZE] = {};
    if (!rollbackGatewayConfig(error, sizeof(error))) {
      String out = "{\"ok\":false,\"error\":\"";
      EquineConfigApi::appendJsonEscaped(out, error);
      out += "\"}";
      webServer.send(400, "application/json", out);
      return;
    }
    webServer.send(200, "application/json",
      "{\"ok\":true,\"reboot_required\":true}");
    gateway_config_reboot_requested = true;
  });

  webServer.on("/api/v1/factory-reset", HTTP_POST, []() {
    if (!requireWebAuthentication()) return;
    if (!gatewayConfigWritesAllowed()) {
      webServer.send(403, "application/json",
        "{\"ok\":false,\"error\":\"physical_config_mode_required\"}");
      return;
    }
    if (webServer.arg("confirm") != "FACTORY_RESET") {
      webServer.send(400, "application/json",
        "{\"ok\":false,\"error\":\"confirmation_required\"}");
      return;
    }
    webServer.send(200, "application/json",
      "{\"ok\":true,\"factory_reset\":true,\"onboarding_required_after_reboot\":true}");
    gateway_factory_reset_requested = true;
  });

  webServer.on("/api/v1/reboot", HTTP_POST, []() {
    if (!requireWebAuthentication()) return;
    if (!gatewayConfigWritesAllowed()) {
      webServer.send(403, "application/json",
        "{\"ok\":false,\"error\":\"physical_config_mode_required\"}");
      return;
    }
    webServer.send(200, "application/json", "{\"ok\":true,\"rebooting\":true}");
    gateway_config_reboot_requested = true;
  });

  webServer.on("/api/v1/trackers", HTTP_GET, []() {
    if (!requireWebAuthentication()) return;
    String out;
    out.reserve(2500);
    out = "{\"api_version\":1,\"gateway_id\":\"" + String(GATEWAY_ID) +
          "\",\"gateway_hash\":\"" + String(gateway_hash_text) +
          "\",\"trackers\":[";
    const uint32_t now = millis();
    for (size_t i = 0; i < GATEWAY_TRACKER_COUNT; i++) {
      const TrackerRuntime& tracker = tracker_runtime[i];
      char escaped_id[EquineConfig::DEVICE_ID_SIZE * 2]{};
      char escaped_name[EquineConfig::DEVICE_NAME_SIZE * 2]{};
      escapeJsonText(tracker.config->device_id, escaped_id, sizeof(escaped_id));
      escapeJsonText(tracker.config->device_name, escaped_name, sizeof(escaped_name));
      if (i > 0) out += ',';
      out += "{\"id\":\"" + String(escaped_id) +
             "\",\"name\":\"" + String(escaped_name) +
             "\",\"device_hash\":\"" + String(tracker.hash_text) +
             "\",\"state_topic\":\"" + String(tracker.state_topic) +
             "\",\"seen\":" + String(tracker.has_been_seen ? "true" : "false") +
             ",\"last_seen_ms_ago\":" +
             String(tracker.has_been_seen ? now - tracker.last_seen_ms : 0) +
             ",\"last_rssi\":" + String(tracker.last_rssi) +
             ",\"has_dedup_cursor\":" +
             String(tracker.has_processed_point ? "true" : "false") +
             ",\"last_boot_id\":" + String(tracker.last_processed_boot_id) +
             ",\"last_seq\":" + String(tracker.last_processed_seq) + "}";
    }
    out += "]}";
    webServer.send(200, "application/json", out);
  });

  webServer.on("/logs", HTTP_GET, []() {
    if (!requireWebAuthentication()) return;
    String out = "";
    if (logHistoryCount == 0) {
      out = "No logs yet.";
    } else {
      const uint8_t oldestIndex =
        (logHistoryHead + LOG_HISTORY_LINES - logHistoryCount) % LOG_HISTORY_LINES;
      for (uint8_t i = 0; i < logHistoryCount; i++) {
        const uint8_t index = (oldestIndex + i) % LOG_HISTORY_LINES;
        out += logHistory[index];
        out += "\n";
      }
    }
    webServer.send(200, "text/plain", out);
  });

  webServer.begin();
  logPrintln("Web UI started on port 80.");
}

void handleRemoteLogging() {
  if (!REMOTE_LOGGING_ENABLED) return;
  if (telnetServer.hasClient()) {
    WiFiClient newClient = telnetServer.available();

    if (telnetClient && telnetClient.connected()) {
      newClient.println("Remote log stream already in use.");
      newClient.stop();
    } else {
      telnetClient = newClient;
      telnetClient.println();
      telnetClient.printf("Connected to %s log stream.\n", ota_hostname);
      replayRecentLogsToClient(telnetClient);
      logPrintln("Remote log client connected.");
    }
  }

  if (telnetClient && !telnetClient.connected()) {
    telnetClient.stop();
    Serial.println("Remote log client disconnected.");
  }
}

void logReceiverHeartbeat() {
  const unsigned long now = millis();

  if (lastHeartbeatMs != 0 &&
      (now - lastHeartbeatMs) < LOG_HEARTBEAT_INTERVAL_MS) {
    return;
  }

  lastHeartbeatMs = now;
  const unsigned long secondsSincePacket =
    (lastLoRaPacketMs == 0 || now < lastLoRaPacketMs)
      ? 0
      : (now - lastLoRaPacketMs) / 1000UL;

  size_t seen_count = 0;
  for (size_t i = 0; i < GATEWAY_TRACKER_COUNT; i++) {
    if (tracker_runtime[i].has_been_seen) seen_count++;
  }

  logPrintf("Heartbeat: wifi=%s mqtt=%s trackers=%u/%u seen last_lora=%lus ago\n",
            WiFi.status() == WL_CONNECTED ? "up" : "down",
            client.connected() ? "up" : "down",
            (unsigned)seen_count,
            (unsigned)GATEWAY_TRACKER_COUNT,
            secondsSincePacket);

  if (client.connected() &&
      (lastGatewayStatusPublishMs == 0 ||
       now - lastGatewayStatusPublishMs >= GATEWAY_STATUS_INTERVAL_MS)) {
    publishGatewayStatus(true);
  }
}

void configureLoRaRadio() {
  LoRa.setTxPower(LORA_TX_POWER_DBM);
  LoRa.setSpreadingFactor(LORA_SPREADING_FACTOR);
  LoRa.setSignalBandwidth(LORA_SIGNAL_BANDWIDTH);
  LoRa.setCodingRate4(LORA_CODING_RATE);
  LoRa.setPreambleLength(LORA_PREAMBLE_LENGTH);
  LoRa.setSyncWord(LORA_SYNC_WORD);
  LoRa.enableCrc();

  logPrintf("LoRa config -> freq=%.0fHz tx=%ddBm sf=%d bw=%ld cr=4/%d sync=0x%02X preamble=%d\n",
            (double)LORA_FREQ,
            LORA_TX_POWER_DBM,
            LORA_SPREADING_FACTOR,
            LORA_SIGNAL_BANDWIDTH,
            LORA_CODING_RATE,
            LORA_SYNC_WORD,
            LORA_PREAMBLE_LENGTH);
}

// ==========================================
// HOME ASSISTANT AUTO-DISCOVERY
// ==========================================
void publishAutoDiscoveryForTracker(const TrackerRuntime& tracker) {
  char topic[160];
  char payload[768];
  char device_registry_id[48];
  char escaped_device_name[EquineConfig::DEVICE_NAME_SIZE * 2]{};
  escapeJsonText(tracker.config->device_name,
                 escaped_device_name, sizeof(escaped_device_name));
  snprintf(device_registry_id, sizeof(device_registry_id),
           "lora_tracker_%s", tracker.hash_text);

  // Keep the discovery path human-readable while using the hash in unique IDs.
  snprintf(topic, sizeof(topic),
           "homeassistant/device_tracker/%s/location/config",
           tracker.config->device_id);
  snprintf(payload, sizeof(payload),
    "{\"name\":\"%s Location\",\"stat_t\":\"%s\","
    "\"json_attr_t\":\"%s\",\"source_type\":\"gps\","
    "\"uniq_id\":\"%s_loc\",\"avty_t\":\"%s\","
    "\"dev\":{\"ids\":[\"%s\"],\"name\":\"%s Tracker\","
    "\"mdl\":\"LoRa Tracker\"}}",
    escaped_device_name,
    tracker.state_topic,
    tracker.state_topic,
    tracker.hash_text,
    gateway_availability_topic,
    device_registry_id,
    tracker.config->device_name);
  publishRetainedMessage(topic, payload);

  snprintf(topic, sizeof(topic),
           "homeassistant/sensor/%s/distance/config",
           tracker.config->device_id);
  snprintf(payload, sizeof(payload),
    "{\"name\":\"%s Total Distance\",\"stat_t\":\"%s\","
    "\"val_tpl\":\"{{(value_json.dist_m/1000)|round(2)}}\","
    "\"unit_of_meas\":\"km\",\"ic\":\"mdi:horse\","
    "\"stat_cla\":\"measurement\",\"uniq_id\":\"%s_dist\","
    "\"avty_t\":\"%s\",\"dev\":{\"ids\":[\"%s\"]}}",
    escaped_device_name,
    tracker.state_topic,
    tracker.hash_text,
    gateway_availability_topic,
    device_registry_id);
  publishRetainedMessage(topic, payload);

  snprintf(topic, sizeof(topic),
           "homeassistant/sensor/%s/battery/config",
           tracker.config->device_id);
  snprintf(payload, sizeof(payload),
    "{\"name\":\"%s Battery\",\"stat_t\":\"%s\","
    "\"val_tpl\":\"{{value_json.battery_level}}\","
    "\"unit_of_meas\":\"%%\",\"dev_cla\":\"battery\","
    "\"stat_cla\":\"measurement\",\"uniq_id\":\"%s_batt\","
    "\"avty_t\":\"%s\",\"dev\":{\"ids\":[\"%s\"]}}",
    escaped_device_name,
    tracker.state_topic,
    tracker.hash_text,
    gateway_availability_topic,
    device_registry_id);
  publishRetainedMessage(topic, payload);

  snprintf(topic, sizeof(topic),
           "homeassistant/sensor/%s/rssi/config",
           tracker.config->device_id);
  snprintf(payload, sizeof(payload),
    "{\"name\":\"%s LoRa Signal\",\"stat_t\":\"%s\","
    "\"val_tpl\":\"{{value_json.rssi}}\",\"unit_of_meas\":\"dBm\","
    "\"dev_cla\":\"signal_strength\",\"stat_cla\":\"measurement\","
    "\"uniq_id\":\"%s_rssi\",\"avty_t\":\"%s\","
    "\"dev\":{\"ids\":[\"%s\"]}}",
    escaped_device_name,
    tracker.state_topic,
    tracker.hash_text,
    gateway_availability_topic,
    device_registry_id);
  publishRetainedMessage(topic, payload);
}

void publishAutoDiscovery() {
  logPrintf("Publishing Home Assistant discovery for %u trackers...\n",
            (unsigned)GATEWAY_TRACKER_COUNT);
  for (size_t i = 0; i < GATEWAY_TRACKER_COUNT; i++) {
    publishAutoDiscoveryForTracker(tracker_runtime[i]);
  }
  logPrintln("Auto-Discovery complete.");
}


void startGatewayFallbackAp();

void serviceGatewayConfigButton() {
  const bool pressed = digitalRead(USER_BTN_PIN) == LOW;
  const uint32_t now = millis();
  if (pressed && !gateway_button_previous) {
    gateway_button_press_start_ms = now;
    gateway_button_hold_handled = false;
  }
  if (pressed && !gateway_button_hold_handled &&
      (uint32_t)(now - gateway_button_press_start_ms) >= 5000UL) {
    gateway_button_hold_handled = true;
    gateway_config_mode = true;
    gateway_config_mode_deadline_ms = now + 600000UL;
    logPrintln("USER held for 5 s: gateway configuration writes unlocked for 10 minutes.");
    if (WiFi.status() != WL_CONNECTED && !gateway_ap_active) {
      startGatewayFallbackAp();
    }
  }
  if (!pressed) gateway_button_hold_handled = false;
  gateway_button_previous = pressed;
}

bool postBootButtonRequestsGatewayConfig() {
  pinMode(USER_BTN_PIN, INPUT_PULLUP);
  // GPIO0 is a boot strap. Never require it to be held during reset. Wait for
  // release, then accept a deliberate 1.5 s hold during a short post-boot window.
  const uint32_t release_deadline = millis() + 2500;
  while (digitalRead(USER_BTN_PIN) == LOW &&
         (int32_t)(release_deadline - millis()) > 0) {
    delay(10);
  }
  logPrintln("Gateway config window: hold USER for 1.5 s within 5 s.");
  const uint32_t window_deadline = millis() + 5000;
  uint32_t press_start = 0;
  while ((int32_t)(window_deadline - millis()) > 0) {
    const bool pressed = digitalRead(USER_BTN_PIN) == LOW;
    if (pressed && press_start == 0) press_start = millis();
    if (!pressed) press_start = 0;
    if (pressed && (uint32_t)(millis() - press_start) >= 1500) {
      logPrintln("Physical gateway configuration mode enabled.");
      return true;
    }
    delay(10);
  }
  return false;
}

void startGatewayFallbackAp() {
  WiFi.disconnect(false, false);
  WiFi.mode(WIFI_AP);
  String ap_name = "LoRaGateway-" + String(GATEWAY_ID);
  if (strlen(admin_password) < 12) {
    logPrintln("Fallback AP disabled: onboarding password must be at least 12 characters.");
  } else if (WiFi.softAP(ap_name.c_str(), admin_password)) {
    gateway_ap_active = true;
    gateway_network_ip = WiFi.softAPIP().toString();
    logPrintf("Gateway setup AP '%s' active at %s.\n",
              ap_name.c_str(), gateway_network_ip.c_str());
    if (gateway_onboarding_required) {
      logPrintf("FIRST-BOOT ADMIN CREDENTIAL (record securely): admin / %s\n",
                admin_password);
    }
  } else {
    logPrintln("Failed to start gateway fallback AP.");
  }
}

void setupGatewayNetwork() {
  WiFi.persistent(false);
  if (gateway_onboarding_required) {
    logPrintln("Factory/unprovisioned gateway: starting onboarding AP.");
    startGatewayFallbackAp();
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  if (gateway_config.wifi_ssid[0] != '\0') {
    logPrintf("Connecting gateway to Wi-Fi '%s'...\n",
              gateway_config.wifi_ssid);
    WiFi.begin(gateway_config.wifi_ssid, gateway_config.wifi_password);
    const uint32_t deadline = millis() + 20000UL;
    while (WiFi.status() != WL_CONNECTED &&
           (int32_t)(deadline - millis()) > 0) {
      delay(250);
      Serial.print('.');
    }
    Serial.println();
  }

  if (WiFi.status() == WL_CONNECTED) {
    gateway_ap_active = false;
    gateway_network_ip = WiFi.localIP().toString();
    lastWiFiStatus = WiFi.status();
    logPrintf("Gateway Wi-Fi connected at %s.\n",
              gateway_network_ip.c_str());
    return;
  }

  logPrintln("Gateway Wi-Fi unavailable; entering local onboarding AP mode.");
  startGatewayFallbackAp();
}

void handleWiFiConnection() {
  if (gateway_ap_active) return;
  const wl_status_t currentStatus = WiFi.status();

  if (currentStatus != lastWiFiStatus) {
    logPrintf("WiFi status changed: %s -> %s\n",
              wifiStatusToString(lastWiFiStatus),
              wifiStatusToString(currentStatus));
    lastWiFiStatus = currentStatus;

    if (currentStatus != WL_CONNECTED && client.connected()) {
      client.disconnect();
    }
  }

  if (currentStatus == WL_CONNECTED) {
    return;
  }

  const unsigned long now = millis();
  if (lastWiFiReconnectAttemptMs != 0 &&
      (now - lastWiFiReconnectAttemptMs) < WIFI_RETRY_INTERVAL_MS) {
    return;
  }

  lastWiFiReconnectAttemptMs = now;
  logPrintln("WiFi disconnected, attempting reconnect...");
  WiFi.reconnect();
}

bool ensureTlsClockReady() {
  if (!gateway_config.mqtt_tls_enabled) return true;
  constexpr time_t MIN_VALID_TLS_TIME = 1704067200;  // 2024-01-01 UTC
  if (time(nullptr) >= MIN_VALID_TLS_TIME) return true;
  if (!ntp_sync_started) {
    configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
    ntp_sync_started = true;
    logPrintln("Waiting for NTP before certificate-validated MQTT TLS.");
  }
  return false;
}

void reconnectMqttIfNeeded() {
  if (WiFi.status() != WL_CONNECTED || client.connected()) {
    return;
  }

  const unsigned long now = millis();
  if (lastMqttReconnectAttemptMs != 0 &&
      (now - lastMqttReconnectAttemptMs) < MQTT_RETRY_INTERVAL_MS) {
    return;
  }

  lastMqttReconnectAttemptMs = now;

  if (!ensureTlsClockReady()) return;
  if (gateway_config.mqtt_tls_enabled && runtime_mqtt_ca_certificate.isEmpty()) {
    logPrintln("MQTT disabled: TLS is enabled but no CA certificate is configured.");
    return;
  }
  if (!gateway_config.mqtt_tls_enabled && !allow_insecure_mqtt) {
    logPrintln("MQTT disabled: plaintext transport requires allow_insecure_mqtt=true.");
    return;
  }
  logPrint("Attempting MQTT connection...");

  char clientId[48];
  snprintf(clientId, sizeof(clientId), "ltg-%s", gateway_hash_text);

  if (client.connect(
        clientId,
        gateway_config.mqtt_username,
        gateway_config.mqtt_password,
        gateway_availability_topic,
        1,
        true,
        "offline")) {
    logPrintln("CONNECTED!");
    publishGatewayAvailability("online");
    subscribeMqttTopics();
    publishGatewayStatus(true);
    publishAutoDiscovery();
  } else {
    logPrint("failed, rc=");
    logPrintf("%d", client.state());
    logPrintln(" retry scheduled");
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  if (!EquineCrypto::selfTest()) {
    Serial.println("Fatal: AES-GCM self-test failed.");
    while (true) delay(1000);
  }
  esp_fill_random(&gateway_tx_nonce_prefix, sizeof(gateway_tx_nonce_prefix));
  if (gateway_tx_nonce_prefix == 0) gateway_tx_nonce_prefix = 1;
  // Cold-start empty: rebooting must not restore a full regulatory burst.
  gateway_airtime_tokens_ms = 0.0;
  gateway_airtime_refill_ms = millis();
  initializeAdminCredential();
  initializeMqttCaCertificate();

  if (!loadGatewayConfig()) {
    makeGatewayFactoryConfig();
    gateway_onboarding_required = true;
    logPrintln("Warning: running with unsaved factory gateway configuration.");
  }

  if (!initializeTrackerRegistry()) {
    logPrintln("Fatal tracker configuration error; gateway halted.");
    while (true) delay(1000);
  }

  logPrintf("\n--- %s booting ---\n", GATEWAY_NAME);
  logPrintf("Gateway id=%s hash=%s protocol=v%u mqtt_api=v%u trackers=%u\n",
            GATEWAY_ID,
            gateway_hash_text,
            EquineProtocol::TRANSPORT_VERSION,
            EquineProtocol::MQTT_API_VERSION,
            (unsigned)GATEWAY_TRACKER_COUNT);
  for (size_t i = 0; i < GATEWAY_TRACKER_COUNT; i++) {
    const TrackerRuntime& tracker = tracker_runtime[i];
    logPrintf("  registry[%u] id=%s name=%s hash=%s\n",
              (unsigned)i,
              tracker.config->device_id,
              tracker.config->device_name,
              tracker.hash_text);
  }

  loadAllDedupStates();

  pinMode(USER_BTN_PIN, INPUT_PULLUP);
  gateway_config_mode = gateway_onboarding_required;
  setupGatewayNetwork();

  setupOTA();
  setupRemoteLogging();
  setupWebInterface();

  if (gateway_config.mqtt_tls_enabled) {
    secureMqttClient.setCACert(runtime_mqtt_ca_certificate.c_str());
    client.setClient(secureMqttClient);
  } else {
    client.setClient(plainMqttClient);
  }
  client.setServer(gateway_config.mqtt_host, gateway_config.mqtt_port);
  client.setBufferSize(MQTT_BUFFER_SIZE);
  client.setCallback(mqttMessageCallback);

  SPI.begin(SCK, MISO, MOSI, SS);
  LoRa.setPins(SS, RST, DIO0);

  if (!LoRa.begin(LORA_FREQ)) {
    logPrintln("LoRa initialization failed!");
    while (true) delay(1000);
  }

  configureLoRaRadio();
  LoRa.receive();
  logPrintln("Gateway is listening for registered LoRa trackers...");
}

void loop() {
  if (gateway_factory_reset_requested) {
    publishGatewayAvailability("offline");
    saveAllDedupStates(true);
    for (size_t i = 0; i < GATEWAY_TRACKER_COUNT; i++) {
      Preferences dedup;
      if (dedup.begin(tracker_runtime[i].dedup_namespace, false)) {
        dedup.clear();
        dedup.end();
      }
    }
    clearGatewayConfigStorage();
    delay(250);
    ESP.restart();
  }
  if (gateway_config_reboot_requested) {
    publishGatewayAvailability("offline");
    saveAllDedupStates(true);
    delay(250);
    ESP.restart();
  }
  if (gateway_config_mode && !gateway_onboarding_required &&
      gateway_config_mode_deadline_ms != 0 &&
      (int32_t)(millis() - gateway_config_mode_deadline_ms) >= 0) {
    gateway_config_mode = false;
    gateway_config_mode_deadline_ms = 0;
    logPrintln("Gateway configuration write window closed.");
  }

  serviceGatewayConfigButton();
  ArduinoOTA.handle();
  handleRemoteLogging();
  webServer.handleClient();
  logReceiverHeartbeat();

  handleWiFiConnection();
  if (WiFi.status() == WL_CONNECTED) {
    reconnectMqttIfNeeded();
    if (client.connected()) client.loop();
  }

  const int packetSize = LoRa.parsePacket();
  if (!packetSize) return;

  do {
    // Authenticated envelope + encrypted history, root and batch metadata.
    if (packetSize < (int)(sizeof(EquineRelay::LinkHeaderV1) +
                           sizeof(SecureFrameHeader) +
                           sizeof(HistoryPayload) + 8 + 2 +
                           EquineProtocol::AEAD_TAG_SIZE) ||
        packetSize > 255) {
      logPrintf("Ignored invalid packet size: %d bytes.\n", packetSize);
      break;
    }

    uint8_t payload_buffer[255];
    const int bytesRead = LoRa.readBytes(payload_buffer, packetSize);
    if (bytesRead != packetSize) {
      logPrintf("Short LoRa read. Expected %d, got %d.\n",
                packetSize, bytesRead);
      break;
    }

    const int packet_rssi = LoRa.packetRssi();
    lastLoRaPacketMs = millis();

    size_t offset = 0;
    DecodedHistoryHeader header{};
    TrackerRuntime* tracker = nullptr;

    EquineRelay::LinkHeaderV1 link_header{};
    SecureFrameHeader wire_header{};
    if (!EquineRelay::parseLinkedFrame(
          payload_buffer, packetSize, link_header, wire_header)) {
      logPrintln("Ignored malformed or unsupported relay link envelope.");
      break;
    }
    const EquineProtocol::FrameHeaderV1& frame = wire_header.frame;
    if (!EquineProtocol::isSupportedHistoryFrame(frame)) {
      logPrintf("Unsupported frame: transport=%u type=%u schema=%u flags=0x%02X.\n",
                frame.transport_version, frame.message_type,
                frame.schema_version, frame.flags);
      break;
    }

    tracker = findTrackerByHash(frame.device_id_hash);
    if (!tracker) {
      char unknown_hash[17];
      EquineProtocol::formatDeviceHash(frame.device_id_hash, unknown_hash,
                                      sizeof(unknown_hash));
      logPrintf("Ignored unregistered device hash %s.\n", unknown_hash);
      break;
    }

    const size_t ciphertext_size =
      packetSize - sizeof(link_header) - sizeof(wire_header) -
      EquineProtocol::AEAD_TAG_SIZE;
    uint8_t plaintext_buffer[255];
    const uint8_t* ciphertext = payload_buffer + sizeof(link_header) +
                                sizeof(wire_header);
    const uint8_t* tag = ciphertext + ciphertext_size;
    if (!EquineCrypto::decrypt(
          tracker->config->lora_aead_key,
          wire_header,
          ciphertext,
          ciphertext_size,
          tag,
          plaintext_buffer)) {
      logPrintf("Rejected history frame with invalid AES-GCM tag from %s.\n",
                tracker->config->device_id);
      break;
    }
    const size_t payload_size = ciphertext_size;
    if (payload_size < sizeof(HistoryPayload) + 8 + 2) {
      logPrintln("Authenticated history payload is too short.");
      break;
    }
    HistoryPayload history_payload{};
    memcpy(&history_payload, plaintext_buffer, sizeof(history_payload));

    header.transport_version = frame.transport_version;
    header.schema_version = frame.schema_version;
    header.device_id_hash = frame.device_id_hash;
    header.boot_id = wire_header.boot_id;
    header.first_seq = history_payload.first_seq;
    header.root_unix_time_s = history_payload.root_unix_time_s;
    header.timestamps_present =
      (frame.flags & EquineProtocol::FLAG_HAS_TIMESTAMPS) != 0;
    header.total_dist_dam = history_payload.total_dist_dam;
    header.batt_pct = history_payload.batt_pct;
    offset = sizeof(history_payload);

    if (header.timestamps_present &&
        (header.root_unix_time_s < 946684800UL ||
         header.root_unix_time_s > 4102444800UL)) {
      logPrintf("Rejected implausible root GNSS timestamp: %lu.\n",
                (unsigned long)header.root_unix_time_s);
      break;
    }
    if (!header.timestamps_present && header.root_unix_time_s != 0) {
      logPrintln("Timestamp value present without timestamp frame flag.");
      break;
    }

    tracker->has_been_seen = true;
    tracker->last_seen_ms = millis();
    tracker->last_rssi = packet_rssi;

    if (offset + 9 > payload_size) {
      logPrintln("Packet ended before root and anchor metadata.");
      break;
    }

    int32_t root_lat = 0;
    int32_t root_lon = 0;
    memcpy(&root_lat, plaintext_buffer + offset, 4); offset += 4;
    memcpy(&root_lon, plaintext_buffer + offset, 4); offset += 4;

    const uint8_t num_anchors = plaintext_buffer[offset++];
    AnchorPoint anchors[64]{};
    if (num_anchors > 64) {
      logPrintln("Too many anchors; packet rejected.");
      break;
    }
    const size_t anchors_bytes = num_anchors * sizeof(AnchorPoint);
    if (offset + anchors_bytes > payload_size) {
      logPrintln("Packet ended while reading anchors.");
      break;
    }
    if (anchors_bytes > 0) {
      memcpy(anchors, plaintext_buffer + offset, anchors_bytes);
      offset += anchors_bytes;
    }

    if (offset >= payload_size) {
      logPrintln("Packet ended before batch count.");
      break;
    }
    const uint8_t num_batches = plaintext_buffer[offset++];

    logPrintf(
      "Received packet [%s/%s]: transport=%u schema=%u boot=%lu "
      "first_seq=%lu root=%.6f,%.6f root_utc=%lu timestamps=%s "
      "anchors=%u batches=%u RSSI=%d relay_hops=%u/%u\n",
      tracker->config->device_id,
      tracker->hash_text,
      header.transport_version,
      header.schema_version,
      (unsigned long)header.boot_id,
      (unsigned long)header.first_seq,
      root_lat / 1000000.0f,
      root_lon / 1000000.0f,
      (unsigned long)header.root_unix_time_s,
      header.timestamps_present ? "yes" : "no",
      num_anchors,
      num_batches,
      packet_rssi,
      link_header.hop_count,
      link_header.hop_limit);

    bool packet_valid = true;
    bool publish_success = true;
    bool processed_any_point = false;
    resetPendingArchiveConfirmations();
    uint32_t current_seq = header.first_seq;
    uint32_t highest_ackable_seq = header.first_seq;

    auto processPoint = [&](int32_t p_lat, int32_t p_lon, uint32_t p_seq,
                            uint32_t p_unix_time_s, bool timestamp_valid) {
      if (!packet_valid || !publish_success) return;

      if (!isValidCoordinateMicrodeg(p_lat, p_lon)) {
        logPrintf("Invalid coordinate: tracker=%s seq=%lu lat=%ld lon=%ld\n",
                  tracker->config->device_id,
                  (unsigned long)p_seq,
                  (long)p_lat,
                  (long)p_lon);
        packet_valid = false;
        return;
      }

      if (isNewPoint(*tracker, header.boot_id, p_seq)) {
        const float real_lat = p_lat / 1000000.0f;
        const float real_lon = p_lon / 1000000.0f;
        const uint32_t dist_m = (uint32_t)header.total_dist_dam * 10U;

        char point_id[64];
        snprintf(point_id, sizeof(point_id), "%s:%lu:%lu",
                 tracker->hash_text,
                 (unsigned long)header.boot_id,
                 (unsigned long)p_seq);

        logPrintf("  -> New point [%s]: seq=%lu lat=%.6f lon=%.6f "
                  "utc=%lu valid=%s dist=%u m\n",
                  tracker->config->device_id,
                  (unsigned long)p_seq,
                  real_lat,
                  real_lon,
                  (unsigned long)p_unix_time_s,
                  timestamp_valid ? "yes" : "no",
                  dist_m);

        char escaped_device_id[EquineConfig::DEVICE_ID_SIZE * 2]{};
        char escaped_device_name[EquineConfig::DEVICE_NAME_SIZE * 2]{};
        char escaped_gateway_id[EquineConfig::DEVICE_ID_SIZE * 2]{};
        escapeJsonText(tracker->config->device_id, escaped_device_id, sizeof(escaped_device_id));
        escapeJsonText(tracker->config->device_name, escaped_device_name, sizeof(escaped_device_name));
        escapeJsonText(GATEWAY_ID, escaped_gateway_id, sizeof(escaped_gateway_id));

        char jsonBuffer[640];
        const int written = snprintf(
          jsonBuffer,
          sizeof(jsonBuffer),
          "{\"api_version\":%u,\"point_schema_version\":%u,"
          "\"transport_version\":%u,\"schema_version\":%u,"
          "\"device_id\":\"%s\","
          "\"device_name\":\"%s\",\"device_hash\":\"%s\","
          "\"gateway_id\":\"%s\",\"gateway_hash\":\"%s\","
          "\"point_id\":\"%s\",\"latitude\":%.6f,"
          "\"longitude\":%.6f,\"dist_m\":%u,"
          "\"battery_level\":%u,\"rssi\":%d,\"seq\":%lu,"
          "\"boot_id\":%lu,\"timestamp_valid\":%s,"
          "\"fix_time_unix_ms\":%llu,\"time_source\":\"%s\","
          "\"gateway_uptime_ms\":%lu}",
          EquineMqttApi::API_VERSION,
          EquineMqttApi::POINT_SCHEMA_VERSION,
          header.transport_version,
          header.schema_version,
          escaped_device_id,
          escaped_device_name,
          tracker->hash_text,
          escaped_gateway_id,
          gateway_hash_text,
          point_id,
          real_lat,
          real_lon,
          dist_m,
          header.batt_pct,
          packet_rssi,
          (unsigned long)p_seq,
          (unsigned long)header.boot_id,
          timestamp_valid ? "true" : "false",
          (unsigned long long)(timestamp_valid ?
            static_cast<uint64_t>(p_unix_time_s) * 1000ULL : 0ULL),
          timestamp_valid ? "gnss" : "unavailable",
          (unsigned long)millis());

        if (written <= 0 || written >= (int)sizeof(jsonBuffer)) {
          logPrintln("MQTT JSON overflow; packet retained for retry.");
          publish_success = false;
          return;
        }

        if (!publishTrackerPoint(*tracker, jsonBuffer)) {
          logPrintln("MQTT publish failed; packet retained for retry.");
          publish_success = false;
          return;
        }

        if (!addPendingArchiveConfirmation(point_id)) {
          logPrintln("Archive confirmation set overflow; packet retained for retry.");
          publish_success = false;
          return;
        }
      } else {
        logPrintf("  -> Duplicate already published [%s] boot=%lu seq=%lu\n",
                  tracker->config->device_id,
                  (unsigned long)header.boot_id,
                  (unsigned long)p_seq);
      }

      processed_any_point = true;
      highest_ackable_seq = p_seq;
    };

    uint32_t current_point_time_s = header.root_unix_time_s;
    processPoint(root_lat, root_lon, current_seq++,
                 current_point_time_s, header.timestamps_present);

    for (uint8_t batch = 0;
         batch < num_batches && packet_valid && publish_success;
         batch++) {
      if (offset + 2 > payload_size) {
        logPrintln("Packet ended before batch metadata.");
        packet_valid = false;
        break;
      }

      const uint8_t anchor_idx = plaintext_buffer[offset++];
      const uint8_t count = plaintext_buffer[offset++];
      if (anchor_idx >= num_anchors) {
        logPrintln("Invalid anchor index in batch.");
        packet_valid = false;
        break;
      }

      const int32_t anchor_lat_micro =
        root_lat + (int32_t)anchors[anchor_idx].dlat * DELTA_UNIT_MICRODEG;
      const int32_t anchor_lon_micro =
        root_lon + (int32_t)anchors[anchor_idx].dlon * DELTA_UNIT_MICRODEG;

      for (uint8_t point = 0; point < count; point++) {
        if (offset + 2 > payload_size) {
          logPrintln("Batch point ended before coordinate delta.");
          packet_valid = false;
          break;
        }
        const int8_t rel_dlat = (int8_t)plaintext_buffer[offset++];
        const int8_t rel_dlon = (int8_t)plaintext_buffer[offset++];
        const int32_t p_lat_micro =
          anchor_lat_micro + (int32_t)rel_dlat * DELTA_UNIT_MICRODEG;
        const int32_t p_lon_micro =
          anchor_lon_micro + (int32_t)rel_dlon * DELTA_UNIT_MICRODEG;

        uint32_t delta_time_s = 0;
        size_t timestamp_bytes = 0;
        if (!EquineProtocol::decodeUleb128U32(
              plaintext_buffer + offset,
              payload_size - offset,
              delta_time_s,
              timestamp_bytes)) {
          logPrintln("Invalid or truncated ULEB128 timestamp delta.");
          packet_valid = false;
          break;
        }
        offset += timestamp_bytes;
        if (header.timestamps_present) {
          if (delta_time_s > UINT32_MAX - current_point_time_s) {
            logPrintln("Timestamp delta overflow.");
            packet_valid = false;
            break;
          }
          current_point_time_s += delta_time_s;
        }

        processPoint(
          p_lat_micro,
          p_lon_micro,
          current_seq++,
          current_point_time_s,
          header.timestamps_present
        );
        if (!packet_valid || !publish_success) break;
      }
    }

    if (packet_valid && publish_success && offset != payload_size) {
      logPrintf("Unexpected %u trailing packet bytes; ACK withheld.\n",
                (unsigned)(payload_size - offset));
      packet_valid = false;
    }

    if (packet_valid && publish_success && pending_archive_count > 0) {
      const uint32_t deadline_ms = millis() + ARCHIVE_ACK_TIMEOUT_MS;
      while (client.connected() && !allArchiveConfirmationsReceived() &&
             static_cast<int32_t>(millis() - deadline_ms) < 0) {
        client.loop();
        delay(1);
      }
      if (!allArchiveConfirmationsReceived()) {
        logPrintf(
          "Archive confirmation timed out (%u points); ACK withheld and "
          "tracker data retained.\n",
          static_cast<unsigned>(pending_archive_count));
        publish_success = false;
      } else {
        updateDedupState(*tracker, header.boot_id, highest_ackable_seq);
      }
    }

    if (!packet_valid || !publish_success || !processed_any_point) {
      logPrintf("ACK withheld for tracker %s (valid=%s publish=%s points=%s).\n",
                tracker->config->device_id,
                packet_valid ? "yes" : "no",
                publish_success ? "yes" : "no",
                processed_any_point ? "yes" : "no");
      break;
    }

    saveDedupState(*tracker, false);

    AckPayload ack{};
    ack.acked_seq = highest_ackable_seq;
    SecureFrameHeader ack_header = EquineProtocol::makeSecureFrameHeader(
      EquineProtocol::MessageType::ACK,
      EquineProtocol::ACK_SCHEMA_VERSION,
      tracker->device_id_hash,
      EquineProtocol::deriveNoncePrefix(
        gateway_tx_nonce_prefix,
        tracker->device_id_hash,
        header.boot_id,
        EquineProtocol::MessageType::ACK),
      header.boot_id,
      highest_ackable_seq);
    const EquineRelay::LinkHeaderV1 ack_link =
      EquineRelay::makeOriginHeader(LORA_RELAY_HOP_LIMIT);
    uint8_t ack_packet[
      sizeof(ack_link) + sizeof(ack_header) + sizeof(ack) +
      EquineProtocol::AEAD_TAG_SIZE];
    memcpy(ack_packet, &ack_link, sizeof(ack_link));
    memcpy(ack_packet + sizeof(ack_link), &ack_header, sizeof(ack_header));
    uint8_t* ack_ciphertext =
      ack_packet + sizeof(ack_link) + sizeof(ack_header);
    uint8_t* ack_tag = ack_ciphertext + sizeof(ack);
    if (!EquineCrypto::encrypt(
          tracker->config->lora_aead_key,
          ack_header,
          reinterpret_cast<const uint8_t*>(&ack),
          sizeof(ack),
          ack_ciphertext,
          ack_tag)) {
      logPrintf("Failed to encrypt ACK for tracker %s.\n",
                tracker->config->device_id);
      break;
    }
    uint32_t estimated_airtime_ms = 0;
    if (!reserveGatewayAirtime(sizeof(ack_packet), estimated_airtime_ms)) {
      gateway_airtime_deferrals++;
      logPrintf(
        "ACK deferred by Germany 1%% airtime limiter for tracker %s; "
        "the tracker will retry.\n",
        tracker->config->device_id);
      break;
    }
    LoRa.beginPacket();
    LoRa.write(ack_packet, sizeof(ack_packet));

    const uint32_t tx_started_ms = millis();
    const int txResult = LoRa.endPacket();
    const uint32_t measured_airtime_ms = millis() - tx_started_ms;
    if (measured_airtime_ms > estimated_airtime_ms) {
      gateway_airtime_tokens_ms -= min(
        gateway_airtime_tokens_ms,
        static_cast<double>(measured_airtime_ms - estimated_airtime_ms));
    }
    if (txResult) {
      logPrintf("Sent ACK [%s] boot=%lu seq=%lu\n",
                tracker->config->device_id,
                (unsigned long)header.boot_id,
                (unsigned long)highest_ackable_seq);
    } else {
      logPrintf("Failed to transmit ACK for tracker %s.\n",
                tracker->config->device_id);
    }
  } while (false);

  LoRa.receive();
}
