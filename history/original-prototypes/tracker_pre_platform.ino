#include <Arduino.h>
#include <TinyGPS++.h>
#include <SPI.h>

#if defined(BOARD_WIRELESS_TRACKER)
  #include <RadioLib.h>
  #include <TFT_eSPI.h>
  TFT_eSPI tft = TFT_eSPI();
#else
  #include <LoRa.h>
  #include <U8g2lib.h>
  #include <Wire.h>
  U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(U8G2_R0, /* clock=*/ 15, /* data=*/ 4, /* reset=*/ 16);
#endif

#define USER_BTN_PIN 0

#include <Preferences.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <string.h>
#include <WebServer.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "secrets.h"

#if defined(BOARD_WIRELESS_TRACKER)
  // --- GPS UART pins (UC6580) ---
  #define RXD2 33          // GNSS_TX
  #define TXD2 34          // GNSS_RX
  #define GNSS_RST_PIN 35  // GNSS_RST
  #define VEXT_CTRL_PIN 3  // Vext Ctrl; powers GNSS/TFT on Wireless Tracker V1.1
  #define GNSS_PWR_PIN 37  // VGNSS Ctrl on earlier/pinout variants
  
  // --- Battery ---
  #define BATT_PIN 1       // ADC1_CH0 / Vbat sense
  #define ADC_CTRL 2       // ADC control / divider enable

  // --- SX1262 LoRa Pins ---
  #define LORA_SCK 9
  #define LORA_MISO 11
  #define LORA_MOSI 10
  #define LORA_NSS 8
  #define LORA_RST 12
  #define LORA_BUSY 13
  #define LORA_DIO1 14
  
  #define GPS_BAUD 115200
#else
  // --- GPS UART pins (BN-220) ---
  #define RXD2 33
  #define TXD2 32
  #define BATT_PIN 36

  // --- Heltec V2 Internal LoRa Pins ---
  #define LORA_SCK 5
  #define LORA_MISO 19
  #define LORA_MOSI 27
  #define LORA_NSS 18
  #define LORA_RST 14
  #define LORA_DIO0 26
  
  #define GPS_BAUD 9600
#endif

// --- Tuning Parameters ---
#define LORA_FREQ 868E6
const int LORA_TX_POWER_DBM = 20;
const int LORA_SPREADING_FACTOR = 10;
const long LORA_SIGNAL_BANDWIDTH = 125E3;
const int LORA_CODING_RATE = 5;
const int LORA_PREAMBLE_LENGTH = 8;
const int LORA_SYNC_WORD = 0x12;
const uint16_t HISTORY_SIZE = 500; // Total logical points in chain, including origin
const double MIN_DISTANCE_METERS = 2.5;
const double MIN_SPEED_KMPH = 0.5;
const double MAX_HDOP = 2.0;
const uint32_t MIN_SATELLITES = 6;

// --- Anti-glitch / timing ---
const double MAX_SPEED_MPS = 20.0;
const uint32_t MAX_FIX_AGE_S = 43200;
const double SAVE_DIST_THRESHOLD = 250.0;
const uint32_t SAVE_CYCLE_THRESHOLD = 60;
const uint32_t GPS_TIMEOUT_MS = 60000;
const uint32_t GPS_LISTEN_WINDOW_MS = 2500;

// --- History encoding ---
const double HISTORY_POINT_SPACING_M = 15.0;   // only store a history point every 15m
const int32_t DELTA_UNIT_MICRODEG = 10;        // 1e-5 degree steps expressed in microdegrees
const uint32_t SECONDS_PER_DAY = 86400;

// --- Battery ---
const float MIN_BATTERY_VOLTAGE = 3.2f;
const float MAX_BATTERY_VOLTAGE = 4.2f;
const bool BATTERY_SENSE_ENABLED = true;

// --- Sleep ---
const uint64_t SLEEP_DURATION_US = 60000000;

// --- Display / setup responsiveness ---
const uint8_t WIFI_MAX_CONNECT_ATTEMPTS = 5;
const uint32_t WIFI_CONNECT_TIMEOUT_MS = 10000;
const uint32_t WEB_CLIENT_IDLE_TIMEOUT_MS = 15000;
const uint32_t DISPLAY_BUTTON_TIMEOUT_MS = 20000;
const uint32_t DISPLAY_REFRESH_MS = 5000;
const uint32_t DISPLAY_PAGE_INTERVAL_MS = 10000;
const uint32_t DISPLAY_BATTERY_REFRESH_MS = 5000;
const uint8_t DISPLAY_PAGE_COUNT = 4;

// --- UBX Command to put BN-220 to Sleep ---
const byte UBX_SLEEP_CMD[] = {
  0xB5, 0x62, 0x02, 0x41, 0x08, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x4D, 0x3B
};

// =====================================================
// PAYLOAD FORMAT
// =====================================================
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

struct DeltaPoint {
  int8_t dlat;
  int8_t dlon;
} __attribute__((packed));

// =====================================================
// INTERNAL RTC HISTORY
// =====================================================
struct StoredHistoryPoint {
  int32_t lat;     // microdegrees
  int32_t lon;     // microdegrees
  uint32_t seq;    // monotonic point sequence
} __attribute__((packed));

// =====================================================
// GLOBALS / RTC STATE
// =====================================================
TinyGPSPlus gps;
Preferences prefs;

RTC_DATA_ATTR double total_distance_meters = 0.0;
RTC_DATA_ATTR double last_lat = 0.0;
RTC_DATA_ATTR double last_lng = 0.0;
RTC_DATA_ATTR bool has_initial_fix = false;

RTC_DATA_ATTR StoredHistoryPoint history_points[HISTORY_SIZE];
RTC_DATA_ATTR uint16_t history_head = 0;   // next write index
RTC_DATA_ATTR uint16_t history_count = 0;  // valid points in ring

RTC_DATA_ATTR uint32_t seconds_since_last_fix = 0;
RTC_DATA_ATTR uint8_t teleport_strikes = 0;
RTC_DATA_ATTR uint32_t wakeup_counter = 0;
RTC_DATA_ATTR double last_saved_dist = -1.0;
RTC_DATA_ATTR uint32_t seconds_since_daily_reset = 0;
RTC_DATA_ATTR uint32_t next_point_seq = 0; // survives deep sleep
RTC_DATA_ATTR uint64_t target_wakeup_time_us = 0;
RTC_DATA_ATTR uint64_t session_start_time_us = 0; // wall-clock uptime across deep sleep

uint32_t boot_id = 0; // loaded/incremented from NVS on hard boot

// =====================================================
// LOGGING & DEBUG STATE
// =====================================================
const uint8_t LOG_HISTORY_LINES = 25; // increased for web logging capability
const size_t LOG_HISTORY_LINE_LENGTH = 160;
char logHistory[LOG_HISTORY_LINES][LOG_HISTORY_LINE_LENGTH];
uint8_t logHistoryHead = 0;
uint8_t logHistoryCount = 0;

WebServer webServer(80);
bool debug_mode = false;
bool force_tracking_mode = false; // flag to break out of wifi setup mode
bool wifi_client_connected = false;
uint32_t last_web_activity_ms = 0;
bool ble_debug_enabled = false;

// --- Live tracker/debug state shown on the display ---
const char* tracker_phase = "Boot";
char last_error[48] = "none";
esp_reset_reason_t last_reset_reason = ESP_RST_UNKNOWN;

bool wifi_setup_active = false;
bool wifi_station_connected = false;
bool wifi_ap_active = false;
uint8_t wifi_connect_attempt = 0;
String last_wifi_ip = "off";
int32_t last_wifi_rssi = 0;

bool ble_advertising = false;
bool lora_initialized = false;
int8_t last_tx_status = -1;   // -1 unknown, 0 failed, 1 successful
int8_t last_ack_status = -1;  // -1 unknown, 0 missing/invalid, 1 successful
uint16_t last_tx_bytes = 0;
uint16_t last_tx_points = 0;
uint32_t last_acked_seq = 0;
float last_ack_rssi = NAN;
float last_ack_snr = NAN;
uint32_t last_cycle_duration_ms = 0;

bool display_initialized = false;
bool display_awake = false;
uint32_t display_on_until_ms = 0;
uint32_t display_last_refresh_ms = 0;
uint32_t display_last_page_change_ms = 0;
uint8_t display_page = 0;
bool previous_button_pressed = false;
uint32_t last_button_event_ms = 0;
float cached_battery_voltage = 0.0f;
uint8_t cached_battery_percentage = 0;
uint32_t cached_battery_read_ms = 0;

uint32_t button_press_start_ms = 0;
bool button_is_held = false;
uint32_t button_hold_duration_ms = 0;

enum class ConfirmationState { NONE, DISTANCE_RESET, BLE_TOGGLE, FACTORY_RESET };
ConfirmationState pending_confirmation = ConfirmationState::NONE;
uint32_t confirmation_timeout_ms = 0;



// Forward declarations: these are also used by WiFi/GPS/LoRa wait loops.
bool isDebugModeActive();
void setTrackerPhase(const char* phase);
void setLastError(const char* error);
void requestDisplayWake(uint32_t duration_ms = DISPLAY_BUTTON_TIMEOUT_MS, bool advance_page = false);
void serviceDisplayAndButton(bool force_refresh = false);
void responsiveDelay(uint32_t duration_ms);

// --- BLE Globals ---
#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E" // UART service UUID
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

BLEServer *pServer = NULL;
BLECharacteristic *pTxCharacteristic;
bool deviceConnected = false;

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        deviceConnected = true;
        ble_advertising = false;
        debug_mode = true; // Keep the device awake while a BLE debug client is attached.
    };
    void onDisconnect(BLEServer* pServer) {
        deviceConnected = false;
        debug_mode = false;
        ble_advertising = true;
        pServer->startAdvertising(); // Restart advertising so clients can reconnect later
    }
};

// =====================================================
// HELPERS
// =====================================================
bool isDebugModeActive() {
  return debug_mode || ble_debug_enabled;
}

void setTrackerPhase(const char* phase) {
  tracker_phase = (phase && phase[0]) ? phase : "Unknown";
}

void setLastError(const char* error) {
  if (!error || !error[0]) {
    strlcpy(last_error, "none", sizeof(last_error));
    return;
  }
  strlcpy(last_error, error, sizeof(last_error));
}

void responsiveDelay(uint32_t duration_ms) {
  const uint32_t start_ms = millis();
  while ((uint32_t)(millis() - start_ms) < duration_ms) {
    serviceDisplayAndButton();
    delay(10);
  }
}

void rememberLogLine(const char* message) {
  if (!message || !message[0]) return;
  size_t start = 0;
  const size_t messageLength = strlen(message);
  while (start < messageLength) {
    size_t end = start;
    while (end < messageLength && message[end] != '\n' && message[end] != '\r') end++;
    if (end > start) {
      size_t lineLength = end - start;
      if (lineLength >= LOG_HISTORY_LINE_LENGTH) lineLength = LOG_HISTORY_LINE_LENGTH - 1;
      memcpy(logHistory[logHistoryHead], message + start, lineLength);
      logHistory[logHistoryHead][lineLength] = '\0';
      logHistoryHead = (logHistoryHead + 1) % LOG_HISTORY_LINES;
      if (logHistoryCount < LOG_HISTORY_LINES) logHistoryCount++;
    }
    while (end < messageLength && (message[end] == '\n' || message[end] == '\r')) end++;
    start = end;
  }
}

template<typename T>
void logPrint(const T& message) {
  String s = String(message);
  Serial.print(s);
  rememberLogLine(s.c_str());
  if (deviceConnected && pTxCharacteristic) {
    pTxCharacteristic->setValue(s.c_str());
    pTxCharacteristic->notify();
    delay(5);
  }
}

template<typename T>
void logPrintln(const T& message) {
  String s = String(message) + "\n";
  Serial.print(s);
  rememberLogLine(s.c_str());
  if (deviceConnected && pTxCharacteristic) {
    String cleanS = s;
    cleanS.replace("\r\n", "\n");
    cleanS.replace("\n", "\r\n");
    pTxCharacteristic->setValue(cleanS.c_str());
    pTxCharacteristic->notify();
    delay(5);
  }
}

void logPrintln() {
  logPrintln("");
}

void logPrintf(const char* format, ...) {
  char buffer[384];
  va_list args;
  va_start(args, format);
  const int written = vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  if (written <= 0) return;
  Serial.print(buffer);
  rememberLogLine(buffer);
  if (deviceConnected && pTxCharacteristic) {
    String cleanS = String(buffer);
    cleanS.replace("\r\n", "\n");
    cleanS.replace("\n", "\r\n");
    pTxCharacteristic->setValue(cleanS.c_str());
    pTxCharacteristic->notify();
    delay(5);
  }
}


float getBatteryVoltage();
uint8_t getBatteryPercentage();

void logRTCState() {
  logPrint("RTC state -> has_fix: ");
  logPrint(has_initial_fix ? "true" : "false");
  float v = getBatteryVoltage();
  uint8_t p = getBatteryPercentage();
  logPrintf(", dist: %.2f m, age: %u s, daily_reset_age: %u s, strikes: %u, hist_count: %u, next_seq: %lu, batt: %.2fV (%u%%)\n",
                total_distance_meters,
                seconds_since_last_fix,
                seconds_since_daily_reset,
                teleport_strikes,
                history_count,
                (unsigned long)next_point_seq,
                v, p);
}

float getBatteryVoltage() {
  if (!BATTERY_SENSE_ENABLED) return 0.0f;
  float voltage = 0.0f;
#if defined(BOARD_WIRELESS_TRACKER)
  pinMode(ADC_CTRL, OUTPUT);
  digitalWrite(ADC_CTRL, HIGH); // Active-HIGH to enable voltage divider on Wireless Tracker
  delay(50);                    // Wait for voltage to stabilize
  uint32_t millivolts = analogReadMilliVolts(BATT_PIN);
  digitalWrite(ADC_CTRL, LOW);  // Disable to save power
  voltage = (millivolts / 1000.0f) * 4.9f;
#else
  uint32_t millivolts = analogReadMilliVolts(BATT_PIN);
  voltage = (millivolts * 2.0f) / 1000.0f;
#endif
  return voltage;
}

uint8_t getBatteryPercentage() {
  float voltage = getBatteryVoltage();
  if (voltage == 0.0f) return 0;
  int pct = (int)(((voltage - MIN_BATTERY_VOLTAGE) * 100.0f) / (MAX_BATTERY_VOLTAGE - MIN_BATTERY_VOLTAGE));
  return (uint8_t)constrain(pct, 0, 100);
}

void wakeupGPS() {
#if defined(BOARD_WIRELESS_TRACKER)
  logPrintln("Powering up GNSS (Wireless Tracker)...");
  pinMode(VEXT_CTRL_PIN, OUTPUT);
  digitalWrite(VEXT_CTRL_PIN, HIGH);
  pinMode(GNSS_PWR_PIN, OUTPUT);
  digitalWrite(GNSS_PWR_PIN, HIGH);
  pinMode(GNSS_RST_PIN, OUTPUT);
  digitalWrite(GNSS_RST_PIN, HIGH);
#else
  logPrintln("Waking up GNSS (sending wakeup byte)...");
  Serial2.write(0xFF); // Wake up BN-220
#endif
  responsiveDelay(500);
}

void sleepGPS() {
#if defined(BOARD_WIRELESS_TRACKER)
  logPrintln("Powering down GNSS (Wireless Tracker)...");
  digitalWrite(GNSS_PWR_PIN, LOW);
  digitalWrite(GNSS_RST_PIN, LOW);
  digitalWrite(VEXT_CTRL_PIN, LOW);

  // TFT and GNSS share VEXT on the Wireless Tracker. Once VEXT is cut,
  // the TFT controller has lost state and must be initialized again.
  display_initialized = false;
  display_awake = false;
#else
  logPrintln("Sending UBX Sleep Command to GPS...");
  for (size_t i = 0; i < sizeof(UBX_SLEEP_CMD); i++) {
    Serial2.write(UBX_SLEEP_CMD[i]);
  }
  Serial2.flush();
#endif
  delay(100);
}

#if defined(BOARD_WIRELESS_TRACKER)
// Keep LoRa on the ESP32-S3 FSPI bus. TFT_eSPI uses the HSPI bus;
// separating them allows the TFT to function properly.
SPIClass loraSPI(FSPI);
SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY, loraSPI);
#endif

bool initLoRaRadio() {
#if defined(BOARD_WIRELESS_TRACKER)
  loraSPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
  
  float freq = LORA_FREQ / 1000000.0;
  float bw = LORA_SIGNAL_BANDWIDTH / 1000.0;
  
  logPrintf("Initializing RadioLib SX1262 -> freq=%.1fMHz bw=%.1fkHz sf=%d cr=%d sync=0x%02X power=%d tcxo=1.6V\n",
            freq, bw, LORA_SPREADING_FACTOR, LORA_CODING_RATE, LORA_SYNC_WORD, LORA_TX_POWER_DBM);

  int state = radio.begin(freq, bw, LORA_SPREADING_FACTOR, LORA_CODING_RATE, LORA_SYNC_WORD, LORA_TX_POWER_DBM, LORA_PREAMBLE_LENGTH, 1.6, false);
  if (state == RADIOLIB_ERR_NONE) {
    radio.setCRC(true);
    return true;
  }
  logPrintf("RadioLib SX1262 init failed, code: %d\n", state);
  return false;
#else
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
  LoRa.setPins(LORA_NSS, LORA_RST, LORA_DIO0);
  if (!LoRa.begin(LORA_FREQ)) {
    return false;
  }
  
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
  return true;
#endif
}

void sleepLoRaRadio() {
#if defined(BOARD_WIRELESS_TRACKER)
  radio.sleep();
#else
  LoRa.sleep();
#endif
}

bool transmitLoRaPacket(uint8_t* buffer, size_t len) {
#if defined(BOARD_WIRELESS_TRACKER)
  int state = radio.transmit(buffer, len);
  return (state == RADIOLIB_ERR_NONE);
#else
  LoRa.beginPacket();
  LoRa.write(buffer, len);
  return (LoRa.endPacket() == 1);
#endif
}

int receiveLoRaAck(uint8_t* buffer, size_t max_len, uint32_t timeout_ms) {
#if defined(BOARD_WIRELESS_TRACKER)
  int state = radio.startReceive();
  if (state != RADIOLIB_ERR_NONE) {
    logPrintf("RadioLib startReceive failed, code: %d\n", state);
    return -1;
  }
  
  unsigned long start = millis();
  while (millis() - start < timeout_ms) {
    uint16_t irq = radio.getIrqStatus();
    if (irq & RADIOLIB_SX126X_IRQ_RX_DONE) {
      size_t length = radio.getPacketLength();
      if (length > max_len) {
        length = max_len;
      }
      state = radio.readData(buffer, length);
      if (state == RADIOLIB_ERR_NONE) {
        last_ack_rssi = radio.getRSSI();
        last_ack_snr = radio.getSNR();
        return length;
      } else {
        logPrintf("RadioLib readData failed, code: %d\n", state);
        return -1;
      }
    } else if (irq & RADIOLIB_SX126X_IRQ_CRC_ERR) {
      logPrintln("RadioLib CRC error during ACK receive");
      return -1;
    } else if (irq & RADIOLIB_SX126X_IRQ_TIMEOUT) {
      return 0; // timed out
    }
    serviceDisplayAndButton();
    delay(10);
  }
  return 0; // timed out
#else
  LoRa.receive();
  unsigned long start = millis();
  while (millis() - start < timeout_ms) {
    int packetSize = LoRa.parsePacket();
    if (packetSize) {
      if (packetSize > (int)max_len) {
        LoRa.readBytes(buffer, max_len);
        last_ack_rssi = LoRa.packetRssi();
        last_ack_snr = LoRa.packetSnr();
        return max_len;
      } else {
        LoRa.readBytes(buffer, packetSize);
        last_ack_rssi = LoRa.packetRssi();
        last_ack_snr = LoRa.packetSnr();
        return packetSize;
      }
    }
    serviceDisplayAndButton();
    delay(10);
  }
  return 0; // timed out
#endif
}

void runWifiSetupMode() {
  wifi_setup_active = true;
  wifi_station_connected = false;
  wifi_ap_active = false;
  wifi_connect_attempt = 0;
  force_tracking_mode = false;
  wifi_client_connected = false;
  setTrackerPhase("WiFi setup");

  // WiFi is only enabled in this bounded hard-boot setup window.
  // Disabling modem sleep here improves association reliability; WiFi is
  // completely shut down again before normal battery-powered tracking.
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);

  String savedSsid = prefs.isKey("ssid") ? prefs.getString("ssid") : String(ssid);
  String savedPw = prefs.isKey("pw") ? prefs.getString("pw") : String(password);
  ble_debug_enabled = prefs.getBool("ble_log", false);

  bool connected = false;
  if (savedSsid.length() > 0) {
    for (uint8_t attempt = 1; attempt <= WIFI_MAX_CONNECT_ATTEMPTS && !connected; attempt++) {
      wifi_connect_attempt = attempt;
      setTrackerPhase("WiFi connect");
      logPrintf("WiFi connection attempt %u/%u to '%s'...\n",
                attempt, WIFI_MAX_CONNECT_ATTEMPTS, savedSsid.c_str());

      WiFi.disconnect(false, false);
      responsiveDelay(250);
      WiFi.begin(savedSsid.c_str(), savedPw.c_str());

      const uint32_t wifi_start = millis();
      uint32_t last_dot_ms = 0;
      while (WiFi.status() != WL_CONNECTED &&
             (uint32_t)(millis() - wifi_start) < WIFI_CONNECT_TIMEOUT_MS) {
        if ((uint32_t)(millis() - last_dot_ms) >= 500) {
          Serial.print(".");
          last_dot_ms = millis();
        }
        serviceDisplayAndButton();
        delay(50);
      }
      Serial.println();

      if (WiFi.status() == WL_CONNECTED) {
        connected = true;
        setLastError(nullptr);
        wifi_station_connected = true;
        wifi_ap_active = false;
        last_wifi_ip = WiFi.localIP().toString();
        last_wifi_rssi = WiFi.RSSI();
        setTrackerPhase("WiFi connected");
        logPrintf("WiFi connected on attempt %u. IP: %s, RSSI: %ld dBm\n",
                  attempt, last_wifi_ip.c_str(), (long)last_wifi_rssi);
      } else {
        logPrintf("WiFi attempt %u failed (status=%d).\n", attempt, (int)WiFi.status());
        setLastError("WiFi association failed");
        if (attempt < WIFI_MAX_CONNECT_ATTEMPTS) {
          responsiveDelay(500);
        }
      }
    }
  } else {
    setLastError("No WiFi SSID configured");
  }

  if (!connected) {
    setTrackerPhase("WiFi fallback AP");
    logPrintln("Starting fallback AP mode...");
    WiFi.disconnect(false, false);
    WiFi.mode(WIFI_AP);
    if (WiFi.softAP("EquineTracker", "WannWieviel!4")) {
      wifi_ap_active = true;
      wifi_station_connected = false;
      responsiveDelay(500);
      last_wifi_ip = WiFi.softAPIP().toString();
      last_wifi_rssi = 0;
      setLastError(savedSsid.length() > 0 ? "STA failed; fallback AP" : "No SSID; fallback AP");
      logPrintln("AP started. IP: " + last_wifi_ip);
    } else {
      setLastError("Fallback AP failed");
      logPrintln("Fallback AP failed.");
    }
  }

  ArduinoOTA.setHostname("equine-tracker");
  ArduinoOTA.begin();

  webServer.on("/", HTTP_GET, []() {
    wifi_client_connected = true;
    last_web_activity_ms = millis();
    logPrintln("Web UI accessed by a client.");
    String html = "<html><head><title>Equine Tracker</title><meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>body{font-family:sans-serif;margin:20px;} input[type=text],input[type=password]{padding:8px;width:100%;box-sizing:border-box;}</style></head><body>";
    html += "<h1>Equine Tracker Setup</h1>";
    html += "<form action='/save' method='POST'>";
    html += "SSID:<br><input type='text' name='ssid' value='" + (prefs.isKey("ssid") ? prefs.getString("ssid") : String(ssid)) + "'><br><br>";
    html += "Password:<br><input type='password' name='pw' value='" + (prefs.isKey("pw") ? prefs.getString("pw") : String(password)) + "'><br><br>";
    html += "<input type='checkbox' name='ble_log' value='1' ";
    if(ble_debug_enabled) html += "checked";
    html += "> Enable BLE Debug Logs<br>";
    html += "<small style='color:red;'><b>Warning:</b> Enabling BLE debugging keeps the device awake and the display on, significantly reducing battery life.</small><br><br>";
    html += "<input type='submit' value='Save Settings' style='padding:10px;background:#007BFF;color:white;border:none;border-radius:4px;'></form>";
    html += "<hr><form action='/start' method='POST'><input type='submit' value='Turn Off WiFi & Start Tracking' style='padding:10px;background:#28A745;color:white;border:none;border-radius:4px;width:100%;'></form>";
    html += "<hr><h3>Live Logs</h3><pre id='logs' style='background:#f4f4f4;padding:10px;max-height:300px;overflow-y:auto;'>Loading...</pre>";
    html += "<script>setInterval(function(){ fetch('/logs').then(r=>r.text()).then(t=>document.getElementById('logs').innerText=t); }, 2000);</script>";
    html += "</body></html>";
    webServer.send(200, "text/html", html);
  });

  webServer.on("/save", HTTP_POST, []() {
    wifi_client_connected = true;
    last_web_activity_ms = millis();
    String newSsid = webServer.arg("ssid");
    String newPw = webServer.arg("pw");
    bool newBle = webServer.hasArg("ble_log");
    prefs.putString("ssid", newSsid);
    prefs.putString("pw", newPw);
    prefs.putBool("ble_log", newBle);
    ble_debug_enabled = newBle;
    logPrintf("Settings saved! SSID updated. BLE Debug is now: %s\n", newBle ? "ON" : "OFF");
    webServer.sendHeader("Location", "/");
    webServer.send(303);
  });

  webServer.on("/start", HTTP_POST, []() {
    wifi_client_connected = true;
    last_web_activity_ms = millis();
    force_tracking_mode = true;
    webServer.send(200, "text/html", "Starting tracking mode. You can close this page.<br><br><small>WiFi will now disconnect.</small>");
  });

  webServer.on("/logs", HTTP_GET, []() {
    wifi_client_connected = true;
    last_web_activity_ms = millis();
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
  setTrackerPhase(connected ? "OTA / web STA" : "OTA / web AP");
  logPrintln("Ready for OTA or Web UI on port 80.");

  const uint32_t ota_start_time = millis();
  const uint32_t timeout_window = 30000;

  while (!force_tracking_mode) {
    const uint32_t now = millis();
    const bool initial_setup_window = (uint32_t)(now - ota_start_time) < timeout_window;
    const bool recent_web_activity = wifi_client_connected &&
      (uint32_t)(now - last_web_activity_ms) < WEB_CLIENT_IDLE_TIMEOUT_MS;

    if (!initial_setup_window && !recent_web_activity) {
      wifi_client_connected = false;
      break;
    }

    ArduinoOTA.handle();
    webServer.handleClient();

    if (wifi_client_connected &&
        (uint32_t)(millis() - last_web_activity_ms) >= WEB_CLIENT_IDLE_TIMEOUT_MS) {
      wifi_client_connected = false;
    }
    if (wifi_station_connected && WiFi.status() == WL_CONNECTED) {
      last_wifi_rssi = WiFi.RSSI();
    }
    serviceDisplayAndButton();
    delay(10);
  }

  logPrintln("\nWiFi setup window closed. Disconnecting WiFi and resuming normal operation.");
  webServer.stop();
  WiFi.disconnect(true);
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  wifi_setup_active = false;
  wifi_station_connected = false;
  wifi_ap_active = false;
  setTrackerPhase("WiFi off");
  delay(100);

  if (!isDebugModeActive()) {
    // Normal mode: the display is now allowed to sleep until the button is used.
    display_on_until_ms = 0;
    serviceDisplayAndButton(true);
  }
}

uint16_t historyIndexToRing(uint16_t logicalIndex) {
  // logicalIndex 0 = oldest, history_count-1 = newest
  return (history_head + HISTORY_SIZE - history_count + logicalIndex) % HISTORY_SIZE;
}

const StoredHistoryPoint& getNewestHistoryPoint() {
  uint16_t idx = historyIndexToRing(history_count - 1);
  return history_points[idx];
}

void clearHistory() {
  history_head = 0;
  history_count = 0;
  memset(history_points, 0, sizeof(history_points));
}

void resetDailyDistanceAndHistory() {
  logPrintln("Daily reset: clearing distance accumulator and history chain.");
  total_distance_meters = 0.0;
  last_saved_dist = 0.0;
  clearHistory();
  seconds_since_daily_reset = 0;
  prefs.putDouble("dist", 0.0);
  prefs.putUInt("daily_age", 0);
}

void appendHistoryPointAbsolute(int32_t lat_micro, int32_t lon_micro) {
  StoredHistoryPoint p;
  p.lat = lat_micro;
  p.lon = lon_micro;
  p.seq = next_point_seq++;

  history_points[history_head] = p;
  history_head = (history_head + 1) % HISTORY_SIZE;
  if (history_count < HISTORY_SIZE) {
    history_count++;
  } else {
    logPrintln("Warning: History buffer is full! Oldest unacknowledged point will be overwritten.");
  }

  logPrintf("History append -> seq=%lu lat=%ld lon=%ld count=%u\n",
                (unsigned long)p.seq, (long)p.lat, (long)p.lon, history_count);
}

int32_t quantizeMicrodegToStepGrid(int32_t value_micro) {
  // Round to nearest DELTA_UNIT_MICRODEG to keep chain reconstructable exactly
  if (value_micro >= 0) {
    return ((value_micro + (DELTA_UNIT_MICRODEG / 2)) / DELTA_UNIT_MICRODEG) * DELTA_UNIT_MICRODEG;
  } else {
    return ((value_micro - (DELTA_UNIT_MICRODEG / 2)) / DELTA_UNIT_MICRODEG) * DELTA_UNIT_MICRODEG;
  }
}

void maybeAppendSignificantHistoryPoint(double lat_deg, double lon_deg) {
  int32_t lat_micro = (int32_t)lround(lat_deg * 1000000.0);
  int32_t lon_micro = (int32_t)lround(lon_deg * 1000000.0);

  // Quantize all stored points to the delta step grid so packet reconstruction is exact.
  lat_micro = quantizeMicrodegToStepGrid(lat_micro);
  lon_micro = quantizeMicrodegToStepGrid(lon_micro);

  if (history_count == 0) {
    appendHistoryPointAbsolute(lat_micro, lon_micro);
    return;
  }

  const StoredHistoryPoint& newest = getNewestHistoryPoint();

  double dist_from_last_stored = TinyGPSPlus::distanceBetween(
    newest.lat / 1000000.0,
    newest.lon / 1000000.0,
    lat_micro / 1000000.0,
    lon_micro / 1000000.0
  );

  if (dist_from_last_stored < HISTORY_POINT_SPACING_M) {
    logPrintf("History skip -> only %.2f m from last stored point\n", dist_from_last_stored);
    return;
  }

  appendHistoryPointAbsolute(lat_micro, lon_micro);
}

uint16_t buildDynamicPayload(uint8_t* buffer, size_t max_len, uint16_t& points_packed) {
  if (history_count == 0) return 0;

  points_packed = 0;
  const StoredHistoryPoint& root = history_points[historyIndexToRing(0)];

  LoRaHeader header;
  header.boot_id = boot_id;
  header.first_seq = root.seq;
  header.total_dist_dam = (uint16_t)(total_distance_meters / 10.0);
  header.batt_pct = getBatteryPercentage();

  AnchorPoint anchors[32];
  uint8_t num_anchors = 0;

  // We write directly to temporary batch array
  uint8_t batches_buf[250];
  size_t batches_len = 0;
  uint8_t num_batches = 0;

  uint8_t current_anchor_idx = 255; 
  uint32_t current_batch_count_ptr = 0; // index in batches_buf of the count

  points_packed = 1; // root point is packed

  for (uint16_t i = 1; i < history_count; i++) {
    const StoredHistoryPoint& p = history_points[historyIndexToRing(i)];

    int32_t dlat_micro = p.lat - root.lat;
    int32_t dlon_micro = p.lon - root.lon;

    int32_t dlat_steps = dlat_micro / DELTA_UNIT_MICRODEG;
    int32_t dlon_steps = dlon_micro / DELTA_UNIT_MICRODEG;

    if (dlat_steps < -32768 || dlat_steps > 32767 || dlon_steps < -32768 || dlon_steps > 32767) {
      // 36km exceeded! Stop packet here. Next packet will re-root.
      break; 
    }

    // Try to find an anchor that works
    uint8_t chosen_anchor = 255;
    for (uint8_t a = 0; a < num_anchors; a++) {
      int32_t rel_lat = dlat_steps - anchors[a].dlat;
      int32_t rel_lon = dlon_steps - anchors[a].dlon;
      if (rel_lat >= -128 && rel_lat <= 127 && rel_lon >= -128 && rel_lon <= 127) {
        chosen_anchor = a;
        break;
      }
    }

    bool needs_new_anchor = (chosen_anchor == 255);
    if (needs_new_anchor && num_anchors >= 32) {
      break; // too many anchors, stop packet here
    }

    uint8_t target_anchor_idx = needs_new_anchor ? num_anchors : chosen_anchor;
    
    // We start a new batch if the target anchor differs, if no batches exist yet,
    // or if the current batch count has reached 255 items.
    bool needs_new_batch = (current_anchor_idx != target_anchor_idx || num_batches == 0 || batches_buf[current_batch_count_ptr] == 255);

    // Calculate exact final payload size if we include this point
    size_t base_len = sizeof(LoRaHeader) + 8 + 1 + 1; // Header + Root + num_anchors + num_batches
    size_t projected_anchor_count = num_anchors + (needs_new_anchor ? 1 : 0);
    size_t projected_batches_len = batches_len + (needs_new_batch ? 2 : 0) + 2;

    size_t projected_total_len = base_len + (projected_anchor_count * sizeof(AnchorPoint)) + projected_batches_len;

    if (projected_total_len > max_len) {
      break;
    }
    if (projected_batches_len > sizeof(batches_buf)) {
      break;
    }

    // Now execute state mutations since we are guaranteed to fit
    if (needs_new_anchor) {
      chosen_anchor = num_anchors;
      anchors[num_anchors].dlat = (int16_t)dlat_steps;
      anchors[num_anchors].dlon = (int16_t)dlon_steps;
      num_anchors++;
    }

    if (needs_new_batch) {
      current_anchor_idx = target_anchor_idx;
      batches_buf[batches_len++] = target_anchor_idx;
      current_batch_count_ptr = batches_len;
      batches_buf[batches_len++] = 0; // count to be incremented
      num_batches++;
    }

    int8_t rel_lat = (int8_t)(dlat_steps - anchors[chosen_anchor].dlat);
    int8_t rel_lon = (int8_t)(dlon_steps - anchors[chosen_anchor].dlon);
    
    batches_buf[batches_len++] = rel_lat;
    batches_buf[batches_len++] = rel_lon;
    batches_buf[current_batch_count_ptr]++;

    points_packed++;
  }

  #define REQUIRE_SPACE(n) do { \
    if (offset + (n) > max_len) return 0; \
  } while (0)

  // Now assemble the final payload
  size_t offset = 0;
  
  REQUIRE_SPACE(sizeof(header));
  memcpy(buffer + offset, &header, sizeof(header));
  offset += sizeof(header);

  REQUIRE_SPACE(8);
  memcpy(buffer + offset, &root.lat, 4); offset += 4;
  memcpy(buffer + offset, &root.lon, 4); offset += 4;

  REQUIRE_SPACE(1);
  buffer[offset++] = num_anchors;
  
  if (num_anchors > 0) {
    REQUIRE_SPACE(num_anchors * sizeof(AnchorPoint));
    memcpy(buffer + offset, anchors, num_anchors * sizeof(AnchorPoint));
    offset += num_anchors * sizeof(AnchorPoint);
  }

  REQUIRE_SPACE(1);
  buffer[offset++] = num_batches;

  if (batches_len > 0) {
    REQUIRE_SPACE(batches_len);
    memcpy(buffer + offset, batches_buf, batches_len);
    offset += batches_len;
  }

  #undef REQUIRE_SPACE
  return offset;
}

// =====================================================
// DISPLAY
// =====================================================
const char* resetReasonName(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:   return "power";
    case ESP_RST_EXT:       return "external";
    case ESP_RST_SW:        return "software";
    case ESP_RST_PANIC:     return "panic";
    case ESP_RST_INT_WDT:   return "int-wdt";
    case ESP_RST_TASK_WDT:  return "task-wdt";
    case ESP_RST_WDT:       return "wdt";
    case ESP_RST_DEEPSLEEP: return "deep-sleep";
    case ESP_RST_BROWNOUT:  return "brownout";
    default:                return "unknown";
  }
}

uint32_t getSessionUptimeSeconds() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  const uint64_t now_us = (uint64_t)tv.tv_sec * 1000000ULL + tv.tv_usec;
  if (session_start_time_us > 0 && now_us >= session_start_time_us) {
    return (uint32_t)((now_us - session_start_time_us) / 1000000ULL);
  }
  return millis() / 1000;
}

void formatDuration(uint32_t total_seconds, char* out, size_t out_size) {
  const uint32_t days = total_seconds / 86400;
  const uint32_t hours = (total_seconds % 86400) / 3600;
  const uint32_t minutes = (total_seconds % 3600) / 60;
  const uint32_t seconds = total_seconds % 60;

  if (days > 0) {
    snprintf(out, out_size, "%lud%02luh", (unsigned long)days, (unsigned long)hours);
  } else if (hours > 0) {
    snprintf(out, out_size, "%luh%02lum", (unsigned long)hours, (unsigned long)minutes);
  } else {
    snprintf(out, out_size, "%lum%02lus", (unsigned long)minutes, (unsigned long)seconds);
  }
}

const char* triStateName(int8_t value) {
  if (value > 0) return "OK";
  if (value == 0) return "FAIL";
  return "--";
}

void refreshDisplayBatteryCache(bool force) {
  const uint32_t now = millis();
  if (!force && cached_battery_read_ms != 0 &&
      (uint32_t)(now - cached_battery_read_ms) < DISPLAY_BATTERY_REFRESH_MS) {
    return;
  }

  cached_battery_voltage = getBatteryVoltage();
  if (cached_battery_voltage > 0.0f) {
    int pct = (int)(((cached_battery_voltage - MIN_BATTERY_VOLTAGE) * 100.0f) /
                    (MAX_BATTERY_VOLTAGE - MIN_BATTERY_VOLTAGE));
    cached_battery_percentage = (uint8_t)constrain(pct, 0, 100);
  } else {
    cached_battery_percentage = 0;
  }
  cached_battery_read_ms = now;
}

void initDisplay() {
  if (display_initialized) return;
#if defined(BOARD_WIRELESS_TRACKER)
  // VEXT powers the TFT on the Wireless Tracker. It must be high before
  // tft.init(); otherwise the init commands are sent to an unpowered panel.
  pinMode(VEXT_CTRL_PIN, OUTPUT);
  digitalWrite(VEXT_CTRL_PIN, HIGH);
  delay(25);

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, LOW); // Ensure off BEFORE init
  tft.init();
  tft.setRotation(1);
  tft.setTextWrap(false);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.fillScreen(TFT_BLACK);
#else
  u8g2.begin();
  u8g2.setFont(u8g2_font_7x14_tr);
  u8g2.clearBuffer();
  u8g2.sendBuffer();
#endif
  display_initialized = true;
  display_awake = false;
}

void turnOnDisplay() {
  if (!display_initialized) {
    initDisplay();
  }
  if (display_awake) return;
  display_awake = true;

#if defined(BOARD_WIRELESS_TRACKER)
  tft.writecommand(ST7735_SLPOUT);
  delay(5);
  digitalWrite(TFT_BL, HIGH);
#else
  u8g2.setPowerSave(0);
#endif
}

void turnOffDisplay() {
  if (!display_initialized || !display_awake) return;
  display_awake = false;

#if defined(BOARD_WIRELESS_TRACKER)
  digitalWrite(TFT_BL, LOW);
  tft.writecommand(ST7735_SLPIN);
#else
  u8g2.setPowerSave(1);
#endif
  display_awake = false;
}

void renderDisplayLines(char lines[5][18]) {
#if defined(BOARD_WIRELESS_TRACKER)
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  for (uint8_t i = 0; i < 5; i++) {
    tft.setCursor(0, i * 16);
    tft.print(lines[i]);
  }
#else
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_7x14_tr);
  for (uint8_t i = 0; i < 5; i++) {
    u8g2.drawStr(0, 12 + (i * 13), lines[i]);
  }
  u8g2.sendBuffer();
#endif
}

void renderStatusPage() {
  char lines[5][18] = {};
  refreshDisplayBatteryCache(false);

  // Check confirmation states first
  if (pending_confirmation != ConfirmationState::NONE) {
    uint32_t now = millis();
    if (now > confirmation_timeout_ms) {
      pending_confirmation = ConfirmationState::NONE;
    } else {
      uint32_t remaining_s = (confirmation_timeout_ms - now + 999) / 1000;
      if (pending_confirmation == ConfirmationState::DISTANCE_RESET) {
        snprintf(lines[0], sizeof(lines[0]), "CONFIRM DIST");
      } else if (pending_confirmation == ConfirmationState::FACTORY_RESET) {
        snprintf(lines[0], sizeof(lines[0]), "CONFIRM RESET");
      } else if (pending_confirmation == ConfirmationState::BLE_TOGGLE) {
        snprintf(lines[0], sizeof(lines[0]), "CONFIRM BLE");
      }
      snprintf(lines[1], sizeof(lines[1]), "Press = OK");
      snprintf(lines[2], sizeof(lines[2]), "Wait = Cancel");
      snprintf(lines[4], sizeof(lines[4]), "Cancel in %lus", (unsigned long)remaining_s);
      renderDisplayLines(lines);
      return;
    }
  }

  // Check button hold progress
  if (button_is_held && button_hold_duration_ms > 500) {
    if (button_hold_duration_ms >= 12000) {
      snprintf(lines[0], sizeof(lines[0]), "HOLD: FacRst");
      snprintf(lines[1], sizeof(lines[1]), "Release = OK");
      snprintf(lines[3], sizeof(lines[3]), "[==========]");
    } else if (button_hold_duration_ms >= 8000) {
      snprintf(lines[0], sizeof(lines[0]), "HOLD: BLE Tgl");
      snprintf(lines[1], sizeof(lines[1]), "Wait %lus FacRst", (unsigned long)((12000 - button_hold_duration_ms)/1000));
      int bars = (button_hold_duration_ms - 8000) * 10 / 4000;
      char pb[13] = "[          ]";
      for (int i=0; i<bars; i++) pb[i+1] = '=';
      snprintf(lines[3], sizeof(lines[3]), "%s", pb);
    } else if (button_hold_duration_ms >= 4000) {
      snprintf(lines[0], sizeof(lines[0]), "HOLD: DistRst");
      snprintf(lines[1], sizeof(lines[1]), "Wait %lus BLE", (unsigned long)((8000 - button_hold_duration_ms)/1000));
      int bars = (button_hold_duration_ms - 4000) * 10 / 4000;
      char pb[13] = "[          ]";
      for (int i=0; i<bars; i++) pb[i+1] = '=';
      snprintf(lines[3], sizeof(lines[3]), "%s", pb);
    } else {
      snprintf(lines[0], sizeof(lines[0]), "HOLDING...");
      snprintf(lines[1], sizeof(lines[1]), "Wait %lus Dist", (unsigned long)((4000 - button_hold_duration_ms)/1000));
      int bars = button_hold_duration_ms * 10 / 4000;
      char pb[13] = "[          ]";
      for (int i=0; i<bars; i++) pb[i+1] = '=';
      snprintf(lines[3], sizeof(lines[3]), "%s", pb);
    }
    renderDisplayLines(lines);
    return;
  }

  // Normal pages
  switch (display_page) {
    case 0:
      // Main Page
      if (wifi_setup_active || wifi_station_connected || wifi_ap_active) {
        // Setup/WiFi Mode
        snprintf(lines[0], sizeof(lines[0]), "WiFi: %s", wifi_station_connected ? "STA" : (wifi_ap_active ? "AP" : "TRY"));
        snprintf(lines[1], sizeof(lines[1]), "%.13s", last_wifi_ip.c_str());
        snprintf(lines[2], sizeof(lines[2]), "%.13s", wifi_ap_active ? "EquineTracker" : "Searching...");
        snprintf(lines[3], sizeof(lines[3]), "B:%u%% %.1fV", cached_battery_percentage, cached_battery_voltage);
        snprintf(lines[4], sizeof(lines[4]), "1/4 -> press");
      } else {
        // Tracking Mode
        snprintf(lines[0], sizeof(lines[0]), "S:%.11s", tracker_phase);
        snprintf(lines[1], sizeof(lines[1]), "D:%.1fm", total_distance_meters);
        snprintf(lines[2], sizeof(lines[2]), "B:%u%% %.1fV", cached_battery_percentage, cached_battery_voltage);
        snprintf(lines[3], sizeof(lines[3]), "Fix:%s", has_initial_fix ? "YES" : "NO");
        snprintf(lines[4], sizeof(lines[4]), "1/4 -> press");
      }
      break;

    case 1:
      // GPS
      {
        const uint32_t sats = gps.satellites.isValid() ? gps.satellites.value() : 0;
        const double speed = gps.speed.isValid() ? gps.speed.kmph() : 0.0;
        snprintf(lines[0], sizeof(lines[0]), "GPS (2/4)");
        snprintf(lines[1], sizeof(lines[1]), "Sats: %lu", (unsigned long)sats);
        snprintf(lines[2], sizeof(lines[2]), "Spd: %.1f", speed);
        snprintf(lines[3], sizeof(lines[3]), "FixAge:%lus", (unsigned long)seconds_since_last_fix);
        snprintf(lines[4], sizeof(lines[4]), "Q: %u pt", history_count);
      }
      break;

    case 2:
      // Network/Radio
      snprintf(lines[0], sizeof(lines[0]), "RADIO (3/4)");
      snprintf(lines[1], sizeof(lines[1]), "LoRa: %s", lora_initialized ? "OK" : "NO");
      snprintf(lines[2], sizeof(lines[2]), "BLE: %s", ble_debug_enabled ? "ON" : "OFF");
      snprintf(lines[3], sizeof(lines[3]), "TX:%.2s AK:%.2s", triStateName(last_tx_status), triStateName(last_ack_status));
      if (!isnan(last_ack_rssi)) {
        snprintf(lines[4], sizeof(lines[4]), "Ak %.0fdBm", last_ack_rssi);
      } else {
        snprintf(lines[4], sizeof(lines[4]), "Ak -- dBm");
      }
      break;

    default:
      // Debug
      {
        char uptime[16];
        formatDuration(getSessionUptimeSeconds(), uptime, sizeof(uptime));
        snprintf(lines[0], sizeof(lines[0]), "DEBUG (4/4)");
        snprintf(lines[1], sizeof(lines[1]), "Up: %.9s", uptime);
        snprintf(lines[2], sizeof(lines[2]), "Rst: %.8s", resetReasonName(last_reset_reason));
        snprintf(lines[3], sizeof(lines[3]), "Err: %.8s", last_error);
        snprintf(lines[4], sizeof(lines[4]), "Wake: %lu", (unsigned long)wakeup_counter);
      }
      break;
  }

  renderDisplayLines(lines);
}

void requestDisplayWake(uint32_t duration_ms, bool advance_page) {
  if (advance_page) {
    display_page = (display_page + 1) % DISPLAY_PAGE_COUNT;
    display_last_page_change_ms = millis();
  }

  const uint32_t requested_deadline = millis() + duration_ms;
  if ((int32_t)(requested_deadline - display_on_until_ms) > 0) {
    display_on_until_ms = requested_deadline;
  }

  turnOnDisplay();
  renderStatusPage();
  display_last_refresh_ms = millis();
}

void serviceDisplayAndButton(bool force_refresh) {
  const uint32_t now = millis();
  const bool button_pressed = digitalRead(USER_BTN_PIN) == LOW;

  if (button_pressed) {
    if (!previous_button_pressed && (uint32_t)(now - last_button_event_ms) > 50) {
      // Just pressed down
      last_button_event_ms = now;
      button_press_start_ms = now;
      button_is_held = true;
      button_hold_duration_ms = 0;
      requestDisplayWake(DISPLAY_BUTTON_TIMEOUT_MS, false);
    } else if (button_is_held) {
      // Still holding
      button_hold_duration_ms = now - button_press_start_ms;
      if ((uint32_t)(now - display_last_refresh_ms) >= 200) {
        force_refresh = true; // Update hold progress
      }
      // Ensure display doesn't time out while holding
      if ((int32_t)((now + 2000) - display_on_until_ms) > 0) {
        display_on_until_ms = now + DISPLAY_BUTTON_TIMEOUT_MS;
      }
    }
  } else {
    // Button not pressed
    if (button_is_held) {
      // Just released
      button_is_held = false;
      last_button_event_ms = now;
      requestDisplayWake(DISPLAY_BUTTON_TIMEOUT_MS, false);

      if (pending_confirmation != ConfirmationState::NONE && now <= confirmation_timeout_ms) {
        // Confirmed an action!
        if (pending_confirmation == ConfirmationState::DISTANCE_RESET) {
          logPrintln("User confirmed distance reset via button!");
          resetDailyDistanceAndHistory();
          total_distance_meters = 0.0;
          prefs.putDouble("dist", 0.0);
          setLastError("Dist Reset!");
        } else if (pending_confirmation == ConfirmationState::FACTORY_RESET) {
          logPrintln("User confirmed factory reset via button!");
          turnOnDisplay();
          char lines[5][18] = {"FACTORY RESET", "IN PROGRESS", "Wiping NVS...", "Rebooting...", ""};
          renderDisplayLines(lines);
          prefs.clear();
          delay(2000);
          ESP.restart();
        } else if (pending_confirmation == ConfirmationState::BLE_TOGGLE) {
          logPrintln("User confirmed BLE toggle via button!");
          ble_debug_enabled = !ble_debug_enabled;
          prefs.putBool("ble_log", ble_debug_enabled);
          setLastError(ble_debug_enabled ? "BLE Debug ON" : "BLE Debug OFF");
          if (ble_debug_enabled && !ble_advertising) {
            BLEDevice::startAdvertising();
            ble_advertising = true;
          }
        }
        pending_confirmation = ConfirmationState::NONE;
      } else {
        // No pending confirmation. Process hold duration.
        if (button_hold_duration_ms >= 12000) {
          pending_confirmation = ConfirmationState::FACTORY_RESET;
          confirmation_timeout_ms = now + 10000;
        } else if (button_hold_duration_ms >= 8000) {
          pending_confirmation = ConfirmationState::BLE_TOGGLE;
          confirmation_timeout_ms = now + 10000;
        } else if (button_hold_duration_ms >= 4000) {
          pending_confirmation = ConfirmationState::DISTANCE_RESET;
          confirmation_timeout_ms = now + 10000;
        } else if (button_hold_duration_ms >= 50) {
          // Short press: advance page if there's no confirmation pending
          if (pending_confirmation == ConfirmationState::NONE) {
            requestDisplayWake(DISPLAY_BUTTON_TIMEOUT_MS, true);
          } else {
            pending_confirmation = ConfirmationState::NONE; // cancel confirmation
          }
        }
      }
      button_hold_duration_ms = 0;
      force_refresh = true;
    }
  }

  previous_button_pressed = button_pressed;

  // Handle confirmation timeout
  if (pending_confirmation != ConfirmationState::NONE && now > confirmation_timeout_ms) {
    pending_confirmation = ConfirmationState::NONE;
    force_refresh = true;
  }

  const bool temporary_window_active = (int32_t)(display_on_until_ms - now) > 0;
  const bool should_be_on = wifi_setup_active || temporary_window_active || button_is_held || (pending_confirmation != ConfirmationState::NONE);

  if (!should_be_on) {
    turnOffDisplay();
    return;
  }

  if (!display_awake) {
    turnOnDisplay();
    force_refresh = true;
  }

  if (force_refresh ||
      (uint32_t)(now - display_last_refresh_ms) >= DISPLAY_REFRESH_MS) {
    renderStatusPage();
    display_last_refresh_ms = now;
  }
}

// =====================================================
// SETUP
// =====================================================
void performTrackingCycle();

void initBoardPins() {
#if defined(BOARD_WIRELESS_TRACKER)
  pinMode(VEXT_CTRL_PIN, OUTPUT);
  pinMode(GNSS_PWR_PIN, OUTPUT);
  pinMode(GNSS_RST_PIN, OUTPUT);

  // Start with GNSS disabled during WiFi/OTA setup.
  digitalWrite(VEXT_CTRL_PIN, LOW);
  digitalWrite(GNSS_PWR_PIN, LOW);
  digitalWrite(GNSS_RST_PIN, LOW);

  pinMode(ADC_CTRL, OUTPUT);
  digitalWrite(ADC_CTRL, LOW); // Disable voltage divider initially to save power
#endif
}

void setup() {
  Serial.begin(115200);
#if defined(BOARD_WIRELESS_TRACKER)
  // Wait up to 4 seconds for native USB serial port to connect.
  const uint32_t start_ms = millis();
  while (!Serial && (uint32_t)(millis() - start_ms) < 4000) {
    delay(10);
  }
#else
  delay(250);
#endif

  prefs.begin("tracker", false);
  ble_debug_enabled = prefs.getBool("ble_log", false);

  initBoardPins();
  pinMode(USER_BTN_PIN, INPUT_PULLUP);

  if (BATTERY_SENSE_ENABLED) {
    analogReadResolution(12);
    analogSetPinAttenuation(BATT_PIN, ADC_11db);
  }

  last_reset_reason = esp_reset_reason();
  const bool hard_boot = (last_reset_reason != ESP_RST_DEEPSLEEP);
  const esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();

  // Load state before drawing anything, so every display page is immediately useful.
  if (hard_boot) {
    logPrintln("\n[boot] HARD BOOT DETECTED! Loading data from flash memory...");

    total_distance_meters = prefs.getDouble("dist", 0.0);
    last_saved_dist = total_distance_meters;

    boot_id = prefs.getUInt("boot_id", 0) + 1;
    prefs.putUInt("boot_id", boot_id);

    has_initial_fix = false;
    seconds_since_last_fix = 0;
    teleport_strikes = 0;
    wakeup_counter = 0;
    seconds_since_daily_reset = prefs.getUInt("daily_age", 0);
    next_point_seq = 0;
    clearHistory();

    struct timeval tv;
    gettimeofday(&tv, NULL);
    session_start_time_us = (uint64_t)tv.tv_sec * 1000000ULL + tv.tv_usec;
  } else {
    wakeup_counter++;
    boot_id = prefs.getUInt("boot_id", 1);
  }

  if (seconds_since_daily_reset >= SECONDS_PER_DAY) {
    resetDailyDistanceAndHistory();
  }

  Serial2.begin(GPS_BAUD, SERIAL_8N1, RXD2, TXD2);

#if !defined(BOARD_WIRELESS_TRACKER)
  // The BN-220 is independent of the OLED power rail.
  if (hard_boot) {
    sleepGPS();
  }
#endif

  if (hard_boot || wakeup_cause == ESP_SLEEP_WAKEUP_EXT0 || wifi_setup_active) {
    initDisplay();
    setTrackerPhase(hard_boot ? "Booting" : "Wake cycle");
    if (hard_boot) {
      requestDisplayWake(30000, false);
    }
  } else {
    // Lazy-load display. We still need to power VEXT for GNSS on the Tracker.
#if defined(BOARD_WIRELESS_TRACKER)
    pinMode(VEXT_CTRL_PIN, OUTPUT);
    digitalWrite(VEXT_CTRL_PIN, HIGH);
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, LOW); // explicitly keep backlight off
#endif
    setTrackerPhase("Wake cycle");
  }

  // A button-only deep-sleep wake is a status request, not a tracking cycle.
  // Show all pages for a while, then resume the original sleep deadline.
  if (!hard_boot && wakeup_cause == ESP_SLEEP_WAKEUP_EXT0) {
    previous_button_pressed = digitalRead(USER_BTN_PIN) == LOW;
    last_button_event_ms = millis();
    setTrackerPhase("Button status");
    requestDisplayWake(DISPLAY_BUTTON_TIMEOUT_MS, false);
    while ((int32_t)(display_on_until_ms - millis()) > 0) {
      serviceDisplayAndButton();
      delay(10);
    }
    turnOffDisplay();

    struct timeval tv;
    gettimeofday(&tv, NULL);
    const uint64_t now_us = (uint64_t)tv.tv_sec * 1000000ULL + tv.tv_usec;

    if (target_wakeup_time_us > now_us) {
#if defined(BOARD_WIRELESS_TRACKER)
      // Display use temporarily raised VEXT. Cut it again before returning to sleep.
      digitalWrite(GNSS_PWR_PIN, LOW);
      digitalWrite(GNSS_RST_PIN, LOW);
      digitalWrite(VEXT_CTRL_PIN, LOW);
      display_initialized = false;
      display_awake = false;
#endif
      prefs.end();
      const uint64_t sleep_remaining_us = target_wakeup_time_us - now_us;
      esp_sleep_enable_ext0_wakeup((gpio_num_t)USER_BTN_PIN, 0);
      esp_sleep_enable_timer_wakeup(sleep_remaining_us);
      esp_deep_sleep_start();
    }
  }

  if (hard_boot) {
    runWifiSetupMode();
  }

  setTrackerPhase("GNSS power-up");
  wakeupGPS();

  logPrintln("\n--- Equine Tracker Waking Up ---");
  logRTCState();

  if (ble_debug_enabled) {
    setTrackerPhase("BLE debug init");
    logPrintln("Starting BLE Debug Server...");
    BLEDevice::init("EquineTracker");
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());
    BLEService *pService = pServer->createService(SERVICE_UUID);
    pTxCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID_TX,
                      BLECharacteristic::PROPERTY_NOTIFY
                     );
    pTxCharacteristic->addDescriptor(new BLE2902());
    BLECharacteristic *pRxCharacteristic = pService->createCharacteristic(
                         CHARACTERISTIC_UUID_RX,
                         BLECharacteristic::PROPERTY_WRITE
                       );
    (void)pRxCharacteristic;
    pService->start();
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);
    pAdvertising->setMinPreferred(0x12);
    BLEDevice::startAdvertising();
    ble_advertising = true;
    logPrintln("BLE Advertising Started.");
  }

  setTrackerPhase("LoRa init");
  lora_initialized = initLoRaRadio();
  if (!lora_initialized) {
    setLastError("LoRa initialization failed");
    setTrackerPhase("LoRa error");
    logPrintln("LoRa failed!");
    serviceDisplayAndButton(true);

    if (!isDebugModeActive()) {
      logPrintln("Sleeping.");
      turnOffDisplay();
      sleepGPS();

      struct timeval tv;
      gettimeofday(&tv, NULL);
      const uint64_t now_us = (uint64_t)tv.tv_sec * 1000000ULL + tv.tv_usec;
      target_wakeup_time_us = now_us + SLEEP_DURATION_US;

      prefs.end();
      esp_sleep_enable_ext0_wakeup((gpio_num_t)USER_BTN_PIN, 0);
      esp_sleep_enable_timer_wakeup(SLEEP_DURATION_US);
      esp_deep_sleep_start();
    } else {
      logPrintln("Debug mode active, skipping deep sleep on LoRa fail.");
    }
  }

  setTrackerPhase("Ready");
  serviceDisplayAndButton(true);

  if (!isDebugModeActive()) {
    // Normal operation is low-power: display off unless the button temporarily wakes it.
    display_on_until_ms = 0;
    serviceDisplayAndButton(true);
    performTrackingCycle();
  } else {
    logPrintln("Setup finished. Debug mode active; display stays on and status pages rotate.");
  }
}

void performTrackingCycle() {
  uint32_t cycle_start_ms = millis();
  setTrackerPhase("GPS acquisition");
  serviceDisplayAndButton(true);
  // =====================================================
  // GPS ACQUISITION
  // =====================================================
  uint32_t elapsed_wait_ms = 0;
  const uint32_t LIGHT_SLEEP_CHUNK_MS = 15000;
  const uint32_t ACTIVE_LISTEN_MS = GPS_LISTEN_WINDOW_MS;
  uint32_t current_elapsed_s = seconds_since_last_fix;

  if (has_initial_fix && current_elapsed_s > MAX_FIX_AGE_S) {
    logPrintf("WARNING: Last fix is %u seconds old. Discarding it!\n", current_elapsed_s);
    has_initial_fix = false;
  }

  logPrintln("Starting GPS acquisition with Light Sleep cycles...");

  while (elapsed_wait_ms < GPS_TIMEOUT_MS) {
    if (isDebugModeActive()) {
      setTrackerPhase("GPS wait (debug)");
      logPrintf("ESP32 waiting for %u ms (skipping light sleep)...\n", LIGHT_SLEEP_CHUNK_MS);
      const uint32_t wait_start = millis();
      while ((uint32_t)(millis() - wait_start) < LIGHT_SLEEP_CHUNK_MS) {
        serviceDisplayAndButton();
        delay(10);
      }
    } else {
      logPrintf("ESP32 entering Light Sleep for %u ms...\n", LIGHT_SLEEP_CHUNK_MS);
      Serial.flush();
      esp_sleep_enable_ext0_wakeup((gpio_num_t)USER_BTN_PIN, 0);
      esp_sleep_enable_timer_wakeup(LIGHT_SLEEP_CHUNK_MS * 1000ULL);
      esp_light_sleep_start();
      if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
        previous_button_pressed = digitalRead(USER_BTN_PIN) == LOW;
        last_button_event_ms = millis();
        logPrintln("Button pressed during light sleep! Showing live status...");
        requestDisplayWake(DISPLAY_BUTTON_TIMEOUT_MS, false);
      }
    }

    elapsed_wait_ms += LIGHT_SLEEP_CHUNK_MS;
    current_elapsed_s += (LIGHT_SLEEP_CHUNK_MS / 1000);

    setTrackerPhase("GPS listen");
    serviceDisplayAndButton(true);
    uint32_t listen_start = millis();
    bool chunk_fix_found = false;

    while ((millis() - listen_start) < ACTIVE_LISTEN_MS) {
      bool processed_gps_bytes = false;

      while (Serial2.available() > 0) {
        gps.encode(Serial2.read());
        processed_gps_bytes = true;
      }

      if (gps.location.isUpdated() && gps.location.isValid() && gps.location.age() < 1500) {
        if (fabs(gps.location.lat()) < 0.1 && fabs(gps.location.lng()) < 0.1) {
          continue;
        }

        if (gps.hdop.isValid() && gps.satellites.isValid() &&
            gps.hdop.hdop() <= MAX_HDOP && gps.satellites.value() >= MIN_SATELLITES) {

          setTrackerPhase("GPS fix acquired");
          logPrintf("Solid Fix Acquired! (Sats: %u, HDOP: %.2f)\n",
                        gps.satellites.value(), gps.hdop.hdop());

          double candidate_lat = gps.location.lat();
          double candidate_lng = gps.location.lng();

          if (has_initial_fix) {
            logPrintf("Evaluating movement from last fix -> candidate: (%.6f, %.6f) vs last: (%.6f, %.6f)\n",
                          candidate_lat, candidate_lng, last_lat, last_lng);
            double distance_moved = TinyGPSPlus::distanceBetween(last_lat, last_lng, candidate_lat, candidate_lng);

            double allowed_distance = current_elapsed_s * MAX_SPEED_MPS;
            if (allowed_distance < 50.0) allowed_distance = 50.0;

            if (distance_moved > allowed_distance) {
              teleport_strikes++;
              logPrintf("TELEPORT! Moved: %.0f m. Allowed: %.0f m. (Strike %u/3)\n",
                            distance_moved, allowed_distance, teleport_strikes);

              if (teleport_strikes >= 3) {
                logPrintln("3 Strikes! Resetting position.");
                has_initial_fix = false;
                teleport_strikes = 0;
              }
              break;
            }

            teleport_strikes = 0;
            double current_speed = gps.speed.isValid() ? gps.speed.kmph() : 0.0;

            if (distance_moved > MIN_DISTANCE_METERS && current_speed > MIN_SPEED_KMPH) {
              total_distance_meters += distance_moved;
              last_lat = candidate_lat;
              last_lng = candidate_lng;
              logPrintf("Accepted movement -> total distance now %.2f m\n", total_distance_meters);

              maybeAppendSignificantHistoryPoint(candidate_lat, candidate_lng);
            } else {
              logPrintln("Jitter/Stationary detected. Freezing map marker.");
            }
          } else {
            last_lat = candidate_lat;
            last_lng = candidate_lng;
            has_initial_fix = true;

            maybeAppendSignificantHistoryPoint(candidate_lat, candidate_lng);
          }

          chunk_fix_found = true;
          seconds_since_last_fix = 0;
          break;
        }
      } else {
        if (gps.location.isValid()) {
          logPrintf("Weak Fix -> Sats: %u, HDOP: %.2f, Age: %u ms\n",
                        gps.satellites.isValid() ? gps.satellites.value() : 0,
                        gps.hdop.isValid() ? gps.hdop.hdop() : -1.0,
                        gps.location.age());
        }
      }

      serviceDisplayAndButton();
      if (!processed_gps_bytes) {
        delay(10);
      }
    }

    elapsed_wait_ms += ACTIVE_LISTEN_MS;
    current_elapsed_s += (ACTIVE_LISTEN_MS / 1000);

    if (chunk_fix_found) {
      break;
    } else {
      logPrintln("No confident fix yet. Going back to Light Sleep...");
    }
  }

  // =====================================================
  // FLASH SAVE
  // =====================================================
  bool should_save_to_flash = false;

  if ((total_distance_meters - last_saved_dist) >= SAVE_DIST_THRESHOLD) {
    should_save_to_flash = true;
  }

  if (wakeup_counter > 0 && (wakeup_counter % SAVE_CYCLE_THRESHOLD == 0)) {
    should_save_to_flash = true;
  }

  if (should_save_to_flash) {
    setTrackerPhase("Saving state");
    serviceDisplayAndButton(true);
    logPrintln("Saving current daily distance and reset age safely to flash (NVS)...");
    prefs.putDouble("dist", total_distance_meters);
    uint32_t projected_daily_age = seconds_since_daily_reset +
                                   ((millis() - cycle_start_ms) / 1000) +
                                   (SLEEP_DURATION_US / 1000000);
    prefs.putUInt("daily_age", projected_daily_age);
    last_saved_dist = total_distance_meters;
  }

  // =====================================================
  // PACK AND TRANSMIT
  // =====================================================
  if (history_count > 0) {
    setTrackerPhase("LoRa transmit");
    serviceDisplayAndButton(true);
    logPrintf("Unacknowledged points in queue: %u\n", history_count);
    
    for (int tx_loop = 0; tx_loop < 5; tx_loop++) {
      if (history_count == 0) {
        logPrintln("All points acknowledged. Done transmitting.");
        break;
      }

      uint8_t payload_buffer[240];
      uint16_t points_packed = 0;
      size_t payload_len = buildDynamicPayload(payload_buffer, sizeof(payload_buffer), points_packed);

      if (payload_len == 0 || points_packed == 0) {
        logPrintln("Skipping LoRa send because payload build failed.");
        break;
      }

      last_tx_bytes = payload_len;
      last_tx_points = points_packed;
      last_ack_status = -1;
      bool txResult = transmitLoRaPacket(payload_buffer, payload_len);
      last_tx_status = txResult ? 1 : 0;
      serviceDisplayAndButton(true);

      if (txResult) {
        setTrackerPhase("Waiting for ACK");
        logPrintf("LoRa sent %u bytes (%u packet points). Listening for ACK...\n", payload_len, points_packed);
        
        // Wait for ACK
        uint8_t rx_buffer[sizeof(AckPayload)];
        int rxResult = receiveLoRaAck(rx_buffer, sizeof(rx_buffer), 2500);
        bool ack_received = false;

        if (rxResult == sizeof(AckPayload)) {
          AckPayload ack;
          memcpy(&ack, rx_buffer, sizeof(AckPayload));
          
          if (ack.boot_id == boot_id) {
            last_acked_seq = ack.acked_seq;
            logPrintf("Received ACK for seq up to %lu\n", (unsigned long)ack.acked_seq);
            
            // Clear acknowledged points
            uint16_t original_count = history_count;
            uint16_t points_to_clear = 0;
            for (uint16_t i = 0; i < original_count; i++) {
              const StoredHistoryPoint& p = history_points[historyIndexToRing(points_to_clear)];
              if (p.seq <= ack.acked_seq) {
                 points_to_clear++;
              } else {
                 break;
              }
            }
            
            if (points_to_clear > 0) {
               history_count -= points_to_clear;
               logPrintf("Cleared %u points. Remaining unacked: %u\n", points_to_clear, history_count);
               ack_received = true;
               last_ack_status = 1;
               setLastError(nullptr);
            } else {
               last_ack_status = 0;
               logPrintf("Ignored stale ACK for seq %lu; nothing cleared.\n", (unsigned long)ack.acked_seq);
            }
          } else {
            last_ack_status = 0;
            logPrintf("Ignored ACK for wrong boot_id: %lu\n", (unsigned long)ack.boot_id);
          }
        } else if (rxResult > 0) {
          last_ack_status = 0;
          logPrintf("Ignored packet of wrong size: %d bytes\n", rxResult);
        } else {
          last_ack_status = 0;
        }

        serviceDisplayAndButton(true);
        sleepLoRaRadio();

        if (!ack_received) {
           setLastError("LoRa ACK not received");
           logPrintln("No ACK received. Stopping transmission loop for this cycle.");
           break;
        }

      } else {
        setLastError("LoRa transmit failed");
        logPrintln("LoRa transmit failed.");
        break;
      }

      responsiveDelay(250); // pause between loop-sent chunks
    }
  } else {
    setTrackerPhase("No queued points");
    logPrintln("No valid history points available. Skipping LoRa send this cycle.");
  }

  last_cycle_duration_ms = millis() - cycle_start_ms;
  serviceDisplayAndButton(true);

  // =====================================================
  // SLEEP
  // =====================================================
  if (!isDebugModeActive()) {
    setTrackerPhase("Deep sleep");
    serviceDisplayAndButton(true);
    turnOffDisplay();
    prefs.end();
    Serial.flush();
    sleepGPS();
    sleepLoRaRadio();

    uint32_t time_awake_s = (millis() - cycle_start_ms) / 1000;

    if (seconds_since_last_fix != 0) {
      seconds_since_last_fix = current_elapsed_s + (SLEEP_DURATION_US / 1000000);
    } else {
      seconds_since_last_fix += (SLEEP_DURATION_US / 1000000);
    }

    seconds_since_daily_reset += time_awake_s;
    seconds_since_daily_reset += (SLEEP_DURATION_US / 1000000);

    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint64_t now_us = (uint64_t)tv.tv_sec * 1000000ULL + tv.tv_usec;
    target_wakeup_time_us = now_us + SLEEP_DURATION_US;

    esp_sleep_enable_ext0_wakeup((gpio_num_t)USER_BTN_PIN, 0);
    esp_sleep_enable_timer_wakeup(SLEEP_DURATION_US);
    esp_deep_sleep_start();
  } else {
    setTrackerPhase("Debug idle");
    serviceDisplayAndButton(true);
    logPrintln("Tracking cycle completed. Awaiting next cycle in debug mode...");

    uint32_t time_awake_s = (millis() - cycle_start_ms) / 1000;
    uint32_t debug_sleep_ms = (SLEEP_DURATION_US / 1000ULL) * 2 / 3;
    uint32_t debug_sleep_s = debug_sleep_ms / 1000;

    if (seconds_since_last_fix != 0) {
      seconds_since_last_fix = current_elapsed_s + debug_sleep_s;
    } else {
      seconds_since_last_fix += debug_sleep_s;
    }

    seconds_since_daily_reset += time_awake_s + debug_sleep_s;
  }

}

void loop() {
  if (isDebugModeActive()) {
    static uint32_t last_cycle_ms = 0;
    static bool first_debug_cycle = true;
    const uint32_t debug_sleep_ms = (SLEEP_DURATION_US / 1000ULL) * 2 / 3;

    serviceDisplayAndButton();

    if ((uint32_t)(millis() - last_cycle_ms) > debug_sleep_ms || last_cycle_ms == 0) {
      if (!first_debug_cycle) {
        logPrintln("\n--- Starting tracking cycle ---");
        wakeup_counter++;
        if (seconds_since_daily_reset >= SECONDS_PER_DAY) {
          resetDailyDistanceAndHistory();
        }
        logRTCState();
      }
      first_debug_cycle = false;

      performTrackingCycle();
      last_cycle_ms = millis();
    }

    serviceDisplayAndButton();
    delay(10);
  } else {
    // Normal mode normally never reaches loop(): performTrackingCycle() enters
    // deep sleep. Keep the button/display service safe if an error path does.
    serviceDisplayAndButton();
    delay(10);
  }
}
