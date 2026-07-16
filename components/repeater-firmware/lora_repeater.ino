#include <Arduino.h>
#include <Preferences.h>
#include <SPI.h>
#include <WebServer.h>
#include <WiFi.h>

#if defined(BOARD_WIRELESS_TRACKER)
  #include <RadioLib.h>
#else
  #include <LoRa.h>
#endif

#include "equine_config.h"
#include "equine_protocol.h"
#include "equine_relay.h"

namespace {

constexpr uint8_t USER_BUTTON_PIN = 0;
constexpr uint32_t CONFIG_HOLD_MS = 1500;
constexpr uint32_t CONFIG_WINDOW_MS = 10UL * 60UL * 1000UL;
constexpr size_t FORWARD_QUEUE_SIZE = 8;
constexpr size_t RECENT_CACHE_SIZE = 48;
constexpr size_t ACK_RESERVATION_SIZE = 8;
constexpr uint32_t ACK_RESERVATION_TTL_MS = 35000;
constexpr uint32_t FORWARD_TURNAROUND_GUARD_MS = 50;
constexpr size_t ADMIN_PASSWORD_SIZE = 21;

#if defined(BOARD_WIRELESS_TRACKER)
constexpr int LORA_SCK = 9;
constexpr int LORA_MISO = 11;
constexpr int LORA_MOSI = 10;
constexpr int LORA_NSS = 8;
constexpr int LORA_RST = 12;
constexpr int LORA_BUSY = 13;
constexpr int LORA_DIO1 = 14;
SPIClass lora_spi(FSPI);
SX1262 radio = new Module(
  LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY, lora_spi);
#else
constexpr int LORA_SCK = 5;
constexpr int LORA_MISO = 19;
constexpr int LORA_MOSI = 27;
constexpr int LORA_NSS = 18;
constexpr int LORA_RST = 14;
constexpr int LORA_DIO0 = 26;
#endif

struct PendingForward {
  bool used;
  EquineRelay::FrameIdentityV1 identity;
  uint32_t due_ms;
  uint8_t outgoing_hop;
  uint8_t retry_count;
  uint8_t packet_size;
  uint8_t packet[EquineRelay::MAX_PACKET_SIZE];
};

struct RecentFrame {
  bool used;
  EquineRelay::FrameIdentityV1 identity;
  uint32_t expires_ms;
};

struct AckReservation {
  bool used;
  uint64_t device_id_hash;
  uint32_t boot_id;
  uint32_t transaction_counter;
  uint32_t expires_ms;
  uint32_t airtime_ms;
};

EquineConfig::RepeaterConfigV1 config{};
PendingForward queue_entries[FORWARD_QUEUE_SIZE]{};
RecentFrame recent_frames[RECENT_CACHE_SIZE]{};
AckReservation ack_reservations[ACK_RESERVATION_SIZE]{};
Preferences config_preferences;
WebServer web_server(80);

bool onboarding_required = false;
bool config_portal_active = false;
bool restart_requested = false;
bool radio_ready = false;
uint32_t config_portal_deadline_ms = 0;
uint64_t repeater_hash = 0;
char admin_password[ADMIN_PASSWORD_SIZE]{};
char repeater_hash_text[17]{};

double airtime_tokens_ms = 0.0;
uint32_t airtime_refill_ms = 0;
uint32_t last_heartbeat_ms = 0;
int16_t last_rssi = 0;
int16_t last_snr_tenths = 0;

uint32_t received_frames = 0;
uint32_t forwarded_frames = 0;
uint32_t suppressed_frames = 0;
uint32_t invalid_frames = 0;
uint32_t queue_drops = 0;
uint32_t airtime_deferrals = 0;
uint32_t transmit_failures = 0;
uint32_t transaction_budget_drops = 0;

bool timeReached(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}

String htmlEscape(const char* value) {
  String escaped(value ? value : "");
  escaped.replace("&", "&amp;");
  escaped.replace("<", "&lt;");
  escaped.replace(">", "&gt;");
  escaped.replace("\"", "&quot;");
  escaped.replace("'", "&#39;");
  return escaped;
}

bool parseUnsignedArg(
    const char* name, uint32_t minimum, uint32_t maximum, uint32_t& value) {
  if (!web_server.hasArg(name)) return false;
  const String text = web_server.arg(name);
  if (text.isEmpty()) return false;
  char* end = nullptr;
  const unsigned long parsed = strtoul(text.c_str(), &end, 0);
  if (!end || *end != '\0' || parsed < minimum || parsed > maximum) {
    return false;
  }
  value = static_cast<uint32_t>(parsed);
  return true;
}

bool parseSignedArg(
    const char* name, int32_t minimum, int32_t maximum, int32_t& value) {
  if (!web_server.hasArg(name)) return false;
  const String text = web_server.arg(name);
  if (text.isEmpty()) return false;
  char* end = nullptr;
  const long parsed = strtol(text.c_str(), &end, 0);
  if (!end || *end != '\0' || parsed < minimum || parsed > maximum) {
    return false;
  }
  value = static_cast<int32_t>(parsed);
  return true;
}

void generateDefaultIdentity(char* output, size_t output_size) {
  const uint64_t chip = ESP.getEfuseMac();
  snprintf(output, output_size, "repeater-%04lx",
           static_cast<unsigned long>(chip & 0xffffUL));
}

void clearAdminPassword() {
  Preferences credentials;
  if (credentials.begin("ltrepcred", false)) {
    credentials.clear();
    credentials.end();
  }
}

void initializeAdminPassword() {
  Preferences credentials;
  credentials.begin("ltrepcred", false);
  const String stored = credentials.getString("admin", "");
  if (stored.length() >= 12 && stored.length() < ADMIN_PASSWORD_SIZE) {
    strlcpy(admin_password, stored.c_str(), sizeof(admin_password));
  } else {
    static const char alphabet[] =
      "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
    for (size_t index = 0; index < 20; index++) {
      admin_password[index] =
        alphabet[esp_random() % (sizeof(alphabet) - 1)];
    }
    admin_password[20] = '\0';
    credentials.putString("admin", admin_password);
  }
  credentials.end();
}

bool readStoredConfig(
    const char* key, EquineConfig::RepeaterConfigV1& output) {
  const size_t length = config_preferences.getBytesLength(key);
  if (length != sizeof(output)) return false;
  return config_preferences.getBytes(key, &output, sizeof(output)) ==
         sizeof(output) && EquineConfig::validateRepeaterConfig(output);
}

bool saveConfig(EquineConfig::RepeaterConfigV1 candidate) {
  EquineConfig::finalize(
    candidate,
    EquineConfig::DeviceRole::REPEATER,
    config.header.revision == UINT32_MAX ? 1 : config.header.revision + 1);
  if (!EquineConfig::validateRepeaterConfig(candidate)) return false;
  if (!onboarding_required && EquineConfig::validateRepeaterConfig(config) &&
      config_preferences.putBytes(
        EquineConfig::BACKUP_CONFIG_KEY, &config, sizeof(config)) !=
        sizeof(config)) {
    return false;
  }
  if (config_preferences.putBytes(
        EquineConfig::ACTIVE_CONFIG_KEY, &candidate, sizeof(candidate)) !=
      sizeof(candidate)) {
    return false;
  }
  config = candidate;
  onboarding_required = false;
  return true;
}

void loadConfig() {
  if (!config_preferences.begin(EquineConfig::CONFIG_NAMESPACE, false)) {
    char id[EquineConfig::DEVICE_ID_SIZE]{};
    generateDefaultIdentity(id, sizeof(id));
    EquineConfig::makeDefaultRepeaterConfig(config, id, "LoRa repeater");
    onboarding_required = true;
    Serial.println("Configuration storage unavailable; onboarding required.");
    return;
  }
  if (readStoredConfig(EquineConfig::ACTIVE_CONFIG_KEY, config)) return;
  if (readStoredConfig(EquineConfig::BACKUP_CONFIG_KEY, config)) {
    const size_t restored = config_preferences.putBytes(
      EquineConfig::ACTIVE_CONFIG_KEY, &config, sizeof(config));
    if (restored != sizeof(config)) {
      Serial.println("Recovered backup config, but active-slot repair failed.");
    } else {
      Serial.println("Recovered backup config and repaired active slot.");
    }
    return;
  }
  char id[EquineConfig::DEVICE_ID_SIZE]{};
  generateDefaultIdentity(id, sizeof(id));
  EquineConfig::makeDefaultRepeaterConfig(config, id, "LoRa repeater");
  onboarding_required = true;
}

bool requireAuthentication() {
  if (web_server.authenticate("admin", admin_password)) return true;
  web_server.requestAuthentication(BASIC_AUTH, "LoRa Tracker repeater");
  return false;
}

String statusJson() {
  String out;
  out.reserve(800);
  out = "{\"role\":\"repeater\",\"id\":\"" +
    String(config.repeater_id) + "\",\"device_hash\":\"" +
    String(repeater_hash_text) + "\",\"onboarding_required\":" +
    String(onboarding_required ? "true" : "false") +
    ",\"relay_hop_cap\":" + String(config.lora.relay_hop_limit) +
    ",\"queue_depth\":";
  size_t queue_depth = 0;
  for (const PendingForward& entry : queue_entries) {
    if (entry.used) queue_depth++;
  }
  out += String(queue_depth) + ",\"airtime_tokens_ms\":" +
    String(static_cast<uint32_t>(airtime_tokens_ms)) +
    ",\"stats\":{\"received\":" + String(received_frames) +
    ",\"forwarded\":" + String(forwarded_frames) +
    ",\"suppressed\":" + String(suppressed_frames) +
    ",\"invalid\":" + String(invalid_frames) +
    ",\"queue_drops\":" + String(queue_drops) +
    ",\"airtime_deferrals\":" + String(airtime_deferrals) +
    ",\"tx_failures\":" + String(transmit_failures) + "}}";
  return out;
}

void setupWebPortal() {
  web_server.on("/", HTTP_GET, []() {
    if (!requireAuthentication()) return;
    String html;
    html.reserve(6500);
    html = "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'><title>LoRa repeater</title><style>body{font-family:sans-serif;max-width:760px;margin:24px auto;padding:0 12px}label{display:block;margin:.7rem 0}input{width:100%;padding:.45rem;box-sizing:border-box}button{padding:.65rem 1rem}code,pre{background:#eee;padding:.2rem}small{color:#555}</style></head><body>";
    html += "<h1>" + htmlEscape(config.repeater_name) + "</h1>";
    html += "<p>ID <code>" + htmlEscape(config.repeater_id) +
      "</code>, hash <code>" + String(repeater_hash_text) +
      "</code>. Configuration schema/revision " +
      String(config.header.schema_version) + "/" +
      String(config.header.revision) + ".</p>";
    html += "<p><strong>Repeating is " +
      String(onboarding_required ? "disabled until a valid configuration is saved" : "active") +
      ".</strong></p><form action='/save' method='post'>";
    html += "<label>Repeater ID<input name='repeater_id' value='" + htmlEscape(config.repeater_id) + "'></label>";
    html += "<label>Name<input name='repeater_name' value='" + htmlEscape(config.repeater_name) + "'></label>";
    html += "<label>Frequency (Hz)<input name='frequency_hz' value='" + String(config.lora.frequency_hz) + "'></label>";
    html += "<label>Bandwidth (Hz)<input name='bandwidth_hz' value='" + String(config.lora.bandwidth_hz) + "'></label>";
    html += "<label>TX power (dBm)<input name='tx_power_dbm' value='" + String(config.lora.tx_power_dbm) + "'></label>";
    html += "<label>Spreading factor<input name='sf' value='" + String(config.lora.spreading_factor) + "'></label>";
    html += "<label>Coding-rate denominator (4/x)<input name='coding_rate' value='" + String(config.lora.coding_rate_denominator) + "'></label>";
    html += "<label>Preamble symbols<input name='preamble' value='" + String(config.lora.preamble_length) + "'></label>";
    html += "<label>Sync word (decimal or 0xNN)<input name='sync_word' value='" + String(config.lora.sync_word) + "'></label>";
    html += "<label>Local relay hop cap (1-4)<input name='hop_limit' value='" + String(config.lora.relay_hop_limit) + "'></label>";
    html += "<label>Forwarding base delay (ms)<input name='base_delay_ms' value='" + String(config.forwarding_base_delay_ms) + "'></label>";
    html += "<label>Priority slot width (ms)<input name='slot_width_ms' value='" + String(config.forwarding_slot_width_ms) + "'></label>";
    html += "<label>Priority slot count<input name='slot_count' value='" + String(config.forwarding_slot_count) + "'></label>";
    html += "<label>Duplicate cache lifetime (s)<input name='cache_ttl_s' value='" + String(config.duplicate_cache_ttl_s) + "'></label>";
    html += "<label>Airtime budget per hour (ms, max 36000)<input name='airtime_budget_ms' value='" + String(config.airtime_budget_ms_per_hour) + "'></label>";
    html += "<label>Heartbeat interval (s)<input name='heartbeat_s' value='" + String(config.heartbeat_interval_s) + "'></label>";
    html += "<small>Radio settings must exactly match trackers and receivers. Saving reboots the device.</small><br><button>Validate, save and reboot</button></form>";
    html += "<h2>Runtime</h2><pre>" + statusJson() + "</pre>";
    html += "<form action='/factory-reset' method='post'><input type='hidden' name='confirm' value='FACTORY_RESET'><button>Factory reset</button></form></body></html>";
    web_server.send(200, "text/html", html);
  });

  web_server.on("/api/v1/status", HTTP_GET, []() {
    if (!requireAuthentication()) return;
    web_server.send(200, "application/json", statusJson());
  });

  web_server.on("/save", HTTP_POST, []() {
    if (!requireAuthentication()) return;
    EquineConfig::RepeaterConfigV1 candidate = config;
    const String id = web_server.arg("repeater_id");
    const String name = web_server.arg("repeater_name");
    if (id.length() >= sizeof(candidate.repeater_id) ||
        name.length() >= sizeof(candidate.repeater_name)) {
      web_server.send(400, "text/plain", "ID or name is too long");
      return;
    }
    strlcpy(candidate.repeater_id, id.c_str(), sizeof(candidate.repeater_id));
    strlcpy(candidate.repeater_name, name.c_str(), sizeof(candidate.repeater_name));

    uint32_t frequency = 0, bandwidth = 0, sf = 0, coding = 0;
    uint32_t preamble = 0, sync_word = 0, hop_limit = 0;
    uint32_t base_delay = 0, slot_width = 0, slot_count = 0;
    uint32_t cache_ttl = 0, heartbeat = 0, airtime_budget = 0;
    int32_t tx_power = 0;
    const bool parsed =
      parseUnsignedArg(
        "frequency_hz", EquineConfig::GERMANY_FREQUENCY_MIN_HZ,
        EquineConfig::GERMANY_FREQUENCY_MAX_HZ, frequency) &&
      parseUnsignedArg("bandwidth_hz", 62500, 500000, bandwidth) &&
      parseSignedArg(
        "tx_power_dbm", 2,
        EquineConfig::GERMANY_MAX_CONDUCTED_POWER_DBM, tx_power) &&
      parseUnsignedArg("sf", 7, 12, sf) &&
      parseUnsignedArg("coding_rate", 5, 8, coding) &&
      parseUnsignedArg("preamble", 6, 32, preamble) &&
      parseUnsignedArg("sync_word", 0, 255, sync_word) &&
      parseUnsignedArg("hop_limit", 1, EquineRelay::MAX_HOPS, hop_limit) &&
      parseUnsignedArg("base_delay_ms", 10, 2000, base_delay) &&
      parseUnsignedArg("slot_width_ms", 5, 1000, slot_width) &&
      parseUnsignedArg("slot_count", 1, 32, slot_count) &&
      parseUnsignedArg("cache_ttl_s", 10, 3600, cache_ttl) &&
      parseUnsignedArg("heartbeat_s", 10, 3600, heartbeat) &&
      parseUnsignedArg("airtime_budget_ms", 1000, 36000, airtime_budget);
    if (!parsed || !EquineConfig::isSupportedBandwidth(bandwidth)) {
      web_server.send(400, "text/plain", "Invalid numeric radio or forwarding setting");
      return;
    }
    candidate.lora.frequency_hz = frequency;
    candidate.lora.bandwidth_hz = bandwidth;
    candidate.lora.tx_power_dbm = static_cast<int8_t>(tx_power);
    candidate.lora.spreading_factor = static_cast<uint8_t>(sf);
    candidate.lora.coding_rate_denominator = static_cast<uint8_t>(coding);
    candidate.lora.preamble_length = static_cast<uint8_t>(preamble);
    candidate.lora.sync_word = static_cast<uint8_t>(sync_word);
    candidate.lora.relay_hop_limit = static_cast<uint8_t>(hop_limit);
    candidate.forwarding_base_delay_ms = static_cast<uint16_t>(base_delay);
    candidate.forwarding_slot_width_ms = static_cast<uint16_t>(slot_width);
    candidate.forwarding_slot_count = static_cast<uint8_t>(slot_count);
    candidate.duplicate_cache_ttl_s = static_cast<uint16_t>(cache_ttl);
    candidate.heartbeat_interval_s = static_cast<uint16_t>(heartbeat);
    candidate.airtime_budget_ms_per_hour = airtime_budget;
    if (!saveConfig(candidate)) {
      web_server.send(400, "text/plain", "Configuration failed validation or storage");
      return;
    }
    web_server.send(200, "text/plain", "Saved; rebooting");
    restart_requested = true;
  });

  web_server.on("/factory-reset", HTTP_POST, []() {
    if (!requireAuthentication()) return;
    if (web_server.arg("confirm") != "FACTORY_RESET") {
      web_server.send(400, "text/plain", "Confirmation required");
      return;
    }
    config_preferences.clear();
    clearAdminPassword();
    web_server.send(200, "text/plain", "Factory reset; rebooting");
    restart_requested = true;
  });
}

void startConfigPortal() {
  char ssid[33]{};
  snprintf(ssid, sizeof(ssid), "lora-repeater-%04lx",
           static_cast<unsigned long>(repeater_hash & 0xffffUL));
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(ssid, admin_password)) {
    Serial.println("Failed to start configuration AP.");
    return;
  }
  setupWebPortal();
  web_server.begin();
  config_portal_active = true;
  config_portal_deadline_ms = onboarding_required
    ? 0
    : millis() + CONFIG_WINDOW_MS;
  Serial.printf("Configuration AP: %s\n", ssid);
  Serial.printf("Open http://%s and authenticate as admin / %s\n",
                WiFi.softAPIP().toString().c_str(), admin_password);
}

bool initializeRadio() {
  const float frequency_mhz = config.lora.frequency_hz / 1000000.0f;
#if defined(BOARD_WIRELESS_TRACKER)
  lora_spi.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
  const int state = radio.begin(
    frequency_mhz,
    config.lora.bandwidth_hz / 1000.0f,
    config.lora.spreading_factor,
    config.lora.coding_rate_denominator,
    config.lora.sync_word,
    config.lora.tx_power_dbm,
    config.lora.preamble_length,
    1.6,
    false);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.printf("SX1262 initialization failed: %d\n", state);
    return false;
  }
  radio.setCRC(true);
  return radio.startReceive() == RADIOLIB_ERR_NONE;
#else
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
  LoRa.setPins(LORA_NSS, LORA_RST, LORA_DIO0);
  if (!LoRa.begin(config.lora.frequency_hz)) return false;
  LoRa.setTxPower(config.lora.tx_power_dbm);
  LoRa.setSpreadingFactor(config.lora.spreading_factor);
  LoRa.setSignalBandwidth(config.lora.bandwidth_hz);
  LoRa.setCodingRate4(config.lora.coding_rate_denominator);
  LoRa.setPreambleLength(config.lora.preamble_length);
  LoRa.setSyncWord(config.lora.sync_word);
  LoRa.enableCrc();
  LoRa.receive();
  return true;
#endif
}

int receivePacket(uint8_t* packet, size_t capacity) {
#if defined(BOARD_WIRELESS_TRACKER)
  const uint16_t irq = radio.getIrqStatus();
  if (irq & RADIOLIB_SX126X_IRQ_CRC_ERR) {
    radio.startReceive();
    invalid_frames++;
    return 0;
  }
  if (!(irq & RADIOLIB_SX126X_IRQ_RX_DONE)) return 0;
  const size_t packet_size = radio.getPacketLength();
  if (packet_size == 0 || packet_size > capacity) {
    radio.startReceive();
    invalid_frames++;
    return 0;
  }
  const int state = radio.readData(packet, packet_size);
  last_rssi = static_cast<int16_t>(radio.getRSSI());
  last_snr_tenths = static_cast<int16_t>(radio.getSNR() * 10.0f);
  radio.startReceive();
  return state == RADIOLIB_ERR_NONE ? static_cast<int>(packet_size) : 0;
#else
  const int packet_size = LoRa.parsePacket();
  if (!packet_size) return 0;
  if (packet_size < 0 || packet_size > static_cast<int>(capacity)) {
    while (LoRa.available()) LoRa.read();
    invalid_frames++;
    return 0;
  }
  const int read = LoRa.readBytes(packet, packet_size);
  last_rssi = static_cast<int16_t>(LoRa.packetRssi());
  last_snr_tenths = static_cast<int16_t>(LoRa.packetSnr() * 10.0f);
  return read == packet_size ? packet_size : 0;
#endif
}

bool transmitPacket(uint8_t* packet, size_t packet_size) {
#if defined(BOARD_WIRELESS_TRACKER)
  const int state = radio.transmit(packet, packet_size);
  radio.startReceive();
  return state == RADIOLIB_ERR_NONE;
#else
  LoRa.idle();
  if (!LoRa.beginPacket()) {
    LoRa.receive();
    return false;
  }
  LoRa.write(packet, packet_size);
  const bool sent = LoRa.endPacket() == 1;
  LoRa.receive();
  return sent;
#endif
}

PendingForward* findPending(const EquineRelay::FrameIdentityV1& identity) {
  for (PendingForward& entry : queue_entries) {
    if (entry.used && EquineRelay::sameIdentity(entry.identity, identity)) {
      return &entry;
    }
  }
  return nullptr;
}

bool wasRecentlySeen(
    const EquineRelay::FrameIdentityV1& identity, uint32_t now) {
  for (RecentFrame& entry : recent_frames) {
    if (entry.used && timeReached(now, entry.expires_ms)) entry.used = false;
    if (entry.used && EquineRelay::sameIdentity(entry.identity, identity)) {
      return true;
    }
  }
  return false;
}

void rememberFrame(
    const EquineRelay::FrameIdentityV1& identity, uint32_t now) {
  RecentFrame* oldest = &recent_frames[0];
  for (RecentFrame& entry : recent_frames) {
    if (!entry.used || timeReached(now, entry.expires_ms)) {
      oldest = &entry;
      break;
    }
    if (static_cast<int32_t>(entry.expires_ms - oldest->expires_ms) < 0) {
      oldest = &entry;
    }
  }
  oldest->used = true;
  oldest->identity = identity;
  oldest->expires_ms = now + EquineRelay::duplicateCacheTtlMs(
    identity, config.duplicate_cache_ttl_s);
}

void scheduleForward(const uint8_t* packet, size_t packet_size) {
  EquineRelay::LinkHeaderV2 link{};
  EquineProtocol::SecureFrameHeaderV2 secure{};
  if (!EquineRelay::parseLinkedFrame(packet, packet_size, link, secure)) {
    invalid_frames++;
    return;
  }
  received_frames++;
  const uint32_t local_token = EquineRelay::relayToken(repeater_hash);
  if (!EquineRelay::mayRelay(
        link, secure, config.lora.relay_hop_limit, local_token)) {
    suppressed_frames++;
    return;
  }

  const EquineRelay::FrameIdentityV1 identity =
    EquineRelay::frameIdentity(secure);
  PendingForward* pending = findPending(identity);
  if (pending) {
    // A peer already forwarded at the hop we planned (or beyond), so our copy
    // would add airtime without extending the covered neighborhood.
    if (EquineRelay::peerForwardSuppressesPending(
          link.hop_count, pending->outgoing_hop)) {
      pending->used = false;
    }
    suppressed_frames++;
    return;
  }
  const uint32_t now = millis();
  if (wasRecentlySeen(identity, now)) {
    suppressed_frames++;
    return;
  }

  PendingForward* free_entry = nullptr;
  for (PendingForward& entry : queue_entries) {
    if (!entry.used) {
      free_entry = &entry;
      break;
    }
  }
  if (!free_entry) {
    queue_drops++;
    return;
  }

  free_entry->used = true;
  free_entry->identity = identity;
  free_entry->packet_size = static_cast<uint8_t>(packet_size);
  memcpy(free_entry->packet, packet, packet_size);
  if (!EquineRelay::advanceHop(
        free_entry->packet, packet_size, config.lora.relay_hop_limit,
        local_token)) {
    free_entry->used = false;
    suppressed_frames++;
    return;
  }
  EquineRelay::LinkHeaderV2 outgoing{};
  memcpy(&outgoing, free_entry->packet, sizeof(outgoing));
  free_entry->outgoing_hop = outgoing.hop_count;
  free_entry->retry_count = 0;
  const bool ack = identity.message_type ==
    static_cast<uint8_t>(EquineProtocol::MessageType::ACK);
  const uint32_t packet_airtime = EquineRelay::estimateAirtimeMs(
    packet_size, config.lora.spreading_factor, config.lora.bandwidth_hz,
    config.lora.coding_rate_denominator, config.lora.preamble_length);
  const uint32_t requested_slot_width = max(
    static_cast<uint32_t>(config.forwarding_slot_width_ms),
    packet_airtime + FORWARD_TURNAROUND_GUARD_MS);
  const uint16_t safe_slot_width = requested_slot_width > UINT16_MAX
    ? UINT16_MAX : static_cast<uint16_t>(requested_slot_width);
  free_entry->due_ms = now + (ack
    ? config.forwarding_base_delay_ms
    : EquineRelay::forwardingDelayMs(
        identity, repeater_hash, config.forwarding_base_delay_ms,
        config.forwarding_slot_count, safe_slot_width));
}

uint32_t ackAirtimeMs() {
  constexpr size_t ACK_PACKET_SIZE = sizeof(EquineRelay::LinkHeaderV2) +
    sizeof(EquineProtocol::SecureFrameHeaderV2) +
    sizeof(EquineProtocol::AckPayloadV1) + EquineProtocol::AEAD_TAG_SIZE;
  return EquineRelay::estimateAirtimeMs(
    ACK_PACKET_SIZE, config.lora.spreading_factor, config.lora.bandwidth_hz,
    config.lora.coding_rate_denominator, config.lora.preamble_length);
}

uint32_t transactionCapacityMs() {
  return EquineRelay::maxFrameAirtimeMs(
    config.lora.spreading_factor, config.lora.bandwidth_hz,
    config.lora.coding_rate_denominator, config.lora.preamble_length) +
    ackAirtimeMs();
}

void expireAckReservations(uint32_t now) {
  for (AckReservation& reservation : ack_reservations) {
    if (!reservation.used || !timeReached(now, reservation.expires_ms)) continue;
    airtime_tokens_ms = min(
      static_cast<double>(transactionCapacityMs()),
      airtime_tokens_ms + reservation.airtime_ms);
    reservation.used = false;
  }
}

AckReservation* findAckReservation(
    uint64_t device_id_hash, uint32_t boot_id, uint32_t transaction_counter) {
  for (AckReservation& reservation : ack_reservations) {
    if (reservation.used && reservation.device_id_hash == device_id_hash &&
        reservation.boot_id == boot_id &&
        reservation.transaction_counter == transaction_counter) return &reservation;
  }
  return nullptr;
}

bool storeAckReservation(
    const EquineRelay::FrameIdentityV1& identity,
    uint32_t transaction_counter, uint32_t airtime_ms, uint32_t now) {
  for (AckReservation& reservation : ack_reservations) {
    if (!reservation.used) {
      reservation = {true, identity.device_id_hash, identity.boot_id,
        transaction_counter, now + ACK_RESERVATION_TTL_MS, airtime_ms};
      return true;
    }
  }
  return false;
}

void refillAirtime(uint32_t now) {
  if (airtime_refill_ms == 0) {
    airtime_refill_ms = now;
    return;
  }
  const uint32_t elapsed = now - airtime_refill_ms;
  airtime_refill_ms = now;
  const uint32_t capacity_ms = transactionCapacityMs();
  airtime_tokens_ms = EquineRelay::refillRollingHourAirtimeTokens(
    airtime_tokens_ms,
    elapsed,
    config.airtime_budget_ms_per_hour,
    capacity_ms);
}

void serviceForwardQueue() {
  const uint32_t now = millis();
  refillAirtime(now);
  expireAckReservations(now);
  PendingForward* due = nullptr;
  for (PendingForward& entry : queue_entries) {
    if (!entry.used || !timeReached(now, entry.due_ms)) continue;
    if (!due || static_cast<int32_t>(entry.due_ms - due->due_ms) < 0) {
      due = &entry;
    }
  }
  if (!due) return;

  const uint32_t estimated_airtime = EquineRelay::estimateAirtimeMs(
    due->packet_size,
    config.lora.spreading_factor,
    config.lora.bandwidth_hz,
    config.lora.coding_rate_denominator,
    config.lora.preamble_length);
  if (estimated_airtime == 0) {
    invalid_frames++;
    due->used = false;
    return;
  }
  EquineRelay::LinkHeaderV2 link{};
  memcpy(&link, due->packet, sizeof(link));
  const bool ack = due->identity.message_type ==
    static_cast<uint8_t>(EquineProtocol::MessageType::ACK);
  AckReservation* reservation = findAckReservation(
    due->identity.device_id_hash, due->identity.boot_id,
    link.transaction_counter);
  const uint32_t reserved_ack_airtime = ackAirtimeMs();
  const uint32_t required_airtime = ack
    ? (reservation ? 0 : estimated_airtime)
    : estimated_airtime + (reservation ? 0 : reserved_ack_airtime);
  if (!EquineRelay::consumeAirtimeTokens(
        airtime_tokens_ms, required_airtime)) {
    airtime_deferrals++;
    transaction_budget_drops++;
    // Deferring one half of a transaction beyond the tracker's RX window is
    // useless. Drop it and let the tracker's normal retry create a fresh path.
    due->used = false;
    return;
  }
  if (!ack && !reservation && !storeAckReservation(
        due->identity, link.transaction_counter, reserved_ack_airtime, now)) {
    airtime_tokens_ms += required_airtime;
    transaction_budget_drops++;
    due->used = false;
    return;
  }

  const uint32_t started = millis();
  const bool sent = transmitPacket(due->packet, due->packet_size);
  const uint32_t measured_airtime = millis() - started;
  if (ack && reservation) reservation->used = false;
  if (measured_airtime > estimated_airtime) {
    airtime_tokens_ms -= min(
      airtime_tokens_ms,
      static_cast<double>(measured_airtime - estimated_airtime));
  }
  if (sent) {
    rememberFrame(due->identity, millis());
    forwarded_frames++;
    Serial.printf("Forwarded type=%u boot=%lu counter=%lu hop=%u bytes=%u airtime=%lu ms\n",
      due->identity.message_type,
      static_cast<unsigned long>(due->identity.boot_id),
      static_cast<unsigned long>(due->identity.counter),
      due->outgoing_hop,
      due->packet_size,
      static_cast<unsigned long>(measured_airtime));
  } else {
    transmit_failures++;
    if (due->retry_count < 3) {
      due->retry_count++;
      due->due_ms = millis() +
        (200UL << static_cast<uint32_t>(due->retry_count - 1));
      return;
    }
  }
  due->used = false;
}

void logHeartbeat() {
  const uint32_t now = millis();
  if (last_heartbeat_ms != 0 &&
      now - last_heartbeat_ms <
        static_cast<uint32_t>(config.heartbeat_interval_s) * 1000UL) {
    return;
  }
  last_heartbeat_ms = now;
  size_t depth = 0;
  for (const PendingForward& entry : queue_entries) {
    if (entry.used) depth++;
  }
  Serial.printf(
    "Heartbeat radio=%s portal=%s queue=%u rx=%lu tx=%lu suppressed=%lu invalid=%lu drops=%lu/%lu/%lu rssi=%d snr=%.1f tokens=%lu ms\n",
    radio_ready ? "up" : "down",
    config_portal_active ? "up" : "down",
    static_cast<unsigned>(depth),
    static_cast<unsigned long>(received_frames),
    static_cast<unsigned long>(forwarded_frames),
    static_cast<unsigned long>(suppressed_frames),
    static_cast<unsigned long>(invalid_frames),
    static_cast<unsigned long>(queue_drops),
    static_cast<unsigned long>(airtime_deferrals),
    static_cast<unsigned long>(transaction_budget_drops),
    last_rssi,
    last_snr_tenths / 10.0f,
    static_cast<unsigned long>(airtime_tokens_ms));
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  pinMode(USER_BUTTON_PIN, INPUT_PULLUP);
  initializeAdminPassword();
  loadConfig();
  repeater_hash = EquineProtocol::deviceIdHash(config.repeater_id);
  EquineProtocol::formatDeviceHash(
    repeater_hash, repeater_hash_text, sizeof(repeater_hash_text));
  // Start empty so rebooting cannot reset the regulatory limiter to a full burst.
  airtime_tokens_ms = 0.0;
  airtime_refill_ms = millis();

  Serial.printf("LoRa Tracker repeater %s (%s), config schema=%u revision=%lu\n",
    config.repeater_id,
    repeater_hash_text,
    config.header.schema_version,
    static_cast<unsigned long>(config.header.revision));
  Serial.println("Repeaters are keyless; encrypted tracker payloads and ACKs remain opaque.");

  bool portal_requested = onboarding_required;
  if (digitalRead(USER_BUTTON_PIN) == LOW) {
    const uint32_t pressed = millis();
    while (digitalRead(USER_BUTTON_PIN) == LOW &&
           millis() - pressed < CONFIG_HOLD_MS) {
      delay(10);
    }
    portal_requested |= millis() - pressed >= CONFIG_HOLD_MS;
  }
  if (portal_requested) startConfigPortal();

  radio_ready = initializeRadio();
  if (!radio_ready) {
    Serial.println("Fatal: LoRa radio initialization failed; repeating disabled.");
  } else if (onboarding_required) {
    Serial.println("Repeating disabled until onboarding is complete.");
  } else {
    Serial.printf("Listening at %lu Hz, SF%u, hop cap %u, airtime budget %lu ms/hour.\n",
      static_cast<unsigned long>(config.lora.frequency_hz),
      config.lora.spreading_factor,
      config.lora.relay_hop_limit,
      static_cast<unsigned long>(config.airtime_budget_ms_per_hour));
  }
}

void loop() {
  if (restart_requested) {
    delay(250);
    ESP.restart();
  }
  if (config_portal_active) {
    web_server.handleClient();
    if (!onboarding_required && config_portal_deadline_ms != 0 &&
        timeReached(millis(), config_portal_deadline_ms)) {
      web_server.stop();
      WiFi.softAPdisconnect(true);
      WiFi.mode(WIFI_OFF);
      config_portal_active = false;
      Serial.println("Configuration window closed.");
    }
  }

  if (radio_ready) {
    uint8_t packet[EquineRelay::MAX_PACKET_SIZE];
    const int packet_size = receivePacket(packet, sizeof(packet));
    if (packet_size > 0 && !onboarding_required) {
      scheduleForward(packet, static_cast<size_t>(packet_size));
    }
    if (!onboarding_required) serviceForwardQueue();
  }
  logHeartbeat();
  delay(2);
}
