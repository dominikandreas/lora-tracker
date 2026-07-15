#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <SPI.h>
#include <LoRa.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <stdarg.h>
#include <WebServer.h>
#include "secrets.h"

// ==========================================
// TRACKER IDENTITY
// ==========================================
const char* TRACKER_ID   = "wera";
const char* TRACKER_NAME = "Wera";

constexpr uint16_t MQTT_BUFFER_SIZE = 768;

// Dynamic topics based on the ID
char state_topic[64];
char availability_topic[64];
char ota_hostname[64];

// --- Heltec V2 Internal LoRa Pins ---
#define SCK   5
#define MISO  19
#define MOSI  27
#define SS    18
#define RST   14
#define DIO0  26

#define LORA_FREQ 868E6
const int LORA_TX_POWER_DBM = 20;
const int LORA_SPREADING_FACTOR = 10;
const long LORA_SIGNAL_BANDWIDTH = 125E3;
const int LORA_CODING_RATE = 5;
const int LORA_PREAMBLE_LENGTH = 8;
const int LORA_SYNC_WORD = 0x12;
const uint8_t HISTORY_SIZE = 25;
const int32_t DELTA_UNIT_MICRODEG = 10; // 1e-5 degree steps expressed in microdegrees
const uint32_t LOG_HEARTBEAT_INTERVAL_MS = 30000;
const uint8_t LOG_HISTORY_LINES = 25; // Increased for web UI
const size_t LOG_HISTORY_LINE_LENGTH = 160;
const uint32_t WIFI_RETRY_INTERVAL_MS = 10000;
const uint32_t MQTT_RETRY_INTERVAL_MS = 5000;

// Persist dedup progress every N successful publishes to reduce flash wear
const uint32_t DEDUP_SAVE_INTERVAL = 10;

// ==========================================
// PAYLOAD FORMAT
// ==========================================
struct AckPayload {
  uint32_t boot_id;
  uint32_t acked_seq;
} __attribute__((packed));

struct LoRaHeader {
  uint32_t boot_id;
  uint32_t first_seq;
  uint16_t total_dist_dam;
  uint8_t batt_pct;
} __attribute__((packed));

struct AnchorPoint {
  int16_t dlat;
  int16_t dlon;
} __attribute__((packed));

WiFiClient espClient;
PubSubClient client(espClient);
Preferences prefs;
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
wl_status_t lastWiFiStatus = WL_IDLE_STATUS;

// Dedup state: last successfully published point identity
uint32_t last_processed_boot_id = 0;
uint32_t last_processed_seq = 0;
bool has_processed_point = false;
uint32_t unsaved_dedup_updates = 0;

// ==========================================
// HELPERS
// ==========================================
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

bool isNewPoint(uint32_t boot_id, uint32_t seq) {
  if (!has_processed_point) {
    return true;
  }

  if (boot_id != last_processed_boot_id) {
    return true;
  }

  return seq > last_processed_seq;
}

void loadDedupState() {
  prefs.begin("gateway", false);

  has_processed_point = prefs.getBool("has_point", false);
  last_processed_boot_id = prefs.getULong("boot_id", 0);
  last_processed_seq = prefs.getULong("seq", 0);
  unsaved_dedup_updates = 0;

  logPrintf("Loaded dedup state -> has_point=%s boot_id=%lu seq=%lu\n",
            has_processed_point ? "true" : "false",
            (unsigned long)last_processed_boot_id,
            (unsigned long)last_processed_seq);
}

void saveDedupState(bool force = false) {
  if (!has_processed_point) {
    return;
  }

  if (!force && unsaved_dedup_updates < DEDUP_SAVE_INTERVAL) {
    return;
  }

  prefs.putBool("has_point", has_processed_point);
  prefs.putULong("boot_id", last_processed_boot_id);
  prefs.putULong("seq", last_processed_seq);
  unsaved_dedup_updates = 0;

  logPrintf("Saved dedup state -> boot_id=%lu seq=%lu\n",
            (unsigned long)last_processed_boot_id,
            (unsigned long)last_processed_seq);
}

void updateDedupState(uint32_t boot_id, uint32_t seq) {
  last_processed_boot_id = boot_id;
  last_processed_seq = seq;
  has_processed_point = true;
  unsaved_dedup_updates++;
}

void setupOTA() {
  snprintf(ota_hostname, sizeof(ota_hostname), "equine-gateway-%s", TRACKER_ID);

  ArduinoOTA.setHostname(ota_hostname);

  ArduinoOTA
    .onStart([]() {
      String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
      logPrintln((String("OTA Start updating ") + type).c_str());

      // Flush dedup state before OTA reboot
      saveDedupState(true);

      // Let HA know we are going away during update
      publishRetainedMessage(availability_topic, "offline");
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
  telnetServer.begin();
  telnetServer.setNoDelay(true);

  logPrintf("Remote logs ready on telnet://%s:23\n", ota_hostname);
}

void setupWebInterface() {
  webServer.on("/", HTTP_GET, []() {
    logPrintln("Web UI accessed by a client.");
    String html = "<html><head><title>Equine Gateway (" + String(TRACKER_ID) + ")</title><meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>body{font-family:sans-serif;margin:20px;} pre{background:#f4f4f4;padding:10px;min-height:300px;overflow-y:auto;}</style></head><body>";
    html += "<h1>Equine Gateway (" + String(TRACKER_ID) + ")</h1>";
    html += "<h3>Live Logs</h3><pre id='logs'>Loading...</pre>";
    html += "<script>setInterval(function(){ fetch('/logs').then(r=>r.text()).then(t=>{let el=document.getElementById('logs');let auto=Math.abs(el.scrollHeight-el.scrollTop-el.clientHeight)<5;el.innerText=t;if(auto)el.scrollTop=el.scrollHeight;}); }, 2000);</script>";
    html += "</body></html>";
    webServer.send(200, "text/html", html);
  });

  webServer.on("/logs", HTTP_GET, []() {
    String out = "";
    if (logHistoryCount == 0) {
      out = "No logs yet.";
    } else {
      const uint8_t oldestIndex = (logHistoryHead + LOG_HISTORY_LINES - logHistoryCount) % LOG_HISTORY_LINES;
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

  if (lastHeartbeatMs != 0 && (now - lastHeartbeatMs) < LOG_HEARTBEAT_INTERVAL_MS) {
    return;
  }

  lastHeartbeatMs = now;

  const unsigned long secondsSincePacket =
    (lastLoRaPacketMs == 0 || now < lastLoRaPacketMs) ? 0 : (now - lastLoRaPacketMs) / 1000UL;

  logPrintf("Heartbeat: wifi=%s mqtt=%s last_lora=%lus ago\n",
            WiFi.status() == WL_CONNECTED ? "up" : "down",
            client.connected() ? "up" : "down",
            secondsSincePacket);
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
void publishAutoDiscovery() {
  logPrintln("Publishing Home Assistant Auto-Discovery payloads...");

  char topic[128];
  char payload[512];

  // Device tracker
  snprintf(topic, sizeof(topic), "homeassistant/device_tracker/%s/location/config", TRACKER_ID);
  snprintf(payload, sizeof(payload),
    "{\"name\":\"%s Location\",\"stat_t\":\"%s\",\"json_attr_t\":\"%s\",\"source_type\":\"gps\","
    "\"uniq_id\":\"%s_loc\",\"avty_t\":\"%s\","
    "\"dev\":{\"ids\":[\"%s\"],\"name\":\"%s Tracker\",\"mdl\":\"Equine Telemetry\"}}",
    TRACKER_NAME, state_topic, state_topic, TRACKER_ID, availability_topic, TRACKER_ID, TRACKER_NAME);
  publishRetainedMessage(topic, payload);

  // Total distance
  snprintf(topic, sizeof(topic), "homeassistant/sensor/%s/distance/config", TRACKER_ID);
  snprintf(payload, sizeof(payload),
    "{\"name\":\"%s Total Distance\",\"stat_t\":\"%s\","
    "\"val_tpl\":\"{{(value_json.dist_m/1000)|round(2)}}\","
    "\"unit_of_meas\":\"km\",\"ic\":\"mdi:horse\",\"stat_cla\":\"measurement\","
    "\"uniq_id\":\"%s_dist\",\"avty_t\":\"%s\","
    "\"dev\":{\"ids\":[\"%s\"]}}",
    TRACKER_NAME, state_topic, TRACKER_ID, availability_topic, TRACKER_ID);
  publishRetainedMessage(topic, payload);

  // Battery
  snprintf(topic, sizeof(topic), "homeassistant/sensor/%s/battery/config", TRACKER_ID);
  snprintf(payload, sizeof(payload),
    "{\"name\":\"%s Battery\",\"stat_t\":\"%s\","
    "\"val_tpl\":\"{{value_json.battery_level}}\",\"unit_of_meas\":\"%%\","
    "\"dev_cla\":\"battery\",\"stat_cla\":\"measurement\","
    "\"uniq_id\":\"%s_batt\",\"avty_t\":\"%s\","
    "\"dev\":{\"ids\":[\"%s\"]}}",
    TRACKER_NAME, state_topic, TRACKER_ID, availability_topic, TRACKER_ID);
  publishRetainedMessage(topic, payload);

  // RSSI
  snprintf(topic, sizeof(topic), "homeassistant/sensor/%s/rssi/config", TRACKER_ID);
  snprintf(payload, sizeof(payload),
    "{\"name\":\"%s LoRa Signal\",\"stat_t\":\"%s\","
    "\"val_tpl\":\"{{value_json.rssi}}\",\"unit_of_meas\":\"dBm\","
    "\"dev_cla\":\"signal_strength\",\"stat_cla\":\"measurement\","
    "\"uniq_id\":\"%s_rssi\",\"avty_t\":\"%s\","
    "\"dev\":{\"ids\":[\"%s\"]}}",
    TRACKER_NAME, state_topic, TRACKER_ID, availability_topic, TRACKER_ID);
  publishRetainedMessage(topic, payload);

  logPrintln("Auto-Discovery complete!");
}

void handleWiFiConnection() {
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

  logPrint("Attempting MQTT connection...");

  char clientId[32];
  snprintf(clientId, sizeof(clientId), "Gateway-%s", TRACKER_ID);

  if (client.connect(clientId, mqtt_user, mqtt_pass, availability_topic, 1, true, "offline")) {
    logPrintln("CONNECTED!");
    publishRetainedMessage(availability_topic, "online");
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

  snprintf(state_topic, sizeof(state_topic), "horse/%s/state", TRACKER_ID);
  snprintf(availability_topic, sizeof(availability_topic), "horse/%s/availability", TRACKER_ID);

  logPrintf("\n--- Equine Gateway Booting for: %s ---\n", TRACKER_NAME);

  loadDedupState();

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    logPrint(".");
  }
  logPrintln("\nWiFi connected!");
  lastWiFiStatus = WiFi.status();

  setupOTA();
  setupRemoteLogging();
  setupWebInterface();

  client.setServer(mqtt_server, mqtt_port);
  client.setBufferSize(MQTT_BUFFER_SIZE);

  SPI.begin(SCK, MISO, MOSI, SS);
  LoRa.setPins(SS, RST, DIO0);

  if (!LoRa.begin(LORA_FREQ)) {
    logPrintln("LoRa initialization failed!");
    while (true) { }
  }

  configureLoRaRadio();

  LoRa.receive();
  logPrintln("Gateway is listening for LoRa packets...");
}

void loop() {
  ArduinoOTA.handle();
  handleRemoteLogging();
  webServer.handleClient();
  logReceiverHeartbeat();

  handleWiFiConnection();

  if (WiFi.status() == WL_CONNECTED) {
    reconnectMqttIfNeeded();
    if (client.connected()) {
      client.loop();
    }
  }

  int packetSize = LoRa.parsePacket();

  if (!packetSize) {
    return;
  }

  do {
    // Accept variable length payloads
    if (packetSize < sizeof(LoRaHeader) + 8 + 2) {
      logPrintf("Ignored invalid packet size. Got: %d\n", packetSize);
      break;
    }

    uint8_t payload_buffer[255];
    int bytesRead = LoRa.readBytes(payload_buffer, packetSize);

    if (bytesRead != packetSize) {
      logPrintf("Short LoRa read. Expected: %d, Got: %d\n",
                packetSize, bytesRead);
      break;
    }

    lastLoRaPacketMs = millis();

    size_t offset = 0;
    LoRaHeader header;
    memcpy(&header, payload_buffer + offset, sizeof(LoRaHeader));
    offset += sizeof(LoRaHeader);

    int32_t root_lat, root_lon;
    memcpy(&root_lat, payload_buffer + offset, 4); offset += 4;
    memcpy(&root_lon, payload_buffer + offset, 4); offset += 4;

    uint8_t num_anchors = payload_buffer[offset++];
    AnchorPoint anchors[64];
    if (num_anchors > 0 && num_anchors <= 64) {
      if (offset + num_anchors * sizeof(AnchorPoint) <= packetSize) {
        memcpy(anchors, payload_buffer + offset, num_anchors * sizeof(AnchorPoint));
        offset += num_anchors * sizeof(AnchorPoint);
      } else {
        logPrintln("Packet size mismatch during anchors reading.");
        break;
      }
    } else if (num_anchors > 64) {
      logPrintln("Too many anchors, ignoring packet");
      break;
    }

    if (offset >= packetSize) {
      logPrintln("Packet ended before batch metadata.");
      break;
    }

    uint8_t num_batches = payload_buffer[offset++];

    uint32_t current_seq = header.first_seq;

    logPrintf("Received compressed packet: boot_id=%lu first_seq=%lu root_lat=%.6f root_lon=%.6f anchors=%u batches=%u RSSI=%d\n",
              (unsigned long)header.boot_id, (unsigned long)header.first_seq,
              root_lat / 1000000.0f, root_lon / 1000000.0f,
              num_anchors, num_batches, LoRa.packetRssi());

    bool publish_success = true;

    auto processPoint = [&](int32_t p_lat, int32_t p_lon, uint32_t p_seq) {
      if (!publish_success) {
        return;
      }

      if (!isValidCoordinateMicrodeg(p_lat, p_lon)) {
        logPrintf("Ignoring out-of-range point: seq=%lu lat=%ld lon=%ld\n",
                  (unsigned long)p_seq,
                  (long)p_lat,
                  (long)p_lon);
        return;
      }

      if (isNewPoint(header.boot_id, p_seq)) {
        float real_lat = p_lat / 1000000.0f;
        float real_lon = p_lon / 1000000.0f;
        uint32_t dist_m = (uint32_t)header.total_dist_dam * 10U;

        logPrintf("  -> New point: boot_id=%lu seq=%lu lat=%.6f lon=%.6f dist=%u m\n",
                  (unsigned long)header.boot_id,
                  (unsigned long)p_seq,
                  real_lat,
                  real_lon,
                  dist_m);

        char jsonBuffer[256];
        int written = snprintf(
          jsonBuffer,
          sizeof(jsonBuffer),
          "{\"latitude\":%.6f,\"longitude\":%.6f,\"dist_m\":%u,\"battery_level\":%u,\"rssi\":%d,\"seq\":%lu,\"boot_id\":%lu}",
          real_lat,
          real_lon,
          dist_m,
          header.batt_pct,
          LoRa.packetRssi(),
          (unsigned long)p_seq,
          (unsigned long)header.boot_id
        );

        if (written > 0 && written < (int)sizeof(jsonBuffer)) {
          if (client.publish(state_topic, jsonBuffer)) {
            updateDedupState(header.boot_id, p_seq);
          } else {
            logPrintln("MQTT publish failed. Halting parsing for this packet.");
            publish_success = false;
          }
        }
      }
    };

    processPoint(root_lat, root_lon, current_seq);
    current_seq++;

    for (uint8_t b = 0; b < num_batches; b++) {
      if (offset + 2 > packetSize || !publish_success) {
        break;
      }

      uint8_t anchor_idx = payload_buffer[offset++];
      uint8_t count = payload_buffer[offset++];

      if (anchor_idx >= num_anchors) {
        logPrintln("Invalid anchor index in batch!");
        break;
      }

      int32_t anchor_lat_micro = root_lat + (int32_t)anchors[anchor_idx].dlat * DELTA_UNIT_MICRODEG;
      int32_t anchor_lon_micro = root_lon + (int32_t)anchors[anchor_idx].dlon * DELTA_UNIT_MICRODEG;

      if (offset + count * 2 > packetSize) {
        logPrintln("Batch count exceeds remaining packet bytes!");
        break;
      }

      for (uint8_t p = 0; p < count; p++) {
        int8_t rel_dlat = (int8_t)payload_buffer[offset++];
        int8_t rel_dlon = (int8_t)payload_buffer[offset++];

        int32_t p_lat_micro = anchor_lat_micro + (int32_t)rel_dlat * DELTA_UNIT_MICRODEG;
        int32_t p_lon_micro = anchor_lon_micro + (int32_t)rel_dlon * DELTA_UNIT_MICRODEG;

        processPoint(p_lat_micro, p_lon_micro, current_seq);
        current_seq++;
      }
    }

    if (publish_success) {
      saveDedupState(false);

      AckPayload ack;
      ack.boot_id = header.boot_id;
      ack.acked_seq = current_seq - 1;

      LoRa.beginPacket();
      LoRa.write((uint8_t*)&ack, sizeof(AckPayload));
      int txResult = LoRa.endPacket();

      if (txResult) {
        logPrintf("Sent ACK for boot_id=%lu seq=%lu\n",
                  (unsigned long)ack.boot_id,
                  (unsigned long)ack.acked_seq);
      } else {
        logPrintln("Failed to transmit ACK payload.");
      }
    }
  } while (false);

  // Always resume RX mode after processing a received packet.
  LoRa.receive();
}