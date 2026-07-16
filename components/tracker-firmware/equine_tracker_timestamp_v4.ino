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
#include <BLESecurity.h>
#include <string>
#include "secrets.h"
#include "equine_protocol.h"
#include "equine_relay.h"
#include "equine_crypto.h"
#include "equine_config.h"
#include "equine_config_api.h"

// ==========================================
// VERSIONED PERSISTENT CONFIGURATION
// ==========================================
// The active identity and all tunable values are loaded from the CRC-protected
// NVS configuration before hardware startup.

EquineConfig::TrackerConfigV1 tracker_config{};
uint64_t tracker_device_hash = 0;

#define TRACKER_ID          (tracker_config.device_id)
#define TRACKER_NAME        (tracker_config.device_name)
#define TRACKER_DEVICE_HASH (tracker_device_hash)

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

// --- Runtime tuning loaded from tracker_config ---
// 448 points use 7,168 bytes and leave headroom in the ESP32's 8 KiB RTC slow
// memory for counters and future CRC/layout metadata. A 500-point queue does
// not link on the supported ESP32 target.
const uint16_t HISTORY_SIZE = 448;
const int32_t DELTA_UNIT_MICRODEG = 10;
const uint32_t SECONDS_PER_DAY = 86400;
const float MIN_BATTERY_VOLTAGE = 3.2f;
const float MAX_BATTERY_VOLTAGE = 4.2f;

#define LORA_FREQ                         ((double)tracker_config.lora.frequency_hz)
#define LORA_TX_POWER_DBM                 ((int)tracker_config.lora.tx_power_dbm)
#define LORA_SPREADING_FACTOR             ((int)tracker_config.lora.spreading_factor)
#define LORA_SIGNAL_BANDWIDTH             ((long)tracker_config.lora.bandwidth_hz)
#define LORA_CODING_RATE                  ((int)tracker_config.lora.coding_rate_denominator)
#define LORA_PREAMBLE_LENGTH              ((int)tracker_config.lora.preamble_length)
#define LORA_SYNC_WORD                    ((int)tracker_config.lora.sync_word)
#define LORA_RELAY_HOP_LIMIT              ((uint8_t)tracker_config.lora.relay_hop_limit)
#define MIN_DISTANCE_METERS               ((double)tracker_config.min_distance_m)
#define MIN_SPEED_KMPH                    ((double)tracker_config.min_speed_kmph)
#define MAX_HDOP                          ((double)tracker_config.max_hdop)
#define MIN_SATELLITES                    ((uint32_t)tracker_config.min_satellites)
#define MAX_SPEED_MPS                     ((double)tracker_config.max_speed_mps)
#define MAX_FIX_AGE_S                     ((uint32_t)tracker_config.max_fix_age_s)
#define SAVE_DIST_THRESHOLD               ((double)tracker_config.save_distance_threshold_m)
#define NVS_SAVE_INTERVAL_S               ((uint32_t)tracker_config.nvs_save_interval_s)
#define GPS_FULL_TIMEOUT_MS               ((uint32_t)tracker_config.gps_timeout_ms[0])
#define GPS_SECOND_NO_FIX_TIMEOUT_MS      ((uint32_t)tracker_config.gps_timeout_ms[1])
#define GPS_THIRD_NO_FIX_TIMEOUT_MS       ((uint32_t)tracker_config.gps_timeout_ms[2])
#define GPS_QUICK_PROBE_TIMEOUT_MS        ((uint32_t)tracker_config.gps_timeout_ms[3])
#define GPS_FULL_RETRY_INTERVAL_S         ((uint32_t)tracker_config.gps_full_retry_interval_s)
#define GPS_INITIAL_LISTEN_MS             ((uint32_t)tracker_config.gps_initial_listen_ms)
#define GPS_LIGHT_SLEEP_CHUNK_MS          ((uint32_t)tracker_config.gps_light_sleep_chunk_ms)
#define GPS_LISTEN_WINDOW_MS              ((uint32_t)tracker_config.gps_listen_window_ms)
#define MOVEMENT_SPEED_THRESHOLD_KMPH     ((double)tracker_config.movement_speed_threshold_kmph)
#define MOVEMENT_DISPLACEMENT_THRESHOLD_M ((double)tracker_config.movement_displacement_threshold_m)
#define MOVEMENT_EVIDENCE_DISTANCE_M      ((double)tracker_config.movement_evidence_distance_m)
#define MOVEMENT_EVIDENCE_STEP_M          ((double)tracker_config.movement_evidence_step_m)
#define MOVEMENT_DIRECTION_TOLERANCE_DEG  ((double)tracker_config.movement_direction_tolerance_deg)
#define MOVEMENT_EVIDENCE_REQUIRED        ((uint8_t)tracker_config.movement_evidence_required)
#define LORA_TX_INTERVAL_S                ((uint32_t)tracker_config.lora_tx_interval_s)
#define LORA_TX_MIN_POINTS                ((uint16_t)tracker_config.lora_tx_min_points)
#define LORA_ACK_TIMEOUT_MS               ((uint32_t)tracker_config.lora_ack_timeout_ms)
#define LORA_RETRY_BACKOFF_1_S            ((uint32_t)tracker_config.lora_retry_backoff_s[0])
#define LORA_RETRY_BACKOFF_2_S            ((uint32_t)tracker_config.lora_retry_backoff_s[1])
#define LORA_RETRY_BACKOFF_3_S            ((uint32_t)tracker_config.lora_retry_backoff_s[2])
#define LORA_RETRY_BACKOFF_MAX_S          ((uint32_t)tracker_config.lora_retry_backoff_s[3])
#define HISTORY_POINT_SPACING_M           ((double)tracker_config.history_point_spacing_m)
#define BATTERY_SENSE_ENABLED             (tracker_config.battery_sense_enabled != 0)
#define SLEEP_DURATION_US                 ((uint64_t)tracker_config.moving_sleep_s * 1000000ULL)
#define STATIONARY_SLEEP_DURATION_US      ((uint64_t)tracker_config.stationary_sleep_s * 1000000ULL)
#define LONG_STATIONARY_SLEEP_DURATION_US ((uint64_t)tracker_config.long_stationary_sleep_s * 1000000ULL)
#define NO_FIX_SLEEP_1_US                 ((uint64_t)tracker_config.no_fix_sleep_s[0] * 1000000ULL)
#define NO_FIX_SLEEP_2_US                 ((uint64_t)tracker_config.no_fix_sleep_s[1] * 1000000ULL)
#define NO_FIX_SLEEP_3_US                 ((uint64_t)tracker_config.no_fix_sleep_s[2] * 1000000ULL)
#define NO_FIX_SLEEP_MAX_US               ((uint64_t)tracker_config.no_fix_sleep_s[3] * 1000000ULL)
#define STATIONARY_FIXES_FOR_LONG_SLEEP   ((uint16_t)tracker_config.stationary_fixes_for_long_sleep)
#define STATIONARY_FIXES_FOR_MAX_SLEEP    ((uint16_t)tracker_config.stationary_fixes_for_max_sleep)

// Fixed implementation limits and UI timing are not part of the user config.
const uint8_t GNSS_CONFIG_VERSION = 1;
const uint32_t GNSS_COMMAND_TIMEOUT_MS = 500;
const uint32_t GNSS_ASSISTANCE_MAX_AGE_S = 12UL * 60UL * 60UL;
const uint8_t WIFI_MAX_CONNECT_ATTEMPTS = 5;
const uint32_t WIFI_CONNECT_TIMEOUT_MS = 10000;
const uint32_t WEB_CLIENT_IDLE_TIMEOUT_MS = 15000;
const uint32_t DISPLAY_BUTTON_TIMEOUT_MS = 20000;
const uint32_t DISPLAY_REFRESH_MS = 5000;
const uint32_t DISPLAY_PAGE_INTERVAL_MS = 10000;
const uint32_t DISPLAY_BATTERY_REFRESH_MS = 5000;
const uint32_t BUTTON_DEBOUNCE_MS = 30;
const uint32_t WIFI_SETUP_POST_BOOT_WINDOW_MS = 5000;
const uint8_t DISPLAY_PAGE_COUNT = 4;
const uint32_t BLE_CONNECTION_WINDOW_MS = 60000;
const uint32_t BLE_RECONNECT_WINDOW_MS = 15000;
const uint32_t WIFI_SETUP_BOOT_HOLD_MS = 1500;

bool isValidSha256Hex(const char* value);

// --- UBX Command to put BN-220 to Sleep ---
const byte UBX_SLEEP_CMD[] = {
  0xB5, 0x62, 0x02, 0x41, 0x08, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x4D, 0x3B
};

// =====================================================
// VERSIONED PAYLOAD FORMAT
// =====================================================
using AckPayload = EquineProtocol::AckPayloadV1;
using SecureFrameHeader = EquineProtocol::SecureFrameHeaderV2;
using HistoryPayload = EquineProtocol::HistoryPayloadV2;
using AnchorPoint = EquineProtocol::AnchorPointV1;
using DeltaPoint = EquineProtocol::DeltaPointV1;

// =====================================================
// INTERNAL RTC HISTORY
// =====================================================
struct StoredHistoryPoint {
  int32_t lat;          // microdegrees
  int32_t lon;          // microdegrees
  uint32_t seq;         // monotonic point sequence
  uint32_t unix_time_s; // GNSS UTC; zero only when time is unavailable
} __attribute__((packed));

// =====================================================
// GLOBALS / RTC STATE
// =====================================================
TinyGPSPlus gps;
Preferences prefs;
Preferences configPrefs;
char admin_password[25]{};

RTC_DATA_ATTR double total_distance_meters = 0.0;
RTC_DATA_ATTR double last_lat = 0.0;
RTC_DATA_ATTR double last_lng = 0.0;
RTC_DATA_ATTR bool has_initial_fix = false;

RTC_DATA_ATTR StoredHistoryPoint history_points[HISTORY_SIZE];
RTC_DATA_ATTR uint16_t history_head = 0;   // next write index
RTC_DATA_ATTR uint16_t history_count = 0;  // valid points in ring

struct RtcHistoryMetadata {
  uint32_t magic;
  uint16_t schema;
  uint16_t point_size;
  uint16_t capacity;
  uint16_t head;
  uint16_t count;
  uint16_t reserved;
  uint32_t next_seq;
  uint32_t next_tx_counter;
  uint32_t boot_id;
  uint64_t nonce_prefix;
  uint32_t crc32;
} __attribute__((packed));

constexpr uint32_t RTC_HISTORY_MAGIC = 0x4c545248UL;  // "LTRH"
constexpr uint16_t RTC_HISTORY_SCHEMA = 2;
RTC_DATA_ATTR RtcHistoryMetadata rtc_history_metadata{};

RTC_DATA_ATTR uint32_t seconds_since_last_fix = 0;
RTC_DATA_ATTR uint8_t teleport_strikes = 0;
RTC_DATA_ATTR uint32_t wakeup_counter = 0;
RTC_DATA_ATTR double last_saved_dist = -1.0;
RTC_DATA_ATTR uint32_t seconds_since_daily_reset = 0;
RTC_DATA_ATTR uint32_t next_point_seq = 0; // survives deep sleep
RTC_DATA_ATTR uint32_t next_tx_counter = 0; // unique per encryption epoch
RTC_DATA_ATTR uint64_t tx_nonce_prefix = 0;
RTC_DATA_ATTR uint64_t target_wakeup_time_us = 0;
RTC_DATA_ATTR uint64_t session_start_time_us = 0; // wall-clock uptime across deep sleep
RTC_DATA_ATTR uint16_t stationary_fix_streak = 0;
RTC_DATA_ATTR uint8_t consecutive_no_fix_cycles = 0;
RTC_DATA_ATTR uint32_t seconds_since_last_tx = 0;
RTC_DATA_ATTR uint32_t seconds_since_last_lora_attempt = 0;
RTC_DATA_ATTR uint8_t consecutive_lora_failures = 0;
RTC_DATA_ATTR uint32_t seconds_since_last_nvs_save = 0;
RTC_DATA_ATTR uint32_t seconds_since_last_full_gnss_attempt = 0;
RTC_DATA_ATTR uint32_t seconds_since_last_accepted_position = 0;
RTC_DATA_ATTR double last_effective_speed_kmph = 0.0;

// Raw-fix history for movement hysteresis. These are deliberately separate
// from last_lat/last_lng, which represent the last accepted tracking position.
RTC_DATA_ATTR bool has_previous_raw_fix = false;
RTC_DATA_ATTR double previous_raw_lat = 0.0;
RTC_DATA_ATTR double previous_raw_lng = 0.0;
RTC_DATA_ATTR uint8_t movement_evidence_streak = 0;

// GNSS assistance state. Time is stored as Unix UTC seconds from the most
// recent trustworthy RMC date/time and advanced using seconds_since_last_fix.
RTC_DATA_ATTR uint64_t last_gnss_utc_epoch_s = 0;
RTC_DATA_ATTR double last_gnss_altitude_m = 0.0;
RTC_DATA_ATTR bool has_gnss_utc_time = false;

uint32_t boot_id = 0; // loaded/incremented from NVS on hard boot

uint32_t updateCrc32(uint32_t crc, const uint8_t* data, size_t length) {
  for (size_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      const uint32_t mask = -(crc & 1UL);
      crc = (crc >> 1) ^ (0xEDB88320UL & mask);
    }
  }
  return crc;
}

uint32_t calculateRtcHistoryCrc(RtcHistoryMetadata metadata) {
  metadata.crc32 = 0;
  uint32_t crc = updateCrc32(
    0xFFFFFFFFUL,
    reinterpret_cast<const uint8_t*>(&metadata), sizeof(metadata));
  crc = updateCrc32(
    crc,
    reinterpret_cast<const uint8_t*>(history_points), sizeof(history_points));
  return ~crc;
}

void sealRtcHistory() {
  rtc_history_metadata.magic = RTC_HISTORY_MAGIC;
  rtc_history_metadata.schema = RTC_HISTORY_SCHEMA;
  rtc_history_metadata.point_size = sizeof(StoredHistoryPoint);
  rtc_history_metadata.capacity = HISTORY_SIZE;
  rtc_history_metadata.head = history_head;
  rtc_history_metadata.count = history_count;
  rtc_history_metadata.reserved = 0;
  rtc_history_metadata.next_seq = next_point_seq;
  rtc_history_metadata.next_tx_counter = next_tx_counter;
  rtc_history_metadata.boot_id = boot_id;
  rtc_history_metadata.nonce_prefix = tx_nonce_prefix;
  rtc_history_metadata.crc32 = calculateRtcHistoryCrc(rtc_history_metadata);
}

bool validateRtcHistory() {
  return rtc_history_metadata.magic == RTC_HISTORY_MAGIC &&
    rtc_history_metadata.schema == RTC_HISTORY_SCHEMA &&
    rtc_history_metadata.point_size == sizeof(StoredHistoryPoint) &&
    rtc_history_metadata.capacity == HISTORY_SIZE &&
    rtc_history_metadata.head == history_head &&
    rtc_history_metadata.count == history_count &&
    history_head < HISTORY_SIZE && history_count <= HISTORY_SIZE &&
    rtc_history_metadata.next_seq == next_point_seq &&
    rtc_history_metadata.next_tx_counter == next_tx_counter &&
    rtc_history_metadata.boot_id == boot_id &&
    rtc_history_metadata.nonce_prefix == tx_nonce_prefix &&
    rtc_history_metadata.crc32 == calculateRtcHistoryCrc(rtc_history_metadata);
}

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
bool gps_powered = false;
int8_t last_tx_status = -1;   // -1 unknown, 0 failed, 1 successful
int8_t last_ack_status = -1;  // -1 unknown, 0 missing/invalid, 1 successful
uint16_t last_tx_bytes = 0;
uint16_t last_tx_points = 0;
uint32_t last_acked_seq = 0;
float last_ack_rssi = NAN;
float last_ack_snr = NAN;
uint32_t last_cycle_duration_ms = 0;
uint32_t last_fix_acquired_ms = 0;

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
void turnOnDisplay();
void turnOffDisplay();
void renderDisplayLines(char lines[5][18]);
void sleepGPS();
void sleepLoRaRadio();
void startBleDebugWindow(uint32_t duration_ms, bool force_provisioning = false);
void processBleConfigCommand(const char* command);
void stopBleDebug();
void serviceBleDebug();
bool isBleConnectionWindowActive();

// --- BLE Globals ---
#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E" // UART service UUID
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

BLEServer *pServer = NULL;
BLECharacteristic *pTxCharacteristic = NULL;
bool deviceConnected = false;
bool ble_session_authenticated = false;
bool ble_initialized = false;
bool ble_connection_window_active = false;
uint32_t ble_connection_window_deadline_ms = 0;

// BLE transitions are requested by the button UI and executed later from
// loop(). Never initialize/deinitialize the BLE stack from inside the button
// handler: doing so can block display state cleanup and leave the tracker in
// an undefined operating mode.
bool ble_disable_transition_requested = false;
bool ble_enable_transition_requested = false;
RTC_DATA_ATTR bool ble_open_window_on_next_wake = false;

// Configuration/onboarding lifecycle. Configuration writes are transactional:
// a complete candidate is patched, finalized, validated, and only then replaces
// the active CRC-protected blob. Reboot/factory-reset work is deferred outside
// HTTP and BLE callbacks so responses can finish cleanly.
bool tracker_onboarding_required = false;
bool config_reboot_requested = false;
bool config_factory_reset_requested = false;
bool ble_provisioning_mode = false;
BLECharacteristic *pRxCharacteristic = NULL;
constexpr size_t BLE_CONFIG_COMMAND_MAX = 1536;
char ble_config_command[BLE_CONFIG_COMMAND_MAX];
size_t ble_config_command_length = 0;


class BleConfigCallbacks: public BLECharacteristicCallbacks {
 public:
  void onWrite(BLECharacteristic* characteristic) override {
    if (!characteristic) return;
    const std::string value = characteristic->getValue();
    for (char c : value) {
      if (c == '\r') continue;
      if (c == '\n') {
        ble_config_command[ble_config_command_length] = '\0';
        if (ble_config_command_length > 0) {
          processBleConfigCommand(ble_config_command);
        }
        ble_config_command_length = 0;
        continue;
      }
      if (ble_config_command_length + 1 >= BLE_CONFIG_COMMAND_MAX) {
        ble_config_command_length = 0;
        processBleConfigCommand("__OVERFLOW__");
        continue;
      }
      ble_config_command[ble_config_command_length++] = c;
    }
  }
};

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* server) override {
        (void)server;
        deviceConnected = true;
        ble_session_authenticated = false;
        ble_advertising = false;
        ble_connection_window_active = false;
        debug_mode = true; // A connected client deliberately keeps the tracker awake.
    }

    void onDisconnect(BLEServer* server) override {
        deviceConnected = false;
        ble_session_authenticated = false;
        debug_mode = false;
        ble_connection_window_active = true;
        ble_connection_window_deadline_ms = millis() + BLE_RECONNECT_WINDOW_MS;
        ble_advertising = true;
        server->startAdvertising();
    }
};

// =====================================================
// HELPERS
// =====================================================
bool isDebugModeActive() {
  return debug_mode || deviceConnected;
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

bool isBleConnectionWindowActive() {
  return ble_connection_window_active &&
         (int32_t)(ble_connection_window_deadline_ms - millis()) > 0;
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
  if (deviceConnected && ble_session_authenticated && pTxCharacteristic) {
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
  if (deviceConnected && ble_session_authenticated && pTxCharacteristic) {
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
  if (deviceConnected && ble_session_authenticated && pTxCharacteristic) {
    String cleanS = String(buffer);
    cleanS.replace("\r\n", "\n");
    cleanS.replace("\n", "\r\n");
    pTxCharacteristic->setValue(cleanS.c_str());
    pTxCharacteristic->notify();
    delay(5);
  }
}


// =====================================================
// CONFIGURATION STORAGE
// =====================================================
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

void applyTrackerConfigRuntimeState() {
  tracker_device_hash = EquineProtocol::deviceIdHash(tracker_config.device_id);
  ble_debug_enabled = tracker_config.ble_debug_enabled != 0;
}

bool readTrackerConfigBlob(const char* key,
                           EquineConfig::TrackerConfigV1& output) {
  if (!configPrefs.begin(EquineConfig::CONFIG_NAMESPACE, true)) return false;
  const size_t length = configPrefs.getBytesLength(key);
  const bool size_ok = length == sizeof(output);
  const size_t read = size_ok
    ? configPrefs.getBytes(key, &output, sizeof(output))
    : 0;
  configPrefs.end();
  return size_ok && read == sizeof(output) &&
         EquineConfig::validateTrackerConfig(output);
}

bool writeTrackerConfigBlob(const char* key,
                            const EquineConfig::TrackerConfigV1& value) {
  if (!configPrefs.begin(EquineConfig::CONFIG_NAMESPACE, false)) return false;
  const size_t written = configPrefs.putBytes(key, &value, sizeof(value));
  configPrefs.end();
  return written == sizeof(value);
}

bool saveTrackerConfig(bool increment_revision = true) {
  EquineConfig::TrackerConfigV1 previous{};
  if (readTrackerConfigBlob(EquineConfig::ACTIVE_CONFIG_KEY, previous)) {
    // Keep one independently validated rollback copy before replacing active.
    if (!writeTrackerConfigBlob(EquineConfig::BACKUP_CONFIG_KEY, previous)) {
      logPrintln("Warning: failed to update tracker configuration backup.");
    }
  }

  uint32_t revision = tracker_config.header.revision;
  if (revision == 0) revision = 1;
  else if (increment_revision && revision < UINT32_MAX) revision++;
  EquineConfig::finalize(
    tracker_config, EquineConfig::DeviceRole::TRACKER, revision);

  if (!EquineConfig::validateTrackerConfig(tracker_config)) {
    logPrintln("Refusing to save invalid tracker configuration.");
    return false;
  }
  if (!writeTrackerConfigBlob(
        EquineConfig::ACTIVE_CONFIG_KEY, tracker_config)) {
    logPrintln("Failed to save tracker configuration.");
    return false;
  }

  applyTrackerConfigRuntimeState();
  logPrintf("Saved tracker config schema=%u revision=%lu id=%s.\n",
            tracker_config.header.schema_version,
            (unsigned long)tracker_config.header.revision,
            tracker_config.device_id);
  return true;
}

void makeTrackerFactoryConfig() {
  const uint32_t chip_suffix =
    static_cast<uint32_t>(ESP.getEfuseMac() & 0x00FFFFFFULL);
  char factory_id[EquineConfig::DEVICE_ID_SIZE];
  char factory_name[EquineConfig::DEVICE_NAME_SIZE];
  snprintf(factory_id, sizeof(factory_id), "tracker-%06lx",
           static_cast<unsigned long>(chip_suffix));
  snprintf(factory_name, sizeof(factory_name), "Tracker %06lX",
           static_cast<unsigned long>(chip_suffix));
  uint8_t generated_lora_key[EquineProtocol::AEAD_KEY_SIZE];
  esp_fill_random(generated_lora_key, sizeof(generated_lora_key));
  EquineConfig::makeDefaultTrackerConfig(
    tracker_config,
    factory_id,
    factory_name,
    ssid,
    password,
    generated_lora_key);

  EquineConfig::finalize(
    tracker_config, EquineConfig::DeviceRole::TRACKER, 1);
}


bool readTrackerProvisionedFlag(bool default_value) {
  if (!configPrefs.begin(EquineConfig::CONFIG_NAMESPACE, true)) {
    return default_value;
  }
  const bool result = configPrefs.getBool(
    EquineConfigApi::PROVISIONED_KEY, default_value);
  configPrefs.end();
  return result;
}

void writeTrackerProvisionedFlag(bool provisioned) {
  if (!configPrefs.begin(EquineConfig::CONFIG_NAMESPACE, false)) return;
  configPrefs.putBool(EquineConfigApi::PROVISIONED_KEY, provisioned);
  configPrefs.end();
}

bool loadTrackerConfig() {
  const char* source = "active";
  bool created_factory = false;
  if (!readTrackerConfigBlob(
        EquineConfig::ACTIVE_CONFIG_KEY, tracker_config)) {
    source = "backup";
    if (!readTrackerConfigBlob(
          EquineConfig::BACKUP_CONFIG_KEY, tracker_config)) {
      source = "factory/migrated";
      created_factory = true;
      makeTrackerFactoryConfig();
      if (!saveTrackerConfig(false)) return false;
      writeTrackerProvisionedFlag(false);
    } else {
      // Repair the active slot from the valid rollback copy.
      writeTrackerConfigBlob(EquineConfig::ACTIVE_CONFIG_KEY, tracker_config);
    }
  }

  tracker_onboarding_required = !readTrackerProvisionedFlag(!created_factory);
  applyTrackerConfigRuntimeState();
  logPrintf("Loaded tracker config from %s: schema=%u revision=%lu id=%s name=%s onboarding=%s.\n",
            source,
            tracker_config.header.schema_version,
            (unsigned long)tracker_config.header.revision,
            tracker_config.device_id,
            tracker_config.device_name,
            tracker_onboarding_required ? "required" : "complete");
  return true;
}

void clearTrackerConfigStorage() {
  if (configPrefs.begin(EquineConfig::CONFIG_NAMESPACE, false)) {
    configPrefs.clear();
    configPrefs.end();
  }
  clearAdminCredential();
}


bool commitTrackerConfigCandidate(
    EquineConfig::TrackerConfigV1 candidate,
    bool mark_provisioned,
    char* error,
    size_t error_size) {
  const uint32_t next_revision =
    tracker_config.header.revision < UINT32_MAX
      ? tracker_config.header.revision + 1
      : tracker_config.header.revision;
  EquineConfig::finalize(
    candidate, EquineConfig::DeviceRole::TRACKER, next_revision);
  if (!EquineConfig::validateTrackerConfig(candidate)) {
    snprintf(error, error_size, "candidate violates cross-field validation");
    return false;
  }

  const EquineConfig::TrackerConfigV1 previous = tracker_config;
  tracker_config = candidate;
  if (!saveTrackerConfig(false)) {
    tracker_config = previous;
    applyTrackerConfigRuntimeState();
    snprintf(error, error_size, "failed to write active configuration");
    return false;
  }

  if (mark_provisioned) {
    writeTrackerProvisionedFlag(true);
    tracker_onboarding_required = false;
  }
  return true;
}

bool rollbackTrackerConfig(char* error, size_t error_size) {
  EquineConfig::TrackerConfigV1 backup{};
  if (!readTrackerConfigBlob(EquineConfig::BACKUP_CONFIG_KEY, backup)) {
    snprintf(error, error_size, "no valid rollback configuration");
    return false;
  }
  return commitTrackerConfigCandidate(backup, true, error, error_size);
}

String trackerConfigJson() {
  using EquineConfigApi::appendJsonEscaped;
  char hash_text[17];
  char key_text[EquineProtocol::AEAD_KEY_SIZE * 2 + 1];
  EquineProtocol::formatDeviceHash(
    TRACKER_DEVICE_HASH, hash_text, sizeof(hash_text));
  for (size_t i = 0; i < EquineProtocol::AEAD_KEY_SIZE; i++) {
    snprintf(key_text + i * 2, 3, "%02x", tracker_config.lora_aead_key[i]);
  }

  String out;
  out.reserve(3600);
  out = "{\"api_version\":" + String(EquineConfigApi::API_VERSION) +
        ",\"role\":\"tracker\",\"config_schema\":" +
        String(tracker_config.header.schema_version) +
        ",\"revision\":" + String(tracker_config.header.revision) +
        ",\"onboarding_required\":" +
        String(tracker_onboarding_required ? "true" : "false") +
        ",\"device_id\":\"";
  appendJsonEscaped(out, tracker_config.device_id);
  out += "\",\"device_name\":\"";
  appendJsonEscaped(out, tracker_config.device_name);
  out += "\",\"device_hash\":\"" + String(hash_text) +
         "\",\"wifi_ssid\":\"";
  appendJsonEscaped(out, tracker_config.wifi_ssid);
  out += "\",\"lora_aead_key\":\"" + String(key_text) +
         "\",\"wifi_password_set\":" +
         String(tracker_config.wifi_password[0] ? "true" : "false") +
         ",\"ble_debug_enabled\":" +
         String(tracker_config.ble_debug_enabled ? "true" : "false") +
         ",\"battery_sense_enabled\":" +
         String(tracker_config.battery_sense_enabled ? "true" : "false") +
         ",\"lora\":{" +
         String("\"frequency_hz\":") + String(tracker_config.lora.frequency_hz) +
         ",\"bandwidth_hz\":" + String(tracker_config.lora.bandwidth_hz) +
         ",\"tx_power_dbm\":" + String(tracker_config.lora.tx_power_dbm) +
         ",\"sf\":" + String(tracker_config.lora.spreading_factor) +
         ",\"coding_rate\":" + String(tracker_config.lora.coding_rate_denominator) +
         ",\"preamble_length\":" + String(tracker_config.lora.preamble_length) +
         ",\"sync_word\":" + String(tracker_config.lora.sync_word) +
         ",\"relay_hop_limit\":" + String(tracker_config.lora.relay_hop_limit) + "}";
  out += ",\"communication\":{";
  out += "\"tx_interval_s\":" + String(tracker_config.lora_tx_interval_s) +
         ",\"tx_min_points\":" + String(tracker_config.lora_tx_min_points) +
         ",\"ack_timeout_ms\":" + String(tracker_config.lora_ack_timeout_ms) +
         ",\"retry_backoff_s\":[";
  for (uint8_t i = 0; i < 4; i++) {
    if (i) out += ',';
    out += String(tracker_config.lora_retry_backoff_s[i]);
  }
  out += "]}";
  out += ",\"gps\":{";
  out += "\"min_distance_m\":" + String(tracker_config.min_distance_m, 2) +
         ",\"min_speed_kmph\":" + String(tracker_config.min_speed_kmph, 2) +
         ",\"max_hdop\":" + String(tracker_config.max_hdop, 2) +
         ",\"min_satellites\":" + String(tracker_config.min_satellites) +
         ",\"max_speed_mps\":" + String(tracker_config.max_speed_mps, 2) +
         ",\"max_fix_age_s\":" + String(tracker_config.max_fix_age_s) +
         ",\"timeouts_ms\":[";
  for (uint8_t i = 0; i < 4; i++) {
    if (i) out += ',';
    out += String(tracker_config.gps_timeout_ms[i]);
  }
  out += "],\"full_retry_interval_s\":" +
         String(tracker_config.gps_full_retry_interval_s) +
         ",\"initial_listen_ms\":" + String(tracker_config.gps_initial_listen_ms) +
         ",\"light_sleep_chunk_ms\":" + String(tracker_config.gps_light_sleep_chunk_ms) +
         ",\"listen_window_ms\":" + String(tracker_config.gps_listen_window_ms) + "}";
  out += ",\"movement\":{";
  out += "\"speed_threshold_kmph\":" +
         String(tracker_config.movement_speed_threshold_kmph, 2) +
         ",\"displacement_threshold_m\":" +
         String(tracker_config.movement_displacement_threshold_m, 2) +
         ",\"evidence_distance_m\":" +
         String(tracker_config.movement_evidence_distance_m, 2) +
         ",\"evidence_step_m\":" +
         String(tracker_config.movement_evidence_step_m, 2) +
         ",\"direction_tolerance_deg\":" +
         String(tracker_config.movement_direction_tolerance_deg, 1) +
         ",\"evidence_required\":" +
         String(tracker_config.movement_evidence_required) + "}";
  out += ",\"storage\":{";
  out += "\"history_point_spacing_m\":" +
         String(tracker_config.history_point_spacing_m, 2) +
         ",\"save_distance_threshold_m\":" +
         String(tracker_config.save_distance_threshold_m, 2) +
         ",\"nvs_save_interval_s\":" +
         String(tracker_config.nvs_save_interval_s) + "}";
  out += ",\"sleep\":{";
  out += "\"moving_s\":" + String(tracker_config.moving_sleep_s) +
         ",\"stationary_s\":" + String(tracker_config.stationary_sleep_s) +
         ",\"long_stationary_s\":" + String(tracker_config.long_stationary_sleep_s) +
         ",\"no_fix_s\":[";
  for (uint8_t i = 0; i < 4; i++) {
    if (i) out += ',';
    out += String(tracker_config.no_fix_sleep_s[i]);
  }
  out += "],\"stationary_fixes_for_long_sleep\":" +
         String(tracker_config.stationary_fixes_for_long_sleep) +
         ",\"stationary_fixes_for_max_sleep\":" +
         String(tracker_config.stationary_fixes_for_max_sleep) + "}}";
  return out;
}

void sendBleConfigText(const String& response) {
  if (!deviceConnected || !pTxCharacteristic) return;
  // 18-byte pieces remain valid with the default 23-byte BLE MTU. The newline
  // terminates one response; app clients concatenate notifications until then.
  String framed = response;
  if (framed.length() == 0 || framed.c_str()[framed.length() - 1] != '\n') {
    framed += '\n';
  }
  constexpr size_t payload_size = 18;
  for (size_t offset = 0; offset < framed.length(); offset += payload_size) {
    const size_t length = min(payload_size, framed.length() - offset);
    pTxCharacteristic->setValue(
      reinterpret_cast<uint8_t*>(const_cast<char*>(framed.c_str() + offset)),
      length);
    pTxCharacteristic->notify();
    delay(8);
  }
}

void sendBleConfigError(const char* code, const char* detail) {
  String out = "{\"ok\":false,\"error\":\"";
  EquineConfigApi::appendJsonEscaped(out, code);
  out += "\",\"detail\":\"";
  EquineConfigApi::appendJsonEscaped(out, detail);
  out += "\"}";
  sendBleConfigText(out);
}

bool applyTrackerUrlPatch(char* encoded,
                          uint32_t& expected_revision,
                          bool& reboot_requested,
                          EquineConfigApi::PatchStatus& status,
                          EquineConfig::TrackerConfigV1& candidate) {
  bool has_revision = false;
  char parse_error[EquineConfigApi::ERROR_SIZE] = {};
  const bool parsed = EquineConfigApi::forEachUrlEncodedPair(
    encoded,
    [&](const char* key, const char* value) {
      if (strcmp(key, "expected_revision") == 0) {
        has_revision = EquineConfigApi::parseUnsigned(
          value, 1, UINT32_MAX, expected_revision);
        if (!has_revision) {
          EquineConfigApi::setError(status, key, "invalid revision");
          return false;
        }
        return true;
      }
      if (strcmp(key, "reboot") == 0) {
        uint8_t parsed_bool = 0;
        if (!EquineConfigApi::parseBool(value, parsed_bool)) {
          EquineConfigApi::setError(status, key, "invalid boolean");
          return false;
        }
        reboot_requested = parsed_bool != 0;
        return true;
      }
      if (EquineConfigApi::isControlField(key)) return true;
      const auto result = EquineConfigApi::applyTrackerField(
        candidate, key, value, status);
      if (result == EquineConfigApi::FieldResult::UNKNOWN) {
        EquineConfigApi::setError(status, key, "unknown setting");
        return false;
      }
      return result == EquineConfigApi::FieldResult::APPLIED;
    },
    parse_error, sizeof(parse_error));
  if (!parsed && status.error[0] == '\0') {
    strlcpy(status.error, parse_error, sizeof(status.error));
  }
  if (!parsed) return false;
  if (!has_revision) {
    EquineConfigApi::setError(status, "expected_revision", "required");
    return false;
  }
  return true;
}

bool applyTrackerWebPatch(EquineConfigApi::PatchStatus& status,
                          bool& reboot_requested) {
  EquineConfig::TrackerConfigV1 candidate = tracker_config;
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
    const auto result = EquineConfigApi::applyTrackerField(
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
  if (expected_revision != tracker_config.header.revision) {
    snprintf(status.error, sizeof(status.error),
             "revision conflict: current=%lu",
             (unsigned long)tracker_config.header.revision);
    return false;
  }
  if (!status.changed) {
    if (tracker_onboarding_required) {
      writeTrackerProvisionedFlag(true);
      tracker_onboarding_required = false;
    }
    return true;
  }
  return commitTrackerConfigCandidate(
    candidate, true, status.error, sizeof(status.error));
}

void processBleConfigCommand(const char* command) {
  if (!command) return;
  if (strcmp(command, "__OVERFLOW__") == 0) {
    sendBleConfigError("command_too_large", "maximum is 1535 bytes");
    return;
  }
  if (strncmp(command, "AUTH ", 5) == 0) {
    const char* supplied = command + 5;
    const size_t supplied_length = strlen(supplied);
    const size_t expected_length = strlen(admin_password);
    uint8_t difference = static_cast<uint8_t>(supplied_length ^ expected_length);
    const size_t compared = supplied_length > expected_length
      ? supplied_length : expected_length;
    for (size_t i = 0; i < compared; i++) {
      const uint8_t left = i < supplied_length ? supplied[i] : 0;
      const uint8_t right = i < expected_length ? admin_password[i] : 0;
      difference |= left ^ right;
    }
    if (difference == 0 && expected_length >= 12) {
      ble_session_authenticated = true;
      sendBleConfigText("{\"ok\":true,\"authenticated\":true}");
    } else {
      ble_session_authenticated = false;
      sendBleConfigError("authentication_failed", "disconnect and try again");
    }
    return;
  }
  if (!ble_session_authenticated) {
    sendBleConfigError("authentication_required", "send AUTH <admin-password> first");
    return;
  }
  if (strcmp(command, "HELLO") == 0) {
    String out = "{\"ok\":true,\"api_version\":" +
      String(EquineConfigApi::API_VERSION) +
      ",\"role\":\"tracker\",\"revision\":" +
      String(tracker_config.header.revision) +
      ",\"onboarding_required\":" +
      String(tracker_onboarding_required ? "true" : "false") + "}";
    sendBleConfigText(out);
    return;
  }
  if (strcmp(command, "GET CONFIG") == 0) {
    sendBleConfigText(trackerConfigJson());
    return;
  }
  if (strncmp(command, "PATCH ", 6) == 0) {
    const size_t length = strlen(command + 6);
    if (length >= BLE_CONFIG_COMMAND_MAX) {
      sendBleConfigError("command_too_large", "patch exceeds buffer");
      return;
    }
    char encoded[BLE_CONFIG_COMMAND_MAX];
    memcpy(encoded, command + 6, length + 1);
    EquineConfigApi::PatchStatus status;
    EquineConfig::TrackerConfigV1 candidate = tracker_config;
    uint32_t expected_revision = 0;
    bool reboot = false;
    if (!applyTrackerUrlPatch(encoded, expected_revision, reboot,
                              status, candidate)) {
      sendBleConfigError("invalid_patch", status.error);
      return;
    }
    if (expected_revision != tracker_config.header.revision) {
      char detail[96];
      snprintf(detail, sizeof(detail), "revision conflict: current=%lu",
               (unsigned long)tracker_config.header.revision);
      sendBleConfigError("revision_conflict", detail);
      return;
    }
    if (status.changed) {
      if (!commitTrackerConfigCandidate(
            candidate, true, status.error, sizeof(status.error))) {
        sendBleConfigError("commit_failed", status.error);
        return;
      }
    } else if (tracker_onboarding_required) {
      writeTrackerProvisionedFlag(true);
      tracker_onboarding_required = false;
    }
    const bool needs_reboot = status.reboot_required || reboot;
    String out = "{\"ok\":true,\"revision\":" +
      String(tracker_config.header.revision) +
      ",\"changed\":" + String(status.changed ? "true" : "false") +
      ",\"reboot_required\":" +
      String(needs_reboot ? "true" : "false") + "}";
    sendBleConfigText(out);
    config_reboot_requested |= needs_reboot;
    return;
  }
  if (strncmp(command, "ROLLBACK ", 9) == 0) {
    uint32_t expected = 0;
    if (!EquineConfigApi::parseUnsigned(command + 9, 1, UINT32_MAX, expected) ||
        expected != tracker_config.header.revision) {
      sendBleConfigError("revision_conflict", "send current revision");
      return;
    }
    char error[EquineConfigApi::ERROR_SIZE] = {};
    if (!rollbackTrackerConfig(error, sizeof(error))) {
      sendBleConfigError("rollback_failed", error);
      return;
    }
    sendBleConfigText("{\"ok\":true,\"reboot_required\":true}");
    config_reboot_requested = true;
    return;
  }
  if (strcmp(command, "FACTORY_RESET FACTORY_RESET") == 0) {
    sendBleConfigText("{\"ok\":true,\"factory_reset\":true}");
    config_factory_reset_requested = true;
    return;
  }
  if (strcmp(command, "REBOOT") == 0) {
    sendBleConfigText("{\"ok\":true,\"rebooting\":true}");
    config_reboot_requested = true;
    return;
  }
  sendBleConfigError("unknown_command",
    "use HELLO, GET CONFIG, PATCH, ROLLBACK, FACTORY_RESET or REBOOT");
}

void stopBleDebug() {
  if (!ble_initialized) {
    ble_connection_window_active = false;
    ble_advertising = false;
    return;
  }

  if (ble_advertising) {
    BLEAdvertising* advertising = BLEDevice::getAdvertising();
    if (advertising) advertising->stop();
  }

  BLEDevice::deinit(true);
  pServer = NULL;
  pTxCharacteristic = NULL;
  pRxCharacteristic = NULL;
  deviceConnected = false;
  ble_session_authenticated = false;
  debug_mode = false;
  ble_initialized = false;
  ble_advertising = false;
  ble_connection_window_active = false;
  ble_provisioning_mode = false;
  ble_config_command_length = 0;
}

void startBleDebugWindow(uint32_t duration_ms, bool force_provisioning) {
  if (!ble_debug_enabled && !force_provisioning) return;
  ble_provisioning_mode = force_provisioning;

  if (!ble_initialized) {
    logPrintln("Starting bounded BLE/configuration window...");
    String ble_name = "EqTrk-" + String(TRACKER_ID);
    BLEDevice::init(ble_name.c_str());
    const uint32_t pairing_pin =
      100000UL + static_cast<uint32_t>(
        EquineProtocol::deviceIdHash(admin_password) % 900000ULL);
    BLESecurity* security = new BLESecurity();
    security->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);
    security->setCapability(ESP_IO_CAP_OUT);
    security->setKeySize(16);
    security->setStaticPIN(pairing_pin);
    logPrintf("BLE Secure Connections pairing PIN: %06lu\n",
              static_cast<unsigned long>(pairing_pin));
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    BLEService *pService = pServer->createService(SERVICE_UUID);
    pTxCharacteristic = pService->createCharacteristic(
      CHARACTERISTIC_UUID_TX,
      BLECharacteristic::PROPERTY_NOTIFY
    );
    pTxCharacteristic->addDescriptor(new BLE2902());

    pRxCharacteristic = pService->createCharacteristic(
      CHARACTERISTIC_UUID_RX,
      BLECharacteristic::PROPERTY_WRITE
    );
    pRxCharacteristic->setAccessPermissions(ESP_GATT_PERM_WRITE_ENC_MITM);
    pRxCharacteristic->setCallbacks(new BleConfigCallbacks());

    pService->start();
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);
    pAdvertising->setMinPreferred(0x12);
    ble_initialized = true;
  }

  BLEDevice::startAdvertising();
  ble_advertising = true;
  ble_connection_window_active = true;
  ble_connection_window_deadline_ms = millis() + duration_ms;
}

void serviceBleDebug() {
  if (ble_connection_window_active && !deviceConnected &&
      !isBleConnectionWindowActive()) {
    logPrintln("BLE connection window expired; shutting BLE down.");
    stopBleDebug();
  }
}

void waitForBleConnectionWindow() {
  while (isBleConnectionWindowActive() && !deviceConnected) {
    serviceDisplayAndButton();
    delay(10);
  }
  serviceBleDebug();
}


// Enter a short deep-sleep reset while preserving RTC tracking state. This is
// used when enabling BLE after it was previously deinitialized. The next wake
// starts from a clean ESP32/BLE stack and opens the requested connection window.
[[noreturn]] void restartThroughDeepSleepForBle() {
  setTrackerPhase("BLE restart");
  turnOnDisplay();
  char lines[5][18] = {"BLE ENABLED", "Restarting radio", "Reconnect soon", "", ""};
  renderDisplayLines(lines);
  delay(600);

  stopBleDebug();
  turnOffDisplay();
  sleepGPS();
  sleepLoRaRadio();
  prefs.end();
  Serial.flush();

  ble_open_window_on_next_wake = true;
  const uint64_t restart_delay_us = 1000000ULL;
  struct timeval tv;
  gettimeofday(&tv, NULL);
  const uint64_t now_us = (uint64_t)tv.tv_sec * 1000000ULL + tv.tv_usec;
  target_wakeup_time_us = now_us + restart_delay_us;

  sealRtcHistory();
  esp_sleep_enable_ext0_wakeup((gpio_num_t)USER_BTN_PIN, 0);
  esp_sleep_enable_timer_wakeup(restart_delay_us);
  esp_deep_sleep_start();
  while (true) delay(1000);
}



// =====================================================
// GNSS CONFIGURATION AND ASSISTANCE
// =====================================================
int64_t daysFromCivil(int year, unsigned month, unsigned day) {
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned year_of_era = (unsigned)(year - era * 400);
  const unsigned adjusted_month =
    (unsigned)((int)month + (month > 2 ? -3 : 9));
  const unsigned day_of_year =
    (153U * adjusted_month + 2U) / 5U + day - 1U;
  const unsigned day_of_era =
    year_of_era * 365U + year_of_era / 4U - year_of_era / 100U + day_of_year;
  return (int64_t)era * 146097LL + (int64_t)day_of_era - 719468LL;
}

uint64_t utcToUnixSeconds(int year, unsigned month, unsigned day,
                          unsigned hour, unsigned minute, unsigned second) {
  if (year < 1980 || month < 1 || month > 12 || day < 1 || day > 31 ||
      hour > 23 || minute > 59 || second > 59) {
    return 0;
  }
  const int64_t days = daysFromCivil(year, month, day);
  if (days < 0) return 0;
  return (uint64_t)days * 86400ULL +
         (uint64_t)hour * 3600ULL +
         (uint64_t)minute * 60ULL + second;
}

void civilFromDays(int64_t days_since_epoch, int& year,
                   unsigned& month, unsigned& day) {
  int64_t z = days_since_epoch + 719468LL;
  const int64_t era = (z >= 0 ? z : z - 146096LL) / 146097LL;
  const unsigned day_of_era = (unsigned)(z - era * 146097LL);
  const unsigned year_of_era =
    (day_of_era - day_of_era / 1460U + day_of_era / 36524U -
     day_of_era / 146096U) / 365U;
  year = (int)year_of_era + (int)era * 400;
  const unsigned day_of_year =
    day_of_era - (365U * year_of_era + year_of_era / 4U - year_of_era / 100U);
  const unsigned month_prime = (5U * day_of_year + 2U) / 153U;
  day = day_of_year - (153U * month_prime + 2U) / 5U + 1U;
  month = (unsigned)((int)month_prime + (month_prime < 10U ? 3 : -9));
  year += month <= 2U;
}

void unixSecondsToUtc(uint64_t epoch_s, int& year, unsigned& month,
                      unsigned& day, unsigned& hour, unsigned& minute,
                      unsigned& second) {
  const uint64_t days = epoch_s / 86400ULL;
  const uint32_t seconds_of_day = (uint32_t)(epoch_s % 86400ULL);
  civilFromDays((int64_t)days, year, month, day);
  hour = seconds_of_day / 3600U;
  minute = (seconds_of_day % 3600U) / 60U;
  second = seconds_of_day % 60U;
}

void drainGnssInput() {
  while (Serial2.available() > 0) (void)Serial2.read();
}

#if defined(BOARD_WIRELESS_TRACKER)
bool waitForUc6580Result(uint32_t timeout_ms) {
  char line[128] = {};
  size_t line_length = 0;
  const uint32_t start_ms = millis();

  while ((uint32_t)(millis() - start_ms) < timeout_ms) {
    while (Serial2.available() > 0) {
      const char c = (char)Serial2.read();
      if (c == '\r' || c == '\n') {
        if (line_length > 0) {
          line[line_length] = '\0';
          if (strstr(line, "$OK") != NULL) return true;
          if (strstr(line, "$FAIL") != NULL) return false;
          line_length = 0;
        }
      } else if (line_length + 1 < sizeof(line)) {
        line[line_length++] = c;
      } else {
        line_length = 0;
      }
    }
    serviceDisplayAndButton();
    delay(5);
  }
  return false;
}

bool sendUc6580Command(const char* command,
                       uint32_t timeout_ms = GNSS_COMMAND_TIMEOUT_MS) {
  if (!command || !command[0]) return false;
  drainGnssInput();
  Serial2.print(command);
  Serial2.print("\r\n");
  Serial2.flush();
  const bool ok = waitForUc6580Result(timeout_ms);
  logPrintf("UC6580 command %s -> %s\n", command, ok ? "OK" : "no ACK/FAIL");
  return ok;
}

void configureUc6580OutputIfNeeded() {
  const uint8_t configured_version = prefs.getUChar("gnss_cfg_v", 0);
  if (configured_version == GNSS_CONFIG_VERSION) return;

  setTrackerPhase("GNSS configure");
  logPrintln("Configuring UC6580 for GGA/RMC-only output...");

  bool ok = true;
  ok = sendUc6580Command("$CFGMSG,0,0,1") && ok; // GGA on
  ok = sendUc6580Command("$CFGMSG,0,1,0") && ok; // GLL off
  ok = sendUc6580Command("$CFGMSG,0,2,0") && ok; // GSA off
  ok = sendUc6580Command("$CFGMSG,0,3,0") && ok; // GSV off
  ok = sendUc6580Command("$CFGMSG,0,4,1") && ok; // RMC on
  ok = sendUc6580Command("$CFGMSG,0,5,0") && ok; // VTG off
  ok = sendUc6580Command("$CFGMSG,0,6,0") && ok; // ZDA off
  ok = sendUc6580Command("$CFGMSG,0,7,0") && ok; // GST off

  if (ok && sendUc6580Command("$CFGSAVE", 1000)) {
    // The UC6580 manual requires at least one second of uninterrupted power
    // after CFGSAVE. Use a little margin before recording success in NVS.
    responsiveDelay(1200);
    prefs.putUChar("gnss_cfg_v", GNSS_CONFIG_VERSION);
    logPrintln("UC6580 message configuration saved.");
  } else {
    setLastError("GNSS config failed");
    logPrintln("UC6580 configuration was not persisted; it will retry on next hard boot.");
  }
  drainGnssInput();
}

void sendUc6580Assistance() {
  if (!has_initial_fix || !has_gnss_utc_time ||
      seconds_since_last_fix > GNSS_ASSISTANCE_MAX_AGE_S) {
    return;
  }

  const uint64_t estimated_epoch_s =
    last_gnss_utc_epoch_s + seconds_since_last_fix;
  int year = 0;
  unsigned month = 0, day = 0, hour = 0, minute = 0, second = 0;
  unixSecondsToUtc(estimated_epoch_s, year, month, day, hour, minute, second);

  char command[128];
  snprintf(command, sizeof(command), "$AIDTIME,%d,%u,%u,%u,%u,%u,0",
           year, month, day, hour, minute, second);
  (void)sendUc6580Command(command);

  const double abs_lat = fabs(last_lat);
  const double abs_lng = fabs(last_lng);
  const int lat_degrees = (int)abs_lat;
  const int lng_degrees = (int)abs_lng;
  const double nmea_lat = lat_degrees * 100.0 + (abs_lat - lat_degrees) * 60.0;
  const double nmea_lng = lng_degrees * 100.0 + (abs_lng - lng_degrees) * 60.0;

  snprintf(command, sizeof(command), "$AIDPOS,%.6f,%c,%.6f,%c,%.1f",
           nmea_lat, last_lat >= 0.0 ? 'N' : 'S',
           nmea_lng, last_lng >= 0.0 ? 'E' : 'W',
           last_gnss_altitude_m);
  (void)sendUc6580Command(command);
  drainGnssInput();
}
#else
void configureUc6580OutputIfNeeded() {}
void sendUc6580Assistance() {}
#endif

uint32_t updateGnssAssistanceState(uint32_t elapsed_since_last_fix_s = 0) {
  const bool fresh_utc =
    gps.date.isValid() && gps.time.isValid() &&
    gps.date.age() < 2000 && gps.time.age() < 2000;

  if (fresh_utc) {
    const uint64_t epoch_s = utcToUnixSeconds(
      gps.date.year(), gps.date.month(), gps.date.day(),
      gps.time.hour(), gps.time.minute(), gps.time.second()
    );
    if (epoch_s > 0) {
      last_gnss_utc_epoch_s = epoch_s;
      has_gnss_utc_time = true;
    }
  } else if (has_gnss_utc_time && elapsed_since_last_fix_s > 0) {
    // GGA can arrive before the matching RMC sentence. Advance the last trusted
    // RMC epoch by the measured fix age rather than attaching the old epoch to
    // a new position. The next fresh RMC sentence corrects any accumulated drift.
    const uint64_t advanced =
      last_gnss_utc_epoch_s + static_cast<uint64_t>(elapsed_since_last_fix_s);
    last_gnss_utc_epoch_s = advanced;
  }

  if (gps.altitude.isValid() && gps.altitude.age() < 2000) {
    last_gnss_altitude_m = gps.altitude.meters();
  }

  if (!has_gnss_utc_time || last_gnss_utc_epoch_s > UINT32_MAX) return 0;
  return static_cast<uint32_t>(last_gnss_utc_epoch_s);
}

float getBatteryVoltage();
uint8_t getBatteryPercentage();
void refreshBatteryCache(bool force);

void refreshBatteryCache(bool force) {
  const uint32_t now = millis();
  if (!force && cached_battery_read_ms != 0 &&
      (uint32_t)(now - cached_battery_read_ms) < DISPLAY_BATTERY_REFRESH_MS) {
    return;
  }

  float voltage = 0.0f;
  if (BATTERY_SENSE_ENABLED) {
#if defined(BOARD_WIRELESS_TRACKER)
    pinMode(ADC_CTRL, OUTPUT);
    digitalWrite(ADC_CTRL, HIGH);
    delay(50);
    const uint32_t millivolts = analogReadMilliVolts(BATT_PIN);
    digitalWrite(ADC_CTRL, LOW);
    voltage = (millivolts / 1000.0f) * 4.9f;
#else
    const uint32_t millivolts = analogReadMilliVolts(BATT_PIN);
    voltage = (millivolts * 2.0f) / 1000.0f;
#endif
  }

  cached_battery_voltage = voltage;
  if (voltage > 0.0f) {
    const int pct = (int)(((voltage - MIN_BATTERY_VOLTAGE) * 100.0f) /
                          (MAX_BATTERY_VOLTAGE - MIN_BATTERY_VOLTAGE));
    cached_battery_percentage = (uint8_t)constrain(pct, 0, 100);
  } else {
    cached_battery_percentage = 0;
  }
  cached_battery_read_ms = now;
}

void logRTCState() {
  refreshBatteryCache(false);
  logPrintf("RTC state -> has_fix: %s, dist: %.2f m, age: %u s, daily_reset_age: %u s, "
            "strikes: %u, hist_count: %u, next_seq: %lu, stationary: %u, "
            "since_tx: %u s, retry_age: %u s, lora_fail: %u, "
            "since_nvs: %u s, full_gnss_age: %u s, batt: %.2fV (%u%%)\n",
            has_initial_fix ? "true" : "false",
            total_distance_meters,
            seconds_since_last_fix,
            seconds_since_daily_reset,
            teleport_strikes,
            history_count,
            (unsigned long)next_point_seq,
            stationary_fix_streak,
            seconds_since_last_tx,
            seconds_since_last_lora_attempt,
            consecutive_lora_failures,
            seconds_since_last_nvs_save,
            seconds_since_last_full_gnss_attempt,
            cached_battery_voltage,
            cached_battery_percentage);
}

float getBatteryVoltage() {
  refreshBatteryCache(false);
  return cached_battery_voltage;
}

uint8_t getBatteryPercentage() {
  refreshBatteryCache(false);
  return cached_battery_percentage;
}

void wakeupGPS() {
  if (gps_powered) return;
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
  Serial2.write(0xFF);
#endif
  gps_powered = true;
  // Start consuming UART data quickly instead of waiting through a fixed 500 ms delay.
  responsiveDelay(100);
}

void sleepGPS() {
  if (!gps_powered) return;
#if defined(BOARD_WIRELESS_TRACKER)
  logPrintln("Powering down GNSS (Wireless Tracker)...");
  digitalWrite(GNSS_PWR_PIN, LOW);
  // Do not drive the reset line into an unpowered module: high impedance
  // avoids both a forced reset and possible GPIO back-powering.
  pinMode(GNSS_RST_PIN, INPUT);
  digitalWrite(VEXT_CTRL_PIN, LOW);

  // TFT and GNSS share VEXT. Cutting it invalidates the TFT controller state.
  display_initialized = false;
  display_awake = false;
#else
  logPrintln("Sending UBX Sleep Command to GPS...");
  for (size_t i = 0; i < sizeof(UBX_SLEEP_CMD); i++) {
    Serial2.write(UBX_SLEEP_CMD[i]);
  }
  Serial2.flush();
#endif
  gps_powered = false;
  delay(50);
}

#if defined(BOARD_WIRELESS_TRACKER)
// Keep LoRa on the ESP32-S3 FSPI bus. TFT_eSPI uses the HSPI bus;
// separating them allows the TFT to function properly.
SPIClass loraSPI(FSPI);
SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY, loraSPI);
#endif

bool initLoRaRadio() {
  if (lora_initialized) return true;
#if defined(BOARD_WIRELESS_TRACKER)
  loraSPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
  
  float freq = LORA_FREQ / 1000000.0;
  float bw = LORA_SIGNAL_BANDWIDTH / 1000.0;
  
  logPrintf("Initializing RadioLib SX1262 -> freq=%.1fMHz bw=%.1fkHz sf=%d cr=%d sync=0x%02X power=%d tcxo=1.6V\n",
            freq, bw, LORA_SPREADING_FACTOR, LORA_CODING_RATE, LORA_SYNC_WORD, LORA_TX_POWER_DBM);

  int state = radio.begin(freq, bw, LORA_SPREADING_FACTOR, LORA_CODING_RATE, LORA_SYNC_WORD, LORA_TX_POWER_DBM, LORA_PREAMBLE_LENGTH, 1.6, false);
  if (state == RADIOLIB_ERR_NONE) {
    radio.setCRC(true);
    lora_initialized = true;
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
  lora_initialized = true;
  return true;
#endif
}

void sleepLoRaRadio() {
  if (!lora_initialized) return;
#if defined(BOARD_WIRELESS_TRACKER)
  radio.sleep();
#else
  LoRa.sleep();
#endif
  lora_initialized = false;
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

bool isCandidateAckPacket(const uint8_t* buffer, size_t length) {
  const size_t expected_size =
    sizeof(EquineRelay::LinkHeaderV1) + sizeof(SecureFrameHeader) +
    sizeof(AckPayload) + EquineProtocol::AEAD_TAG_SIZE;
  if (!buffer || length != expected_size) return false;
  EquineRelay::LinkHeaderV1 link{};
  SecureFrameHeader header{};
  return EquineRelay::parseLinkedFrame(buffer, length, link, header) &&
         header.frame.message_type == static_cast<uint8_t>(
           EquineProtocol::MessageType::ACK) &&
         header.frame.device_id_hash == TRACKER_DEVICE_HASH &&
         header.boot_id == boot_id;
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
      if (length > max_len) length = max_len;
      state = radio.readData(buffer, length);
      if (state == RADIOLIB_ERR_NONE) {
        if (isCandidateAckPacket(buffer, length)) {
          last_ack_rssi = radio.getRSSI();
          last_ack_snr = radio.getSNR();
          return length;
        }
        // A repeater can echo this tracker's HISTORY before the gateway ACK.
        // Keep listening instead of ending the ACK window on expected traffic.
        state = radio.startReceive();
        if (state != RADIOLIB_ERR_NONE) return -1;
      } else {
        logPrintf("RadioLib readData failed, code: %d\n", state);
        return -1;
      }
    } else if (irq & RADIOLIB_SX126X_IRQ_CRC_ERR) {
      radio.startReceive();
    } else if (irq & RADIOLIB_SX126X_IRQ_TIMEOUT) {
      return 0;
    }
    serviceDisplayAndButton();
    delay(10);
  }
  return 0;
#else
  LoRa.receive();
  unsigned long start = millis();
  while (millis() - start < timeout_ms) {
    int packetSize = LoRa.parsePacket();
    if (packetSize) {
      if (packetSize <= (int)max_len) {
        const int bytes_read = LoRa.readBytes(buffer, packetSize);
        if (bytes_read == packetSize &&
            isCandidateAckPacket(buffer, packetSize)) {
          last_ack_rssi = LoRa.packetRssi();
          last_ack_snr = LoRa.packetSnr();
          return packetSize;
        }
      } else {
        while (LoRa.available()) LoRa.read();
      }
    }
    serviceDisplayAndButton();
    delay(10);
  }
  return 0;
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

  String savedSsid = String(tracker_config.wifi_ssid);
  String savedPw = String(tracker_config.wifi_password);
  ble_debug_enabled = tracker_config.ble_debug_enabled != 0;

  // Configuration BLE is available during an explicit setup/onboarding session
  // even when ordinary BLE debug logging is disabled.
  startBleDebugWindow(tracker_onboarding_required ? 600000UL : 120000UL, true);

  bool connected = false;
  if (savedSsid.length() > 0 && !tracker_onboarding_required) {
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
    String onboarding_ap = "LoRaTracker-" + String(TRACKER_ID);
    if (strlen(admin_password) < 12) {
      setLastError("Setup password too short");
      logPrintln("Fallback AP disabled: onboarding password must be at least 12 characters.");
    } else if (WiFi.softAP(onboarding_ap.c_str(), admin_password)) {
      wifi_ap_active = true;
      wifi_station_connected = false;
      responsiveDelay(500);
      last_wifi_ip = WiFi.softAPIP().toString();
      last_wifi_rssi = 0;
      setLastError(savedSsid.length() > 0 ? "STA failed; fallback AP" : "No SSID; fallback AP");
      logPrintf("AP '%s' started at %s.\n",
                onboarding_ap.c_str(), last_wifi_ip.c_str());
      if (tracker_onboarding_required) {
        logPrintf("FIRST-BOOT ADMIN CREDENTIAL (record securely): admin / %s\n",
                  admin_password);
      }
    } else {
      setLastError("Fallback AP failed");
      logPrintln("Fallback AP failed.");
    }
  }

  String otaHost = "lora-tracker-" + String(TRACKER_ID);
  ArduinoOTA.setHostname(otaHost.c_str());
  if (isValidSha256Hex(ota_password_hash)) {
    ArduinoOTA.setPasswordHash(ota_password_hash);
    ArduinoOTA.begin();
  } else {
    logPrintln("OTA disabled: configure a 64-character SHA-256 password hash.");
  }

  auto requireWebAuthentication = []() -> bool {
    if (strlen(admin_password) < 12) {
      webServer.send(503, "application/json", "{\"error\":\"admin_credentials_not_configured\"}");
      return false;
    }
    if (webServer.authenticate("admin", admin_password)) return true;
    webServer.requestAuthentication(BASIC_AUTH, "LoRa Tracker");
    return false;
  };

  webServer.on("/", HTTP_GET, [requireWebAuthentication]() {
    if (!requireWebAuthentication()) return;
    wifi_client_connected = true;
    last_web_activity_ms = millis();
    logPrintln("Web onboarding UI accessed by a client.");
    String html;
    html.reserve(7000);
    html = "<html><head><title>LoRa Tracker</title><meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<style>body{font-family:sans-serif;margin:20px;max-width:760px}label{display:block;margin-top:12px}input{padding:8px;width:100%;box-sizing:border-box}button{margin-top:18px;padding:10px 16px}code,pre{background:#f4f4f4;padding:4px}small{color:#555}</style></head><body>";
    html += "<h1>" + String(TRACKER_NAME) + " Tracker Onboarding</h1>";
    html += "<p>Schema/revision: " + String(tracker_config.header.schema_version) +
            "/" + String(tracker_config.header.revision) +
            "<br>Status: <b>" +
            String(tracker_onboarding_required ? "onboarding required" : "configured") +
            "</b></p>";
    html += "<form action='/save' method='POST'>";
    html += "<input type='hidden' name='expected_revision' value='" +
            String(tracker_config.header.revision) + "'>";
    html += "<label>Tracker ID<input name='device_id' value='" +
            String(tracker_config.device_id) + "'></label>";
    html += "<label>Name<input name='device_name' value='" +
            String(tracker_config.device_name) + "'></label>";
    html += "<label>Wi-Fi SSID<input name='wifi_ssid' value='" +
            String(tracker_config.wifi_ssid) + "'></label>";
    html += "<label>Wi-Fi password<input type='password' name='wifi_password' value='";
    html += EquineConfigApi::KEEP_SECRET;
    html += "'></label><small>Leave as __KEEP__ to preserve it; use __CLEAR__ to erase it.</small>";
    html += "<label><input style='width:auto' type='checkbox' name='ble_debug_enabled' value='1' ";
    if (tracker_config.ble_debug_enabled) html += "checked";
    html += "> Enable bounded BLE debug logging</label>";
    html += "<label>Moving interval (s)<input type='number' name='moving_sleep_s' value='" +
            String(tracker_config.moving_sleep_s) + "'></label>";
    html += "<label>Stationary interval (s)<input type='number' name='stationary_sleep_s' value='" +
            String(tracker_config.stationary_sleep_s) + "'></label>";
    html += "<label>Maximum HDOP<input name='max_hdop' value='" +
            String(tracker_config.max_hdop, 2) + "'></label>";
    html += "<label>Minimum satellites<input type='number' name='min_satellites' value='" +
            String(tracker_config.min_satellites) + "'></label>";
    html += "<label>LoRa spreading factor<input type='number' name='lora_sf' value='" +
            String(tracker_config.lora.spreading_factor) + "'></label>";
    html += "<label>Maximum relay hops (0-4)<input type='number' min='0' max='4' name='lora_relay_hop_limit' value='" +
            String(tracker_config.lora.relay_hop_limit) + "'></label>";
    html += "<label>ACK window (ms)<input type='number' min='100' max='30000' name='lora_ack_timeout_ms' value='" +
            String(tracker_config.lora_ack_timeout_ms) + "'></label>";
    html += "<input type='hidden' name='reboot' value='1'>";
    html += "<button type='submit'>Validate, save and reboot</button></form>";
    html += "<p>The complete transactional API is available at <code>GET/POST /api/v1/config</code>. POST bodies use <code>application/x-www-form-urlencoded</code> and must include <code>expected_revision</code>.</p>";
    html += "<form action='/start' method='POST'><button>Close setup and start tracking</button></form>";
    html += "<h3>Live logs</h3><pre id='logs'>Loading...</pre>";
    html += "<script>setInterval(()=>fetch('/logs').then(r=>r.text()).then(t=>document.getElementById('logs').innerText=t),2000)</script>";
    html += "</body></html>";
    webServer.send(200, "text/html", html);
  });

  // Compatibility HTML form. It uses the same transactional candidate/validate
  // path as the REST and BLE transports.
  webServer.on("/save", HTTP_POST, [requireWebAuthentication]() {
    if (!requireWebAuthentication()) return;
    wifi_client_connected = true;
    last_web_activity_ms = millis();
    EquineConfig::TrackerConfigV1 candidate = tracker_config;
    EquineConfigApi::PatchStatus status;
    uint32_t expected = 0;
    if (!EquineConfigApi::parseUnsigned(
          webServer.arg("expected_revision").c_str(), 1, UINT32_MAX, expected) ||
        expected != tracker_config.header.revision) {
      webServer.send(409, "text/plain", "Configuration revision conflict; reload the page");
      return;
    }
    const char* fields[] = {
      "device_id", "device_name", "wifi_ssid", "wifi_password",
      "moving_sleep_s", "stationary_sleep_s", "max_hdop",
      "min_satellites", "lora_sf", "lora_relay_hop_limit",
      "lora_ack_timeout_ms"
    };
    for (const char* field : fields) {
      if (!webServer.hasArg(field)) continue;
      const String value = webServer.arg(field);
      const auto result = EquineConfigApi::applyTrackerField(
        candidate, field, value.c_str(), status);
      if (result != EquineConfigApi::FieldResult::APPLIED) {
        webServer.send(400, "text/plain", status.error[0] ? status.error : "Invalid field");
        return;
      }
    }
    const auto ble_result = EquineConfigApi::applyTrackerField(
      candidate, "ble_debug_enabled",
      webServer.hasArg("ble_debug_enabled") ? "1" : "0", status);
    if (ble_result == EquineConfigApi::FieldResult::INVALID ||
        !commitTrackerConfigCandidate(
          candidate, true, status.error, sizeof(status.error))) {
      webServer.send(400, "text/plain", status.error[0] ? status.error : "Configuration validation failed");
      return;
    }
    config_reboot_requested = true;
    webServer.send(200, "text/html",
      "Configuration saved and validated. The tracker will reboot shortly.");
  });

  webServer.on("/api/v1/onboarding", HTTP_GET, [requireWebAuthentication]() {
    if (!requireWebAuthentication()) return;
    String out = "{\"api_version\":" + String(EquineConfigApi::API_VERSION) +
      ",\"role\":\"tracker\",\"onboarding_required\":" +
      String(tracker_onboarding_required ? "true" : "false") +
      ",\"revision\":" + String(tracker_config.header.revision) +
      ",\"transports\":[\"wifi\",\"ble_nus\"],\"config_post_content_type\":\"application/x-www-form-urlencoded\"}";
    webServer.send(200, "application/json", out);
  });

  webServer.on("/api/v1/config", HTTP_GET, [requireWebAuthentication]() {
    if (!requireWebAuthentication()) return;
    webServer.send(200, "application/json", trackerConfigJson());
  });

  webServer.on("/api/v1/config", HTTP_POST, [requireWebAuthentication]() {
    if (!requireWebAuthentication()) return;
    wifi_client_connected = true;
    last_web_activity_ms = millis();
    EquineConfigApi::PatchStatus status;
    bool reboot = false;
    if (!applyTrackerWebPatch(status, reboot)) {
      const int code = strstr(status.error, "revision conflict") ? 409 : 400;
      String out = "{\"ok\":false,\"error\":\"";
      EquineConfigApi::appendJsonEscaped(out, status.error);
      out += "\",\"current_revision\":" +
             String(tracker_config.header.revision) + "}";
      webServer.send(code, "application/json", out);
      return;
    }
    const bool needs_reboot = status.reboot_required || reboot;
    String out = "{\"ok\":true,\"revision\":" +
      String(tracker_config.header.revision) +
      ",\"changed\":" + String(status.changed ? "true" : "false") +
      ",\"reboot_required\":" + String(needs_reboot ? "true" : "false") + "}";
    webServer.send(200, "application/json", out);
    config_reboot_requested |= needs_reboot;
  });

  webServer.on("/api/v1/config/rollback", HTTP_POST, [requireWebAuthentication]() {
    if (!requireWebAuthentication()) return;
    uint32_t expected = 0;
    if (!EquineConfigApi::parseUnsigned(
          webServer.arg("expected_revision").c_str(), 1, UINT32_MAX, expected) ||
        expected != tracker_config.header.revision) {
      webServer.send(409, "application/json",
        "{\"ok\":false,\"error\":\"revision_conflict\"}");
      return;
    }
    char error[EquineConfigApi::ERROR_SIZE] = {};
    if (!rollbackTrackerConfig(error, sizeof(error))) {
      String out = "{\"ok\":false,\"error\":\"";
      EquineConfigApi::appendJsonEscaped(out, error);
      out += "\"}";
      webServer.send(400, "application/json", out);
      return;
    }
    webServer.send(200, "application/json",
      "{\"ok\":true,\"reboot_required\":true}");
    config_reboot_requested = true;
  });

  webServer.on("/api/v1/factory-reset", HTTP_POST, [requireWebAuthentication]() {
    if (!requireWebAuthentication()) return;
    if (webServer.arg("confirm") != "FACTORY_RESET") {
      webServer.send(400, "application/json",
        "{\"ok\":false,\"error\":\"confirmation_required\"}");
      return;
    }
    webServer.send(200, "application/json",
      "{\"ok\":true,\"factory_reset\":true,\"onboarding_required_after_reboot\":true}");
    config_factory_reset_requested = true;
  });

  webServer.on("/api/v1/reboot", HTTP_POST, [requireWebAuthentication]() {
    if (!requireWebAuthentication()) return;
    webServer.send(200, "application/json", "{\"ok\":true,\"rebooting\":true}");
    config_reboot_requested = true;
  });

  webServer.on("/start", HTTP_POST, [requireWebAuthentication]() {
    if (!requireWebAuthentication()) return;
    wifi_client_connected = true;
    last_web_activity_ms = millis();
    force_tracking_mode = true;
    webServer.send(200, "text/html", "Starting tracking mode. You can close this page.<br><br><small>WiFi will now disconnect.</small>");
  });

  webServer.on("/logs", HTTP_GET, [requireWebAuthentication]() {
    if (!requireWebAuthentication()) return;
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
  const uint32_t timeout_window =
    tracker_onboarding_required ? 600000UL : 120000UL;

  while (!force_tracking_mode) {
    const uint32_t now = millis();
    if (config_reboot_requested || config_factory_reset_requested) break;
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
  stopBleDebug();
  delay(100);

  if (config_factory_reset_requested) {
    prefs.clear();
    clearTrackerConfigStorage();
    delay(250);
    ESP.restart();
  }
  if (config_reboot_requested) {
    delay(250);
    ESP.restart();
  }

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
  seconds_since_last_nvs_save = 0;
}

void appendHistoryPointAbsolute(int32_t lat_micro, int32_t lon_micro, uint32_t unix_time_s) {
  StoredHistoryPoint p;
  p.lat = lat_micro;
  p.lon = lon_micro;
  p.seq = next_point_seq++;
  p.unix_time_s = unix_time_s;

  history_points[history_head] = p;
  history_head = (history_head + 1) % HISTORY_SIZE;
  if (history_count < HISTORY_SIZE) {
    history_count++;
  } else {
    logPrintln("Warning: History buffer is full! Oldest unacknowledged point will be overwritten.");
  }

  logPrintf("History append -> seq=%lu lat=%ld lon=%ld utc=%lu count=%u\n",
                (unsigned long)p.seq, (long)p.lat, (long)p.lon,
                (unsigned long)p.unix_time_s, history_count);
}

int32_t quantizeMicrodegToStepGrid(int32_t value_micro) {
  // Round to nearest DELTA_UNIT_MICRODEG to keep chain reconstructable exactly
  if (value_micro >= 0) {
    return ((value_micro + (DELTA_UNIT_MICRODEG / 2)) / DELTA_UNIT_MICRODEG) * DELTA_UNIT_MICRODEG;
  } else {
    return ((value_micro - (DELTA_UNIT_MICRODEG / 2)) / DELTA_UNIT_MICRODEG) * DELTA_UNIT_MICRODEG;
  }
}

void maybeAppendSignificantHistoryPoint(double lat_deg, double lon_deg, uint32_t unix_time_s) {
  int32_t lat_micro = (int32_t)lround(lat_deg * 1000000.0);
  int32_t lon_micro = (int32_t)lround(lon_deg * 1000000.0);

  // Quantize all stored points to the delta step grid so packet reconstruction is exact.
  lat_micro = quantizeMicrodegToStepGrid(lat_micro);
  lon_micro = quantizeMicrodegToStepGrid(lon_micro);

  if (history_count == 0) {
    appendHistoryPointAbsolute(lat_micro, lon_micro, unix_time_s);
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

  appendHistoryPointAbsolute(lat_micro, lon_micro, unix_time_s);
}

uint16_t buildDynamicPayload(uint8_t* buffer, size_t max_len, uint16_t& points_packed) {
  if (history_count == 0) return 0;

  points_packed = 0;
  const StoredHistoryPoint& root = history_points[historyIndexToRing(0)];

  HistoryPayload header{};
  header.first_seq = root.seq;
  header.root_unix_time_s = root.unix_time_s;
  header.total_dist_dam = (uint16_t)(total_distance_meters / 10.0);
  header.batt_pct = getBatteryPercentage();

  AnchorPoint anchors[32];
  uint8_t num_anchors = 0;

  // Batch body: anchor index, count, followed by repeated
  // int8 dlat, int8 dlon, ULEB128(delta seconds from previous point).
  uint8_t batches_buf[250];
  size_t batches_len = 0;
  uint8_t num_batches = 0;

  uint8_t current_anchor_idx = 255;
  uint32_t current_batch_count_ptr = 0;
  uint32_t previous_time_s = root.unix_time_s;
  const bool timestamps_available = root.unix_time_s > 0;

  points_packed = 1;

  for (uint16_t i = 1; i < history_count; i++) {
    const StoredHistoryPoint& p = history_points[historyIndexToRing(i)];

    int32_t dlat_micro = p.lat - root.lat;
    int32_t dlon_micro = p.lon - root.lon;

    int32_t dlat_steps = dlat_micro / DELTA_UNIT_MICRODEG;
    int32_t dlon_steps = dlon_micro / DELTA_UNIT_MICRODEG;

    if (dlat_steps < -32768 || dlat_steps > 32767 ||
        dlon_steps < -32768 || dlon_steps > 32767) {
      break;
    }

    uint32_t delta_time_s = 0;
    if (timestamps_available) {
      // Do not create a partially timestamped frame. The next packet can re-root
      // at the first point that again has a trustworthy monotonic UTC value.
      if (p.unix_time_s == 0 || p.unix_time_s < previous_time_s) break;
      delta_time_s = p.unix_time_s - previous_time_s;
    }
    const size_t timestamp_bytes =
      EquineProtocol::uleb128SizeU32(delta_time_s);

    uint8_t chosen_anchor = 255;
    for (uint8_t a = 0; a < num_anchors; a++) {
      int32_t rel_lat = dlat_steps - anchors[a].dlat;
      int32_t rel_lon = dlon_steps - anchors[a].dlon;
      if (rel_lat >= -128 && rel_lat <= 127 &&
          rel_lon >= -128 && rel_lon <= 127) {
        chosen_anchor = a;
        break;
      }
    }

    const bool needs_new_anchor = chosen_anchor == 255;
    if (needs_new_anchor && num_anchors >= 32) break;

    const uint8_t target_anchor_idx =
      needs_new_anchor ? num_anchors : chosen_anchor;
    const bool needs_new_batch =
      current_anchor_idx != target_anchor_idx ||
      num_batches == 0 || batches_buf[current_batch_count_ptr] == 255;

    const size_t base_len = sizeof(HistoryPayload) + 8 + 1 + 1;
    const size_t projected_anchor_count =
      num_anchors + (needs_new_anchor ? 1 : 0);
    const size_t projected_batches_len =
      batches_len + (needs_new_batch ? 2 : 0) + 2 + timestamp_bytes;
    const size_t projected_total_len =
      base_len + projected_anchor_count * sizeof(AnchorPoint) +
      projected_batches_len;

    if (projected_total_len > max_len ||
        projected_batches_len > sizeof(batches_buf)) {
      break;
    }

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
      batches_buf[batches_len++] = 0;
      num_batches++;
    }

    const int8_t rel_lat =
      (int8_t)(dlat_steps - anchors[chosen_anchor].dlat);
    const int8_t rel_lon =
      (int8_t)(dlon_steps - anchors[chosen_anchor].dlon);
    batches_buf[batches_len++] = static_cast<uint8_t>(rel_lat);
    batches_buf[batches_len++] = static_cast<uint8_t>(rel_lon);

    const size_t encoded_timestamp_bytes = EquineProtocol::encodeUleb128U32(
      delta_time_s,
      batches_buf + batches_len,
      sizeof(batches_buf) - batches_len
    );
    if (encoded_timestamp_bytes == 0) break;
    batches_len += encoded_timestamp_bytes;
    batches_buf[current_batch_count_ptr]++;

    previous_time_s = p.unix_time_s;
    points_packed++;
  }

  #define REQUIRE_SPACE(n) do { \
    if (offset + (n) > max_len) return 0; \
  } while (0)

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
  refreshBatteryCache(force);
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
        snprintf(lines[2], sizeof(lines[2]), "%.13s", wifi_ap_active ? "LoRaTracker" : "Searching...");
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
        const double speed = last_effective_speed_kmph;
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
  serviceBleDebug();
  const uint32_t now = millis();
  const bool button_pressed = digitalRead(USER_BTN_PIN) == LOW;

  if (button_pressed) {
    if (!previous_button_pressed && (uint32_t)(now - last_button_event_ms) > BUTTON_DEBOUNCE_MS) {
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
      // Just released. Derive the duration from timestamps rather than the
      // periodically refreshed hold counter; otherwise a short press released
      // before the next service pass can incorrectly look like a zero-length
      // press and fail to advance the page.
      const uint32_t released_hold_duration_ms = now - button_press_start_ms;
      button_hold_duration_ms = released_hold_duration_ms;
      button_is_held = false;
      last_button_event_ms = now;

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
          clearTrackerConfigStorage();
          delay(2000);
          ESP.restart();
        } else if (pending_confirmation == ConfirmationState::BLE_TOGGLE) {
          logPrintln("User confirmed BLE toggle via button!");
          const bool enable_ble = !ble_debug_enabled;
          ble_debug_enabled = enable_ble;
          tracker_config.ble_debug_enabled = enable_ble ? 1 : 0;
          if (!saveTrackerConfig(true)) {
            ble_debug_enabled = !enable_ble;
            tracker_config.ble_debug_enabled = ble_debug_enabled ? 1 : 0;
            setLastError("Config save fail");
            logPrintln("BLE toggle cancelled because config save failed.");
            pending_confirmation = ConfirmationState::NONE;
            force_refresh = true;
            return;
          }
          setLastError(enable_ble ? "BLE Debug ON" : "BLE Debug OFF");

          // Defer all BLE stack work until after this input handler has
          // cleared the confirmation state and returned.
          if (enable_ble) {
            ble_enable_transition_requested = true;
          } else {
            ble_disable_transition_requested = true;
          }
        }
        pending_confirmation = ConfirmationState::NONE;
      } else {
        // No pending confirmation. Process hold duration.
        if (button_hold_duration_ms >= 12000) {
          pending_confirmation = ConfirmationState::FACTORY_RESET;
          confirmation_timeout_ms = now + 10000;
          requestDisplayWake(DISPLAY_BUTTON_TIMEOUT_MS, false);
        } else if (button_hold_duration_ms >= 8000) {
          pending_confirmation = ConfirmationState::BLE_TOGGLE;
          confirmation_timeout_ms = now + 10000;
          requestDisplayWake(DISPLAY_BUTTON_TIMEOUT_MS, false);
        } else if (button_hold_duration_ms >= 4000) {
          pending_confirmation = ConfirmationState::DISTANCE_RESET;
          confirmation_timeout_ms = now + 10000;
          requestDisplayWake(DISPLAY_BUTTON_TIMEOUT_MS, false);
        } else {
          // Every detected, debounced short press advances exactly one page.
          // Rendering is done once here, after classification, so a normal
          // refresh cannot overwrite or swallow the page transition.
          if (pending_confirmation == ConfirmationState::NONE) {
            requestDisplayWake(DISPLAY_BUTTON_TIMEOUT_MS, true);
          } else {
            pending_confirmation = ConfirmationState::NONE;
            requestDisplayWake(DISPLAY_BUTTON_TIMEOUT_MS, false);
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
  pinMode(GNSS_RST_PIN, INPUT);

  // Start with GNSS disabled during optional WiFi/OTA setup.
  digitalWrite(VEXT_CTRL_PIN, LOW);
  digitalWrite(GNSS_PWR_PIN, LOW);

  pinMode(ADC_CTRL, OUTPUT);
  digitalWrite(ADC_CTRL, LOW);
#endif
}

bool postBootButtonRequestsWifiSetup() {
  // GPIO0 is an ESP32-S3 boot-strapping pin. Holding it low during reset can
  // enter the ROM download bootloader before this firmware runs, leaving the
  // application display black. Therefore the setup gesture is deliberately
  // accepted only after the application has booted and illuminated the TFT.
  setTrackerPhase("Hold for WiFi");
  requestDisplayWake(WIFI_SETUP_POST_BOOT_WINDOW_MS + 1000, false);

  const uint32_t window_start_ms = millis();
  uint32_t hold_start_ms = 0;
  bool released_since_boot = digitalRead(USER_BTN_PIN) != LOW;

  while ((uint32_t)(millis() - window_start_ms) <
         WIFI_SETUP_POST_BOOT_WINDOW_MS) {
    const bool pressed = digitalRead(USER_BTN_PIN) == LOW;

    if (!pressed) {
      released_since_boot = true;
      hold_start_ms = 0;
    } else if (released_since_boot) {
      if (hold_start_ms == 0) hold_start_ms = millis();
      if ((uint32_t)(millis() - hold_start_ms) >=
          WIFI_SETUP_BOOT_HOLD_MS) {
        // Consume this gesture so it is not replayed as a status-page press.
        previous_button_pressed = true;
        button_is_held = false;
        button_hold_duration_ms = 0;
        last_button_event_ms = millis();
        setTrackerPhase("WiFi requested");
        renderStatusPage();
        return true;
      }
    }

    delay(10);
  }

  previous_button_pressed = digitalRead(USER_BTN_PIN) == LOW;
  button_is_held = false;
  button_hold_duration_ms = 0;
  last_button_event_ms = millis();
  return false;
}

double angularDifferenceDegrees(double a, double b) {
  double difference = fabs(a - b);
  while (difference >= 360.0) difference -= 360.0;
  return difference > 180.0 ? 360.0 - difference : difference;
}

bool listenForGpsFix(uint32_t listen_duration_ms,
                     uint32_t current_elapsed_s,
                     uint32_t cycle_start_ms,
                     bool& movement_accepted,
                     bool& position_accepted) {
  const uint32_t listen_start_ms = millis();

  while ((uint32_t)(millis() - listen_start_ms) < listen_duration_ms) {
    bool processed_gps_bytes = false;

    while (Serial2.available() > 0) {
      gps.encode(Serial2.read());
      processed_gps_bytes = true;
    }

    if (gps.location.isUpdated() && gps.location.isValid() &&
        gps.location.age() < 1500) {
      if (fabs(gps.location.lat()) < 0.1 && fabs(gps.location.lng()) < 0.1) {
        continue;
      }

      if (gps.hdop.isValid() && gps.satellites.isValid() &&
          gps.hdop.hdop() <= MAX_HDOP &&
          gps.satellites.value() >= MIN_SATELLITES) {
        setTrackerPhase("GPS fix acquired");
        logPrintf("Solid Fix Acquired! (Sats: %u, HDOP: %.2f)\n",
                  gps.satellites.value(), gps.hdop.hdop());

        const double candidate_lat = gps.location.lat();
        const double candidate_lng = gps.location.lng();
        const uint32_t fix_unix_time_s =
          updateGnssAssistanceState(current_elapsed_s);

        if (has_initial_fix) {
          const double distance_from_accepted = TinyGPSPlus::distanceBetween(
            last_lat, last_lng, candidate_lat, candidate_lng
          );

          double allowed_distance = current_elapsed_s * MAX_SPEED_MPS;
          if (allowed_distance < 50.0) allowed_distance = 50.0;

          if (distance_from_accepted > allowed_distance) {
            teleport_strikes++;
            movement_evidence_streak = 0;
            logPrintf("TELEPORT! Moved: %.0f m. Allowed: %.0f m. (Strike %u/3)\n",
                      distance_from_accepted, allowed_distance, teleport_strikes);

            if (teleport_strikes >= 3) {
              logPrintln("3 Strikes! Resetting position.");
              has_initial_fix = false;
              has_previous_raw_fix = false;
              teleport_strikes = 0;
            }
            return false;
          }

          teleport_strikes = 0;

          // TinyGPS++ fields are updated by different NMEA sentences. Only use
          // receiver speed when it was updated during this acquisition; always
          // calculate a second, epoch-independent speed from displacement and
          // elapsed time since last_lat/last_lng were accepted.
          const bool nmea_speed_updated = gps.speed.isUpdated();
          const bool nmea_speed_fresh =
            nmea_speed_updated && gps.speed.isValid() && gps.speed.age() < 1500;
          const double nmea_speed_kmph =
            nmea_speed_fresh ? gps.speed.kmph() : 0.0;

          uint32_t accepted_position_age_s =
            seconds_since_last_accepted_position +
            (uint32_t)((millis() - cycle_start_ms) / 1000U);
          if (accepted_position_age_s == 0) accepted_position_age_s = 1;
          const double derived_speed_kmph =
            (distance_from_accepted * 3.6) / accepted_position_age_s;
          const double effective_speed_kmph =
            nmea_speed_fresh ?
            max(nmea_speed_kmph, derived_speed_kmph) : derived_speed_kmph;
          last_effective_speed_kmph = effective_speed_kmph;

          const bool movement_by_speed =
            effective_speed_kmph >= MOVEMENT_SPEED_THRESHOLD_KMPH;
          const bool movement_by_large_displacement =
            distance_from_accepted >= MOVEMENT_DISPLACEMENT_THRESHOLD_M;

          bool directionally_consistent = false;
          double raw_step_m = 0.0;
          double direction_difference_deg = 180.0;
          if (has_previous_raw_fix &&
              distance_from_accepted >= MOVEMENT_EVIDENCE_DISTANCE_M) {
            raw_step_m = TinyGPSPlus::distanceBetween(
              previous_raw_lat, previous_raw_lng,
              candidate_lat, candidate_lng
            );
            if (raw_step_m >= MOVEMENT_EVIDENCE_STEP_M) {
              const double previous_offset_m = TinyGPSPlus::distanceBetween(
                last_lat, last_lng, previous_raw_lat, previous_raw_lng
              );
              if (previous_offset_m >= MOVEMENT_EVIDENCE_STEP_M) {
                const double outward_course = TinyGPSPlus::courseTo(
                  last_lat, last_lng, previous_raw_lat, previous_raw_lng
                );
                const double step_course = TinyGPSPlus::courseTo(
                  previous_raw_lat, previous_raw_lng,
                  candidate_lat, candidate_lng
                );
                direction_difference_deg =
                  angularDifferenceDegrees(outward_course, step_course);
                directionally_consistent =
                  direction_difference_deg <= MOVEMENT_DIRECTION_TOLERANCE_DEG;
              }
            }
          }

          if (movement_by_speed || movement_by_large_displacement) {
            movement_evidence_streak = MOVEMENT_EVIDENCE_REQUIRED;
          } else if (directionally_consistent) {
            if (movement_evidence_streak < MOVEMENT_EVIDENCE_REQUIRED) {
              movement_evidence_streak++;
            }
          } else {
            movement_evidence_streak = 0;
          }

          const bool movement_detected =
            movement_by_speed || movement_by_large_displacement ||
            movement_evidence_streak >= MOVEMENT_EVIDENCE_REQUIRED;

          previous_raw_lat = candidate_lat;
          previous_raw_lng = candidate_lng;
          has_previous_raw_fix = true;

          if (movement_detected) {
            // Movement state controls the power policy independently of whether
            // this individual position delta is large enough to accumulate.
            movement_accepted = true;
          }

          if (movement_detected &&
              distance_from_accepted > MIN_DISTANCE_METERS) {
            total_distance_meters += distance_from_accepted;
            last_lat = candidate_lat;
            last_lng = candidate_lng;
            movement_evidence_streak = 0;
            position_accepted = true;
            logPrintf("Accepted movement -> %.2f m, effective %.2f km/h "
                      "(derived %.2f, GNSS %.2f%s), total %.2f m\n",
                      distance_from_accepted, effective_speed_kmph,
                      derived_speed_kmph, nmea_speed_kmph,
                      nmea_speed_fresh ? " fresh" : " stale/unavailable",
                      total_distance_meters);
            maybeAppendSignificantHistoryPoint(candidate_lat, candidate_lng, fix_unix_time_s);
          } else if (movement_detected) {
            logPrintf("Movement detected by speed/hysteresis; %.2f m delta not accumulated.\n",
                      distance_from_accepted);
          } else {
            logPrintf("Stationary evidence -> offset %.2f m, raw step %.2f m, "
                      "effective %.2f km/h (derived %.2f, GNSS %.2f%s), "
                      "direction diff %.0f deg, streak %u/%u\n",
                      distance_from_accepted, raw_step_m, effective_speed_kmph,
                      derived_speed_kmph, nmea_speed_kmph,
                      nmea_speed_fresh ? " fresh" : " stale/unavailable",
                      direction_difference_deg, movement_evidence_streak,
                      MOVEMENT_EVIDENCE_REQUIRED);
          }
        } else {
          last_lat = candidate_lat;
          last_lng = candidate_lng;
          previous_raw_lat = candidate_lat;
          previous_raw_lng = candidate_lng;
          has_previous_raw_fix = true;
          movement_evidence_streak = 0;
          has_initial_fix = true;
          position_accepted = true;
          seconds_since_last_accepted_position = 0;
          last_effective_speed_kmph = 0.0;
          maybeAppendSignificantHistoryPoint(candidate_lat, candidate_lng, fix_unix_time_s);
        }

        seconds_since_last_fix = 0;
        last_fix_acquired_ms = millis();
        return true;
      }

      logPrintf("Weak Fix -> Sats: %u, HDOP: %.2f, Age: %u ms\n",
                gps.satellites.isValid() ? gps.satellites.value() : 0,
                gps.hdop.isValid() ? gps.hdop.hdop() : -1.0,
                gps.location.age());
    }

    serviceDisplayAndButton();
    if (!processed_gps_bytes) delay(10);
  }

  return false;
}

uint32_t chooseGpsAcquisitionTimeoutMs() {
  const bool full_attempt_due =
    consecutive_no_fix_cycles == 0 ||
    seconds_since_last_full_gnss_attempt >= GPS_FULL_RETRY_INTERVAL_S;

  if (full_attempt_due) {
    seconds_since_last_full_gnss_attempt = 0;
    return GPS_FULL_TIMEOUT_MS;
  }
  if (consecutive_no_fix_cycles == 1) {
    return GPS_SECOND_NO_FIX_TIMEOUT_MS;
  }
  if (consecutive_no_fix_cycles == 2) {
    return GPS_THIRD_NO_FIX_TIMEOUT_MS;
  }
  return GPS_QUICK_PROBE_TIMEOUT_MS;
}

uint64_t chooseNextSleepDurationUs(bool fix_found, bool movement_accepted) {
  if (!fix_found) {
    if (consecutive_no_fix_cycles < UINT8_MAX) {
      consecutive_no_fix_cycles++;
    }
    stationary_fix_streak = 0;
    movement_evidence_streak = 0;

    if (consecutive_no_fix_cycles == 1) return NO_FIX_SLEEP_1_US;
    if (consecutive_no_fix_cycles == 2) return NO_FIX_SLEEP_2_US;
    if (consecutive_no_fix_cycles == 3) return NO_FIX_SLEEP_3_US;
    return NO_FIX_SLEEP_MAX_US;
  }

  consecutive_no_fix_cycles = 0;
  if (movement_accepted) {
    stationary_fix_streak = 0;
    return SLEEP_DURATION_US;
  }

  if (stationary_fix_streak < UINT16_MAX) stationary_fix_streak++;
  if (stationary_fix_streak >= STATIONARY_FIXES_FOR_MAX_SLEEP) {
    return LONG_STATIONARY_SLEEP_DURATION_US;
  }
  if (stationary_fix_streak >= STATIONARY_FIXES_FOR_LONG_SLEEP) {
    return STATIONARY_SLEEP_DURATION_US;
  }
  return SLEEP_DURATION_US;
}


void saturatingAddSeconds(uint32_t& value, uint32_t increment) {
  if (UINT32_MAX - value < increment) {
    value = UINT32_MAX;
  } else {
    value += increment;
  }
}

uint32_t getLoRaRetryBackoffSeconds() {
  if (consecutive_lora_failures == 0) return 0;
  if (consecutive_lora_failures == 1) return LORA_RETRY_BACKOFF_1_S;
  if (consecutive_lora_failures == 2) return LORA_RETRY_BACKOFF_2_S;
  if (consecutive_lora_failures == 3) return LORA_RETRY_BACKOFF_3_S;
  return LORA_RETRY_BACKOFF_MAX_S;
}

void recordLoRaFailure(const char* reason) {
  if (consecutive_lora_failures < UINT8_MAX) {
    consecutive_lora_failures++;
  }
  seconds_since_last_lora_attempt = 0;
  logPrintf("LoRa failure #%u (%s); next retry in %u s.\n",
            consecutive_lora_failures,
            reason ? reason : "unknown",
            getLoRaRetryBackoffSeconds());
}

void recordLoRaAckSuccess() {
  consecutive_lora_failures = 0;
  seconds_since_last_lora_attempt = 0;
  seconds_since_last_tx = 0;
}

void setup() {
  // Determine wake type before any optional USB wait.
  last_reset_reason = esp_reset_reason();
  const bool hard_boot = (last_reset_reason != ESP_RST_DEEPSLEEP);
  const esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();

  Serial.begin(115200);
#if defined(BOARD_WIRELESS_TRACKER)
  if (hard_boot) {
    const uint32_t start_ms = millis();
    while (!Serial && (uint32_t)(millis() - start_ms) < 4000) {
      delay(10);
    }
  }
#else
  if (hard_boot) delay(250);
#endif

  if (!EquineCrypto::selfTest()) {
    Serial.println("Fatal: AES-GCM self-test failed.");
    while (true) delay(1000);
  }

  prefs.begin("tracker", false);
  initializeAdminCredential();
  if (!loadTrackerConfig()) {
    // The factory configuration is validated in code, so reaching this branch
    // indicates an NVS write failure. Continue with the in-memory defaults.
    makeTrackerFactoryConfig();
    applyTrackerConfigRuntimeState();
    tracker_onboarding_required = true;
    logPrintln("Warning: running with unsaved factory tracker configuration.");
  }

  char device_hash_text[17];
  EquineProtocol::formatDeviceHash(
    TRACKER_DEVICE_HASH, device_hash_text, sizeof(device_hash_text)
  );
  logPrintf(
    "Protocol v%u tracker_id=%s device_hash=%s history_schema=%u ack_schema=%u\n",
    EquineProtocol::TRANSPORT_VERSION,
    TRACKER_ID,
    device_hash_text,
    EquineProtocol::HISTORY_SCHEMA_VERSION,
    EquineProtocol::ACK_SCHEMA_VERSION
  );

  initBoardPins();
  pinMode(USER_BTN_PIN, INPUT_PULLUP);
  bool wifi_setup_requested = false;

  if (BATTERY_SENSE_ENABLED) {
    analogReadResolution(12);
    analogSetPinAttenuation(BATT_PIN, ADC_11db);
  }

  if (hard_boot) {
    logPrintln("\n[boot] HARD BOOT DETECTED! Loading data from flash memory...");

    total_distance_meters = prefs.getDouble("dist", 0.0);
    last_saved_dist = total_distance_meters;

    const bool had_boot_counter = prefs.isKey("boot_id");
    const uint32_t previous_boot_id = prefs.getUInt("boot_id", 0);
    if ((!had_boot_counter && !tracker_onboarding_required) ||
        previous_boot_id == UINT32_MAX) {
      esp_fill_random(
        tracker_config.lora_aead_key,
        sizeof(tracker_config.lora_aead_key));
      saveTrackerConfig(true);
      writeTrackerProvisionedFlag(false);
      tracker_onboarding_required = true;
      logPrintln("LoRa key rotated because the monotonic boot counter was unavailable; re-register this tracker.");
      boot_id = 1;
    } else {
      boot_id = previous_boot_id + 1;
    }
    prefs.putUInt("boot_id", boot_id);
    esp_fill_random(&tx_nonce_prefix, sizeof(tx_nonce_prefix));
    if (tx_nonce_prefix == 0) tx_nonce_prefix = 1;
    next_tx_counter = 0;

    has_initial_fix = false;
    seconds_since_last_fix = 0;
    teleport_strikes = 0;
    wakeup_counter = 0;
    seconds_since_daily_reset = prefs.getUInt("daily_age", 0);
    next_point_seq = 0;
    stationary_fix_streak = 0;
    consecutive_no_fix_cycles = 0;
    seconds_since_last_tx = LORA_TX_INTERVAL_S;
    seconds_since_last_lora_attempt = LORA_RETRY_BACKOFF_MAX_S;
    consecutive_lora_failures = 0;
    seconds_since_last_nvs_save = 0;
    seconds_since_last_full_gnss_attempt = GPS_FULL_RETRY_INTERVAL_S;
    seconds_since_last_accepted_position = 0;
    last_effective_speed_kmph = 0.0;
    has_previous_raw_fix = false;
    movement_evidence_streak = 0;
    last_gnss_utc_epoch_s = 0;
    last_gnss_altitude_m = 0.0;
    has_gnss_utc_time = false;
    clearHistory();

    struct timeval tv;
    gettimeofday(&tv, NULL);
    session_start_time_us =
      (uint64_t)tv.tv_sec * 1000000ULL + tv.tv_usec;
  } else {
    wakeup_counter++;
    boot_id = prefs.getUInt("boot_id", 1);
    if (!validateRtcHistory()) {
      logPrintln("RTC history metadata/CRC validation failed; discarding retained queue.");
      if (boot_id == UINT32_MAX) {
        esp_fill_random(
          tracker_config.lora_aead_key,
          sizeof(tracker_config.lora_aead_key));
        saveTrackerConfig(true);
        writeTrackerProvisionedFlag(false);
        tracker_onboarding_required = true;
        boot_id = 1;
        logPrintln("LoRa key rotated after boot epoch exhaustion; re-register this tracker.");
      } else {
        boot_id++;
      }
      prefs.putUInt("boot_id", boot_id);
      esp_fill_random(&tx_nonce_prefix, sizeof(tx_nonce_prefix));
      if (tx_nonce_prefix == 0) tx_nonce_prefix = 1;
      next_tx_counter = 0;
      history_head = 0;
      history_count = 0;
      next_point_seq = 0;
      memset(history_points, 0, sizeof(history_points));
      memset(&rtc_history_metadata, 0, sizeof(rtc_history_metadata));
    }
  }

  if (seconds_since_daily_reset >= SECONDS_PER_DAY) {
    resetDailyDistanceAndHistory();
  }

  Serial2.begin(GPS_BAUD, SERIAL_8N1, RXD2, TXD2);

#if !defined(BOARD_WIRELESS_TRACKER)
  // The BN-220 is powered independently and is physically awake after a hard
  // boot. Mark that state so the initial sleep command is actually sent.
  if (hard_boot) {
    gps_powered = true;
    sleepGPS();
  }
#endif

  if (hard_boot || wakeup_cause == ESP_SLEEP_WAKEUP_EXT0) {
    initDisplay();
    setTrackerPhase(hard_boot ? "Booting" : "Wake cycle");
    if (hard_boot) requestDisplayWake(30000, false);
  } else {
    // Timer wake: leave the display completely uninitialized.
    setTrackerPhase("Wake cycle");
  }

  if (hard_boot) {
    wifi_setup_requested = tracker_onboarding_required || postBootButtonRequestsWifiSetup();
  }

  // A button-only wake opens the status UI and, when enabled, a bounded BLE
  // connection window. It otherwise resumes the original sleep deadline.
  if (!hard_boot && wakeup_cause == ESP_SLEEP_WAKEUP_EXT0) {
    previous_button_pressed = digitalRead(USER_BTN_PIN) == LOW;
    last_button_event_ms = millis();
    setTrackerPhase("Button status");
    requestDisplayWake(DISPLAY_BUTTON_TIMEOUT_MS, false);

    if (ble_debug_enabled) {
      startBleDebugWindow(BLE_CONNECTION_WINDOW_MS);
    }

    while ((int32_t)(display_on_until_ms - millis()) > 0 ||
           isBleConnectionWindowActive()) {
      serviceDisplayAndButton();
      if (deviceConnected) break;
      delay(10);
    }
    turnOffDisplay();

    struct timeval tv;
    gettimeofday(&tv, NULL);
    const uint64_t now_us =
      (uint64_t)tv.tv_sec * 1000000ULL + tv.tv_usec;

    if (!isDebugModeActive() && target_wakeup_time_us > now_us) {
      stopBleDebug();
#if defined(BOARD_WIRELESS_TRACKER)
      digitalWrite(GNSS_PWR_PIN, LOW);
      pinMode(GNSS_RST_PIN, INPUT);
      digitalWrite(VEXT_CTRL_PIN, LOW);
      display_initialized = false;
      display_awake = false;
#endif
      prefs.end();
      const uint64_t sleep_remaining_us = target_wakeup_time_us - now_us;
      sealRtcHistory();
      esp_sleep_enable_ext0_wakeup((gpio_num_t)USER_BTN_PIN, 0);
      esp_sleep_enable_timer_wakeup(sleep_remaining_us);
      esp_deep_sleep_start();
    }
  }

  // A BLE enable request uses a one-second deep-sleep reset so the stack is
  // recreated from a clean boot. Open the connection window on that wake.
  if (ble_open_window_on_next_wake) {
    ble_open_window_on_next_wake = false;
    if (ble_debug_enabled) {
      startBleDebugWindow(BLE_CONNECTION_WINDOW_MS);
      waitForBleConnectionWindow();
    }
  }

  if (wifi_setup_requested) {
    runWifiSetupMode();
  } else if (hard_boot) {
    logPrintln("WiFi setup skipped. After power-up, hold the button when the display prompts for WiFi.");
  }

  // BLE is on-demand and bounded. A connected client may deliberately keep
  // the device awake; no connection means BLE is shut down before tracking.
  if (hard_boot && !tracker_onboarding_required && ble_debug_enabled && !deviceConnected) {
    startBleDebugWindow(BLE_CONNECTION_WINDOW_MS);
    waitForBleConnectionWindow();
  }

  setTrackerPhase("GNSS power-up");
  wakeupGPS();
#if defined(BOARD_WIRELESS_TRACKER)
  if (hard_boot) configureUc6580OutputIfNeeded();
  sendUc6580Assistance();
#endif

  logPrintln("\n--- LoRa Tracker Waking Up ---");
  logRTCState();

  // LoRa is initialized only inside performTrackingCycle() when a batch is due.
  lora_initialized = false;
  setTrackerPhase("Ready");
  serviceDisplayAndButton(true);

  if (!isDebugModeActive()) {
    display_on_until_ms = 0;
    serviceDisplayAndButton(true);
    performTrackingCycle();
  } else {
    logPrintln("BLE debug client connected; entering live debug operation.");
  }
}

void performTrackingCycle() {
  const uint32_t cycle_start_ms = millis();
  if (!gps_powered) {
    setTrackerPhase("GNSS power-up");
    wakeupGPS();
#if defined(BOARD_WIRELESS_TRACKER)
    sendUc6580Assistance();
#endif
  }
  setTrackerPhase("GPS acquisition");
  serviceDisplayAndButton(true);

  // =====================================================
  // GPS ACQUISITION
  // =====================================================
  const uint32_t fix_age_at_cycle_start = seconds_since_last_fix;
  const uint32_t acquisition_start_ms = millis();
  bool cycle_fix_found = false;
  bool cycle_movement_accepted = false;
  bool cycle_position_accepted = false;

  if (has_initial_fix && fix_age_at_cycle_start > MAX_FIX_AGE_S) {
    logPrintf("WARNING: Last fix is %u seconds old. Discarding it!\n",
              fix_age_at_cycle_start);
    has_initial_fix = false;
  }

  const uint32_t gps_timeout_ms = chooseGpsAcquisitionTimeoutMs();
  logPrintf("GNSS acquisition policy -> no_fix=%u, full_retry_age=%u s, deadline=%u ms.\n",
            consecutive_no_fix_cycles,
            seconds_since_last_full_gnss_attempt,
            gps_timeout_ms);
  uint32_t first_window_ms = GPS_INITIAL_LISTEN_MS;
  if (first_window_ms > gps_timeout_ms) first_window_ms = gps_timeout_ms;
  cycle_fix_found = listenForGpsFix(
    first_window_ms,
    fix_age_at_cycle_start,
    cycle_start_ms,
    cycle_movement_accepted,
    cycle_position_accepted
  );

  while (!cycle_fix_found) {
    const uint32_t elapsed_ms = millis() - acquisition_start_ms;
    if (elapsed_ms >= gps_timeout_ms) break;

    uint32_t remaining_ms = gps_timeout_ms - elapsed_ms;
    uint32_t sleep_chunk_ms = GPS_LIGHT_SLEEP_CHUNK_MS;
    if (sleep_chunk_ms > remaining_ms) sleep_chunk_ms = remaining_ms;

    if (isDebugModeActive()) {
      setTrackerPhase("GPS wait (debug)");
      responsiveDelay(sleep_chunk_ms);
    } else {
      setTrackerPhase("GPS light sleep");
      Serial.flush();
      esp_sleep_enable_ext0_wakeup((gpio_num_t)USER_BTN_PIN, 0);
      esp_sleep_enable_timer_wakeup((uint64_t)sleep_chunk_ms * 1000ULL);
      esp_light_sleep_start();

      if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
        previous_button_pressed = digitalRead(USER_BTN_PIN) == LOW;
        last_button_event_ms = millis();
        logPrintln("Button pressed during GNSS light sleep.");
        requestDisplayWake(DISPLAY_BUTTON_TIMEOUT_MS, false);
      }
    }

    const uint32_t elapsed_after_sleep_ms = millis() - acquisition_start_ms;
    if (elapsed_after_sleep_ms >= gps_timeout_ms) break;

    remaining_ms = gps_timeout_ms - elapsed_after_sleep_ms;
    uint32_t listen_ms = GPS_LISTEN_WINDOW_MS;
    if (listen_ms > remaining_ms) listen_ms = remaining_ms;

    setTrackerPhase("GPS listen");
    const uint32_t current_elapsed_s =
      fix_age_at_cycle_start + elapsed_after_sleep_ms / 1000U;
    cycle_fix_found = listenForGpsFix(
      listen_ms,
      current_elapsed_s,
      cycle_start_ms,
      cycle_movement_accepted,
      cycle_position_accepted
    );
  }

  const uint32_t acquisition_elapsed_ms = millis() - acquisition_start_ms;
  uint32_t current_elapsed_s = 0;
  if (!cycle_fix_found) {
    current_elapsed_s =
      fix_age_at_cycle_start + acquisition_elapsed_ms / 1000U;
    logPrintf("No confident GNSS fix before the %u ms deadline.\n",
              gps_timeout_ms);
  }

  const uint64_t next_sleep_duration_us =
    chooseNextSleepDurationUs(cycle_fix_found, cycle_movement_accepted);
  const uint32_t next_sleep_s =
    (uint32_t)(next_sleep_duration_us / 1000000ULL);
  logPrintf("Power policy -> fix=%s moved=%s stationary_streak=%u no_fix=%u next_sleep=%u s\n",
            cycle_fix_found ? "yes" : "no",
            cycle_movement_accepted ? "yes" : "no",
            stationary_fix_streak,
            consecutive_no_fix_cycles,
            next_sleep_s);

  // On ordinary timer wakes the display is off, so the shared VEXT rail can
  // be cut immediately. A user-requested display remains available until the
  // final sleep transition, and BLE debug deliberately keeps GNSS powered.
  if (!isDebugModeActive() && !display_awake) {
    sleepGPS();
  }

  // =====================================================
  // FLASH SAVE
  // =====================================================
  bool should_save_to_flash = false;
  if ((total_distance_meters - last_saved_dist) >= SAVE_DIST_THRESHOLD) {
    should_save_to_flash = true;
  }
  if (seconds_since_last_nvs_save >= NVS_SAVE_INTERVAL_S) {
    should_save_to_flash = true;
  }

  if (should_save_to_flash) {
    setTrackerPhase("Saving state");
    serviceDisplayAndButton(true);
    logPrintln("Saving current daily distance and reset age to flash.");
    prefs.putDouble("dist", total_distance_meters);
    const uint32_t projected_daily_age =
      seconds_since_daily_reset +
      ((millis() - cycle_start_ms) / 1000U) +
      next_sleep_s;
    prefs.putUInt("daily_age", projected_daily_age);
    last_saved_dist = total_distance_meters;
    seconds_since_last_nvs_save = 0;
  }

  // =====================================================
  // PACK AND TRANSMIT
  // =====================================================
  const bool lora_batch_ready =
    history_count > 0 &&
    (history_count >= LORA_TX_MIN_POINTS ||
     seconds_since_last_tx >= LORA_TX_INTERVAL_S ||
     history_count >= (HISTORY_SIZE - 16));
  const uint32_t lora_retry_backoff_s = getLoRaRetryBackoffSeconds();
  const bool lora_retry_ready =
    consecutive_lora_failures == 0 ||
    seconds_since_last_lora_attempt >= lora_retry_backoff_s;
  const bool tx_due = lora_batch_ready && lora_retry_ready;

  if (tx_due) {
    setTrackerPhase("LoRa transmit");
    serviceDisplayAndButton(true);
    logPrintf("LoRa batch due: %u queued points, %u s since last ACK.\n",
              history_count, seconds_since_last_tx);

    seconds_since_last_lora_attempt = 0;
    setTrackerPhase("LoRa init");
    lora_initialized = initLoRaRadio();
    if (!lora_initialized) {
      setLastError("LoRa initialization failed");
      last_tx_status = 0;
      logPrintln("LoRa initialization failed; retaining queued data for retry.");
      recordLoRaFailure("radio init");
    }

    for (int tx_loop = 0; lora_initialized && tx_loop < 5; tx_loop++) {
      if (history_count == 0) break;

      constexpr size_t MAX_SECURE_PLAINTEXT =
        EquineRelay::MAX_PACKET_SIZE - sizeof(EquineRelay::LinkHeaderV1) -
        sizeof(SecureFrameHeader) - EquineProtocol::AEAD_TAG_SIZE;
      uint8_t plaintext_buffer[MAX_SECURE_PLAINTEXT];
      uint16_t points_packed = 0;
      const size_t plaintext_len = buildDynamicPayload(
        plaintext_buffer, sizeof(plaintext_buffer), points_packed
      );

      if (plaintext_len == 0 || points_packed == 0) {
        logPrintln("Skipping LoRa send because payload build failed.");
        break;
      }

      const StoredHistoryPoint& packet_root =
        history_points[historyIndexToRing(0)];
      if (next_tx_counter == UINT32_MAX) {
        esp_fill_random(&tx_nonce_prefix, sizeof(tx_nonce_prefix));
        if (tx_nonce_prefix == 0) tx_nonce_prefix = 1;
        next_tx_counter = 0;
      }
      const uint32_t transmit_counter = next_tx_counter++;
      SecureFrameHeader secure_header = EquineProtocol::makeSecureFrameHeader(
        EquineProtocol::MessageType::HISTORY,
        EquineProtocol::HISTORY_SCHEMA_VERSION,
        TRACKER_DEVICE_HASH,
        tx_nonce_prefix,
        boot_id,
        transmit_counter,
        packet_root.unix_time_s > 0 ? EquineProtocol::FLAG_HAS_TIMESTAMPS
                                    : EquineProtocol::FLAG_NONE);
      uint8_t payload_buffer[255];
      const EquineRelay::LinkHeaderV1 link_header =
        EquineRelay::makeOriginHeader(LORA_RELAY_HOP_LIMIT);
      memcpy(payload_buffer, &link_header, sizeof(link_header));
      memcpy(payload_buffer + sizeof(link_header),
             &secure_header, sizeof(secure_header));
      uint8_t* ciphertext = payload_buffer + sizeof(link_header) +
                            sizeof(secure_header);
      uint8_t* tag = ciphertext + plaintext_len;
      if (!EquineCrypto::encrypt(
            tracker_config.lora_aead_key,
            secure_header,
            plaintext_buffer,
            plaintext_len,
            ciphertext,
            tag)) {
        logPrintln("AES-GCM history encryption failed; retaining queued data.");
        recordLoRaFailure("encryption");
        break;
      }
      const size_t payload_len =
        sizeof(link_header) + sizeof(secure_header) + plaintext_len +
        EquineProtocol::AEAD_TAG_SIZE;

      last_tx_bytes = payload_len;
      last_tx_points = points_packed;
      last_ack_status = -1;
      const bool tx_result = transmitLoRaPacket(payload_buffer, payload_len);
      last_tx_status = tx_result ? 1 : 0;
      serviceDisplayAndButton(true);

      if (!tx_result) {
        setLastError("LoRa transmit failed");
        logPrintln("LoRa transmit failed.");
        recordLoRaFailure("transmit");
        break;
      }

      setTrackerPhase("Waiting for ACK");
      logPrintf("LoRa sent %u bytes (%u points); ACK window %u ms.\n",
                payload_len, points_packed, LORA_ACK_TIMEOUT_MS);

      uint8_t rx_buffer[255];
      const int rx_result = receiveLoRaAck(
        rx_buffer, sizeof(rx_buffer), LORA_ACK_TIMEOUT_MS
      );
      bool ack_received = false;

      const size_t expected_ack_size =
        sizeof(EquineRelay::LinkHeaderV1) + sizeof(SecureFrameHeader) +
        sizeof(AckPayload) +
        EquineProtocol::AEAD_TAG_SIZE;
      if (rx_result == static_cast<int>(expected_ack_size)) {
        EquineRelay::LinkHeaderV1 ack_link{};
        SecureFrameHeader ack_header{};
        if (!EquineRelay::parseLinkedFrame(
              rx_buffer, rx_result, ack_link, ack_header)) {
          last_ack_status = 0;
          logPrintln("Ignored malformed relay link envelope.");
          serviceDisplayAndButton(true);
          continue;
        }
        const bool supported_ack = EquineProtocol::isSupportedFrame(
          ack_header.frame,
          EquineProtocol::MessageType::ACK,
          EquineProtocol::ACK_SCHEMA_VERSION
        );

        if (!supported_ack) {
          last_ack_status = 0;
          logPrintf(
            "Ignored unsupported ACK: magic=0x%04X transport=%u type=%u schema=%u.\n",
            ack_header.frame.magic,
            ack_header.frame.transport_version,
            ack_header.frame.message_type,
            ack_header.frame.schema_version
          );
        } else if (ack_header.frame.device_id_hash != TRACKER_DEVICE_HASH) {
          last_ack_status = 0;
          char received_hash[17];
          EquineProtocol::formatDeviceHash(
            ack_header.frame.device_id_hash, received_hash, sizeof(received_hash)
          );
          logPrintf("Ignored ACK for device hash %s.\n", received_hash);
        } else if (ack_header.boot_id == boot_id) {
          AckPayload ack{};
          const uint8_t* ack_ciphertext =
            rx_buffer + sizeof(ack_link) + sizeof(ack_header);
          const uint8_t* ack_tag = ack_ciphertext + sizeof(ack);
          if (!EquineCrypto::decrypt(
                tracker_config.lora_aead_key,
                ack_header,
                ack_ciphertext,
                sizeof(ack),
                ack_tag,
                reinterpret_cast<uint8_t*>(&ack)) ||
              ack.acked_seq != ack_header.counter) {
            last_ack_status = 0;
            logPrintln("Rejected unauthenticated or inconsistent ACK.");
            serviceDisplayAndButton(true);
            continue;
          }
          last_acked_seq = ack.acked_seq;
          uint16_t points_to_clear = 0;
          const uint16_t original_count = history_count;

          for (uint16_t i = 0; i < original_count; i++) {
            const StoredHistoryPoint& point =
              history_points[historyIndexToRing(points_to_clear)];
            if (point.seq <= ack.acked_seq) {
              points_to_clear++;
            } else {
              break;
            }
          }

          if (points_to_clear > 0) {
            history_count -= points_to_clear;
            ack_received = true;
            last_ack_status = 1;
            recordLoRaAckSuccess();
            setLastError(nullptr);
            logPrintf("ACK cleared %u points; %u remain.\n",
                      points_to_clear, history_count);
          } else {
            last_ack_status = 0;
            logPrintf("Ignored stale ACK for seq %lu.\n",
                      (unsigned long)ack.acked_seq);
          }
        } else {
          last_ack_status = 0;
          logPrintf("Ignored ACK for wrong boot_id: %lu\n",
                    (unsigned long)ack_header.boot_id);
        }
      } else if (rx_result > 0) {
        last_ack_status = 0;
        logPrintf("Ignored packet of wrong size: %d bytes\n", rx_result);
      } else {
        last_ack_status = 0;
      }

      serviceDisplayAndButton(true);

      if (!ack_received) {
        setLastError("LoRa ACK not received");
        logPrintln("No valid ACK; retaining queued data for retry.");
        recordLoRaFailure("missing/invalid ACK");
        break;
      }

      if (history_count == 0) {
        logPrintln("All queued points acknowledged.");
        break;
      }

      responsiveDelay(50);
    }

    sleepLoRaRadio();
  } else if (history_count > 0) {
    if (lora_batch_ready && !lora_retry_ready) {
      setTrackerPhase("LoRa backoff");
      const uint32_t retry_remaining_s =
        lora_retry_backoff_s - seconds_since_last_lora_attempt;
      logPrintf("Deferring LoRa retry: %u points queued, failure #%u, "
                "%u s remaining.\n",
                history_count, consecutive_lora_failures,
                retry_remaining_s);
    } else {
      setTrackerPhase("LoRa batching");
      logPrintf("Deferring LoRa: %u points queued, %u/%u s elapsed.\n",
                history_count, seconds_since_last_tx, LORA_TX_INTERVAL_S);
    }
  } else {
    setTrackerPhase("No queued points");
    logPrintln("No valid history points available.");
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
    stopBleDebug();
    prefs.end();
    Serial.flush();
    sleepGPS();
    sleepLoRaRadio();

    const uint32_t time_awake_s =
      (millis() - cycle_start_ms) / 1000U;

    const uint32_t elapsed_to_next_wake_s = time_awake_s + next_sleep_s;
    if (cycle_fix_found) {
      const uint32_t post_fix_awake_s =
        last_fix_acquired_ms > 0 ?
        (millis() - last_fix_acquired_ms) / 1000U : 0;
      seconds_since_last_fix = post_fix_awake_s + next_sleep_s;
    } else {
      seconds_since_last_fix = current_elapsed_s + next_sleep_s;
    }

    saturatingAddSeconds(seconds_since_daily_reset, elapsed_to_next_wake_s);
    saturatingAddSeconds(seconds_since_last_tx, elapsed_to_next_wake_s);
    saturatingAddSeconds(seconds_since_last_lora_attempt, elapsed_to_next_wake_s);
    if (cycle_position_accepted) {
      const uint32_t post_fix_awake_s =
        last_fix_acquired_ms > 0 ?
        (millis() - last_fix_acquired_ms) / 1000U : 0;
      seconds_since_last_accepted_position = post_fix_awake_s + next_sleep_s;
    } else {
      saturatingAddSeconds(seconds_since_last_accepted_position,
                           elapsed_to_next_wake_s);
    }
    saturatingAddSeconds(seconds_since_last_nvs_save, elapsed_to_next_wake_s);
    saturatingAddSeconds(seconds_since_last_full_gnss_attempt,
                         elapsed_to_next_wake_s);

    struct timeval tv;
    gettimeofday(&tv, NULL);
    const uint64_t now_us =
      (uint64_t)tv.tv_sec * 1000000ULL + tv.tv_usec;
    target_wakeup_time_us = now_us + next_sleep_duration_us;

    sealRtcHistory();
    esp_sleep_enable_ext0_wakeup((gpio_num_t)USER_BTN_PIN, 0);
    esp_sleep_enable_timer_wakeup(next_sleep_duration_us);
    esp_deep_sleep_start();
  } else {
    setTrackerPhase("Debug idle");
    serviceDisplayAndButton(true);
    logPrintln("Tracking cycle completed; BLE client remains connected.");

    const uint32_t time_awake_s =
      (millis() - cycle_start_ms) / 1000U;
    const uint32_t debug_pause_s =
      (uint32_t)((SLEEP_DURATION_US / 1000ULL) * 2ULL / 3ULL / 1000ULL);

    const uint32_t debug_elapsed_s = time_awake_s + debug_pause_s;
    if (cycle_fix_found) {
      const uint32_t post_fix_awake_s =
        last_fix_acquired_ms > 0 ?
        (millis() - last_fix_acquired_ms) / 1000U : 0;
      seconds_since_last_fix = post_fix_awake_s + debug_pause_s;
    } else {
      seconds_since_last_fix = current_elapsed_s + debug_pause_s;
    }
    saturatingAddSeconds(seconds_since_daily_reset, debug_elapsed_s);
    saturatingAddSeconds(seconds_since_last_tx, debug_elapsed_s);
    saturatingAddSeconds(seconds_since_last_lora_attempt, debug_elapsed_s);
    if (cycle_position_accepted) {
      const uint32_t post_fix_awake_s =
        last_fix_acquired_ms > 0 ?
        (millis() - last_fix_acquired_ms) / 1000U : 0;
      seconds_since_last_accepted_position = post_fix_awake_s + debug_pause_s;
    } else {
      saturatingAddSeconds(seconds_since_last_accepted_position,
                           debug_elapsed_s);
    }
    saturatingAddSeconds(seconds_since_last_nvs_save, debug_elapsed_s);
    saturatingAddSeconds(seconds_since_last_full_gnss_attempt,
                         debug_elapsed_s);
  }
}

void loop() {
  static uint32_t last_cycle_ms = 0;
  static bool first_debug_cycle = true;
  const uint32_t debug_pause_ms =
    (uint32_t)((SLEEP_DURATION_US / 1000ULL) * 2ULL / 3ULL);

  serviceDisplayAndButton();

  if (config_factory_reset_requested) {
    stopBleDebug();
    prefs.clear();
    clearTrackerConfigStorage();
    delay(250);
    ESP.restart();
  }
  if (config_reboot_requested) {
    stopBleDebug();
    delay(250);
    ESP.restart();
  }

  // Execute BLE lifecycle transitions only from the main loop, never from the
  // button callback path. Disabling immediately returns to one normal tracking
  // cycle and then deep sleep. Enabling performs a short deep-sleep reset so
  // BLE is initialized from a clean stack on the next wake.
  if (ble_disable_transition_requested) {
    ble_disable_transition_requested = false;
    stopBleDebug();
    logPrintln("BLE disabled; resuming normal low-power tracking now.");
    performTrackingCycle();
    return;
  }

  if (ble_enable_transition_requested) {
    ble_enable_transition_requested = false;
    restartThroughDeepSleepForBle();
  }

  if (isDebugModeActive()) {
    if ((uint32_t)(millis() - last_cycle_ms) >= debug_pause_ms ||
        last_cycle_ms == 0) {
      if (!first_debug_cycle) {
        logPrintln("\n--- Starting BLE debug tracking cycle ---");
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
  } else if (isBleConnectionWindowActive()) {
    // Allow a recently disconnected client to reconnect briefly.
    serviceBleDebug();
  } else if (ble_initialized) {
    // A debug session ended. Shut BLE down and run one normal cycle, which
    // will select the appropriate adaptive deep-sleep interval.
    stopBleDebug();
    logPrintln("BLE debug session ended; returning to low-power tracking.");
    performTrackingCycle();
  }

  delay(10);
}
