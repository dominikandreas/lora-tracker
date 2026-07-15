#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#include "equine_protocol.h"

namespace EquineConfig {

constexpr uint32_t CONFIG_MAGIC = 0x45434647UL;  // "ECFG"
constexpr uint16_t CONFIG_SCHEMA_VERSION = 1;
constexpr uint8_t MAX_GATEWAY_TRACKERS = 12;
constexpr size_t DEVICE_ID_SIZE = 25;       // 24 chars + NUL
constexpr size_t DEVICE_NAME_SIZE = 33;     // 32 chars + NUL
constexpr size_t WIFI_SSID_SIZE = 33;
constexpr size_t WIFI_PASSWORD_SIZE = 65;
constexpr size_t MQTT_HOST_SIZE = 65;
constexpr size_t MQTT_USERNAME_SIZE = 33;
constexpr size_t MQTT_PASSWORD_SIZE = 65;
constexpr size_t MQTT_BASE_TOPIC_SIZE = 33;

constexpr const char* CONFIG_NAMESPACE = "eqcfg";
constexpr const char* ACTIVE_CONFIG_KEY = "active";
constexpr const char* BACKUP_CONFIG_KEY = "backup";

enum class DeviceRole : uint8_t {
  TRACKER = 1,
  GATEWAY = 2,
};

struct ConfigHeaderV1 {
  uint32_t magic;
  uint16_t schema_version;
  uint16_t struct_size;
  uint32_t revision;
  uint8_t role;
  uint8_t reserved[3];
  uint32_t crc32;
} __attribute__((packed));

struct LoRaConfigV1 {
  uint32_t frequency_hz;
  uint32_t bandwidth_hz;
  int8_t tx_power_dbm;
  uint8_t spreading_factor;
  uint8_t coding_rate_denominator;
  uint8_t preamble_length;
  uint8_t sync_word;
  uint8_t reserved[3];
} __attribute__((packed));

struct TrackerConfigV1 {
  ConfigHeaderV1 header;

  char device_id[DEVICE_ID_SIZE];
  char device_name[DEVICE_NAME_SIZE];
  char wifi_ssid[WIFI_SSID_SIZE];
  char wifi_password[WIFI_PASSWORD_SIZE];

  uint8_t ble_debug_enabled;
  uint8_t battery_sense_enabled;
  uint8_t reserved_flags[2];

  LoRaConfigV1 lora;
  uint32_t lora_tx_interval_s;
  uint16_t lora_tx_min_points;
  uint16_t lora_ack_timeout_ms;
  uint32_t lora_retry_backoff_s[4];

  float min_distance_m;
  float min_speed_kmph;
  float max_hdop;
  uint16_t min_satellites;
  uint16_t reserved_gps0;
  float max_speed_mps;
  uint32_t max_fix_age_s;

  uint32_t gps_timeout_ms[4];
  uint32_t gps_full_retry_interval_s;
  uint16_t gps_initial_listen_ms;
  uint16_t gps_light_sleep_chunk_ms;
  uint16_t gps_listen_window_ms;
  uint16_t reserved_gps1;

  float movement_speed_threshold_kmph;
  float movement_displacement_threshold_m;
  float movement_evidence_distance_m;
  float movement_evidence_step_m;
  float movement_direction_tolerance_deg;
  uint8_t movement_evidence_required;
  uint8_t reserved_movement[3];

  float history_point_spacing_m;
  float save_distance_threshold_m;
  uint32_t nvs_save_interval_s;

  uint32_t moving_sleep_s;
  uint32_t stationary_sleep_s;
  uint32_t long_stationary_sleep_s;
  uint32_t no_fix_sleep_s[4];
  uint16_t stationary_fixes_for_long_sleep;
  uint16_t stationary_fixes_for_max_sleep;
} __attribute__((packed));

struct GatewayTrackerConfigV1 {
  char device_id[DEVICE_ID_SIZE];
  char device_name[DEVICE_NAME_SIZE];
  uint8_t enabled;
  uint8_t accepts_legacy_lora;
  uint8_t publish_legacy_mqtt_aliases;
  uint8_t reserved;
} __attribute__((packed));

struct GatewayConfigV1 {
  ConfigHeaderV1 header;

  char gateway_id[DEVICE_ID_SIZE];
  char gateway_name[DEVICE_NAME_SIZE];
  char wifi_ssid[WIFI_SSID_SIZE];
  char wifi_password[WIFI_PASSWORD_SIZE];
  char mqtt_host[MQTT_HOST_SIZE];
  uint16_t mqtt_port;
  uint8_t mqtt_tls_enabled;
  uint8_t reserved_network;
  char mqtt_username[MQTT_USERNAME_SIZE];
  char mqtt_password[MQTT_PASSWORD_SIZE];
  char mqtt_base_topic[MQTT_BASE_TOPIC_SIZE];

  LoRaConfigV1 lora;
  uint16_t mqtt_buffer_size;
  uint16_t dedup_save_interval;
  uint32_t wifi_retry_interval_ms;
  uint32_t mqtt_retry_interval_ms;

  uint8_t tracker_count;
  uint8_t reserved_registry[3];
  GatewayTrackerConfigV1 trackers[MAX_GATEWAY_TRACKERS];
} __attribute__((packed));

static_assert(sizeof(TrackerConfigV1) < 1024,
              "Tracker config should remain a compact NVS blob");
static_assert(sizeof(GatewayConfigV1) < 1600,
              "Gateway config should remain below the practical NVS blob limit");

inline uint32_t crc32(const uint8_t* data, size_t length) {
  uint32_t crc = 0xFFFFFFFFUL;
  for (size_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      const uint32_t mask = -(crc & 1UL);
      crc = (crc >> 1) ^ (0xEDB88320UL & mask);
    }
  }
  return ~crc;
}

inline bool isNullTerminated(const char* value, size_t capacity) {
  return value && memchr(value, '\0', capacity) != nullptr;
}

inline bool isValidCanonicalId(const char* id, size_t capacity) {
  if (!isNullTerminated(id, capacity)) return false;
  const size_t length = strnlen(id, capacity);
  if (length == 0 || length >= capacity) return false;
  for (size_t i = 0; i < length; i++) {
    const char c = id[i];
    const bool valid =
      (c >= 'a' && c <= 'z') ||
      (c >= '0' && c <= '9') ||
      (i > 0 && (c == '-' || c == '_'));
    if (!valid) return false;
  }
  return true;
}

inline bool isValidDisplayName(const char* name, size_t capacity) {
  if (!isNullTerminated(name, capacity)) return false;
  const size_t length = strnlen(name, capacity);
  if (length == 0 || length >= capacity) return false;
  for (size_t i = 0; i < length; i++) {
    const uint8_t c = static_cast<uint8_t>(name[i]);
    if (c < 0x20 || c == 0x7F) return false;
  }
  return true;
}

inline bool isFiniteRange(float value, float minimum, float maximum) {
  return isfinite(value) && value >= minimum && value <= maximum;
}

inline bool isSupportedBandwidth(uint32_t bandwidth_hz) {
  return bandwidth_hz == 62500UL || bandwidth_hz == 125000UL ||
         bandwidth_hz == 250000UL || bandwidth_hz == 500000UL;
}

inline bool validateLoRa(const LoRaConfigV1& config) {
  return config.frequency_hz >= 863000000UL &&
         config.frequency_hz <= 870000000UL &&
         isSupportedBandwidth(config.bandwidth_hz) &&
         config.tx_power_dbm >= 2 && config.tx_power_dbm <= 22 &&
         config.spreading_factor >= 7 && config.spreading_factor <= 12 &&
         config.coding_rate_denominator >= 5 &&
         config.coding_rate_denominator <= 8 &&
         config.preamble_length >= 6 && config.preamble_length <= 32;
}

template <typename T>
inline void finalize(T& config, DeviceRole role, uint32_t revision) {
  config.header.magic = CONFIG_MAGIC;
  config.header.schema_version = CONFIG_SCHEMA_VERSION;
  config.header.struct_size = sizeof(T);
  config.header.revision = revision == 0 ? 1 : revision;
  config.header.role = static_cast<uint8_t>(role);
  memset(config.header.reserved, 0, sizeof(config.header.reserved));
  config.header.crc32 = 0;
  config.header.crc32 = crc32(
    reinterpret_cast<const uint8_t*>(&config), sizeof(config));
}

template <typename T>
inline bool validateEnvelope(const T& config, DeviceRole expected_role) {
  if (config.header.magic != CONFIG_MAGIC ||
      config.header.schema_version != CONFIG_SCHEMA_VERSION ||
      config.header.struct_size != sizeof(T) ||
      config.header.role != static_cast<uint8_t>(expected_role) ||
      config.header.revision == 0) {
    return false;
  }

  T copy;
  memcpy(&copy, &config, sizeof(copy));
  const uint32_t expected_crc = copy.header.crc32;
  copy.header.crc32 = 0;
  return expected_crc == crc32(
    reinterpret_cast<const uint8_t*>(&copy), sizeof(copy));
}

inline bool validateTrackerConfig(const TrackerConfigV1& config) {
  if (!validateEnvelope(config, DeviceRole::TRACKER) ||
      !isValidCanonicalId(config.device_id, sizeof(config.device_id)) ||
      !isValidDisplayName(config.device_name, sizeof(config.device_name)) ||
      !isNullTerminated(config.wifi_ssid, sizeof(config.wifi_ssid)) ||
      !isNullTerminated(config.wifi_password, sizeof(config.wifi_password)) ||
      !validateLoRa(config.lora)) {
    return false;
  }

  if (config.lora_tx_interval_s < 10 || config.lora_tx_interval_s > 86400 ||
      config.lora_tx_min_points == 0 || config.lora_tx_min_points > 100 ||
      config.lora_ack_timeout_ms < 100 || config.lora_ack_timeout_ms > 10000) {
    return false;
  }
  for (uint8_t i = 0; i < 4; i++) {
    if (config.lora_retry_backoff_s[i] < 10 ||
        config.lora_retry_backoff_s[i] > 86400 ||
        config.gps_timeout_ms[i] < 1000 ||
        config.gps_timeout_ms[i] > 180000 ||
        config.no_fix_sleep_s[i] < 10 ||
        config.no_fix_sleep_s[i] > 86400) {
      return false;
    }
    if (i > 0 &&
        (config.lora_retry_backoff_s[i] < config.lora_retry_backoff_s[i - 1] ||
         config.gps_timeout_ms[i] > config.gps_timeout_ms[i - 1] ||
         config.no_fix_sleep_s[i] < config.no_fix_sleep_s[i - 1])) {
      return false;
    }
  }

  return isFiniteRange(config.min_distance_m, 0.5f, 100.0f) &&
         isFiniteRange(config.min_speed_kmph, 0.0f, 30.0f) &&
         isFiniteRange(config.max_hdop, 0.5f, 20.0f) &&
         config.min_satellites >= 3 && config.min_satellites <= 64 &&
         isFiniteRange(config.max_speed_mps, 1.0f, 100.0f) &&
         config.max_fix_age_s >= 60 && config.max_fix_age_s <= 604800 &&
         config.gps_full_retry_interval_s >= 60 &&
         config.gps_full_retry_interval_s <= 86400 &&
         config.gps_initial_listen_ms >= 250 &&
         config.gps_initial_listen_ms <= 30000 &&
         config.gps_light_sleep_chunk_ms >= 250 &&
         config.gps_light_sleep_chunk_ms <= 30000 &&
         config.gps_listen_window_ms >= 250 &&
         config.gps_listen_window_ms <= 30000 &&
         isFiniteRange(config.movement_speed_threshold_kmph, 0.1f, 30.0f) &&
         isFiniteRange(config.movement_displacement_threshold_m, 1.0f, 200.0f) &&
         isFiniteRange(config.movement_evidence_distance_m, 0.5f, 100.0f) &&
         isFiniteRange(config.movement_evidence_step_m, 0.5f, 100.0f) &&
         isFiniteRange(config.movement_direction_tolerance_deg, 1.0f, 180.0f) &&
         config.movement_evidence_required >= 1 &&
         config.movement_evidence_required <= 10 &&
         isFiniteRange(config.history_point_spacing_m, 1.0f, 1000.0f) &&
         isFiniteRange(config.save_distance_threshold_m, 10.0f, 10000.0f) &&
         config.nvs_save_interval_s >= 60 && config.nvs_save_interval_s <= 86400 &&
         config.moving_sleep_s >= 10 && config.moving_sleep_s <= 86400 &&
         config.stationary_sleep_s >= config.moving_sleep_s &&
         config.stationary_sleep_s <= 86400 &&
         config.long_stationary_sleep_s >= config.stationary_sleep_s &&
         config.long_stationary_sleep_s <= 86400 &&
         config.stationary_fixes_for_long_sleep >= 1 &&
         config.stationary_fixes_for_long_sleep <= 1000 &&
         config.stationary_fixes_for_max_sleep >=
           config.stationary_fixes_for_long_sleep &&
         config.stationary_fixes_for_max_sleep <= 1000;
}

inline bool validateGatewayConfig(const GatewayConfigV1& config) {
  if (!validateEnvelope(config, DeviceRole::GATEWAY) ||
      !isValidCanonicalId(config.gateway_id, sizeof(config.gateway_id)) ||
      !isValidDisplayName(config.gateway_name, sizeof(config.gateway_name)) ||
      !isNullTerminated(config.wifi_ssid, sizeof(config.wifi_ssid)) ||
      !isNullTerminated(config.wifi_password, sizeof(config.wifi_password)) ||
      !isNullTerminated(config.mqtt_host, sizeof(config.mqtt_host)) ||
      strnlen(config.mqtt_host, sizeof(config.mqtt_host)) == 0 ||
      !isNullTerminated(config.mqtt_username, sizeof(config.mqtt_username)) ||
      !isNullTerminated(config.mqtt_password, sizeof(config.mqtt_password)) ||
      !isValidCanonicalId(config.mqtt_base_topic, sizeof(config.mqtt_base_topic)) ||
      !validateLoRa(config.lora) ||
      config.mqtt_port == 0 ||
      config.mqtt_tls_enabled != 0 ||
      config.mqtt_buffer_size < 512 || config.mqtt_buffer_size > 4096 ||
      config.dedup_save_interval == 0 || config.dedup_save_interval > 1000 ||
      config.wifi_retry_interval_ms < 1000 ||
      config.mqtt_retry_interval_ms < 1000 ||
      config.tracker_count > MAX_GATEWAY_TRACKERS) {
    return false;
  }

  uint8_t legacy_count = 0;
  for (uint8_t i = 0; i < config.tracker_count; i++) {
    const GatewayTrackerConfigV1& tracker = config.trackers[i];
    if (!tracker.enabled) continue;
    if (!isValidCanonicalId(tracker.device_id, sizeof(tracker.device_id)) ||
        !isValidDisplayName(tracker.device_name, sizeof(tracker.device_name))) {
      return false;
    }
    if (tracker.accepts_legacy_lora) legacy_count++;
    const uint64_t hash = EquineProtocol::deviceIdHash(tracker.device_id);
    for (uint8_t previous = 0; previous < i; previous++) {
      if (!config.trackers[previous].enabled) continue;
      if (hash == EquineProtocol::deviceIdHash(
                    config.trackers[previous].device_id)) {
        return false;
      }
    }
  }
  return legacy_count <= 1;
}

inline void setDefaultLoRa(LoRaConfigV1& config) {
  memset(&config, 0, sizeof(config));
  config.frequency_hz = 868000000UL;
  config.bandwidth_hz = 125000UL;
  config.tx_power_dbm = 20;
  config.spreading_factor = 10;
  config.coding_rate_denominator = 5;
  config.preamble_length = 8;
  config.sync_word = 0x12;
}

inline void makeDefaultTrackerConfig(
    TrackerConfigV1& config,
    const char* device_id,
    const char* device_name,
    const char* default_wifi_ssid,
    const char* default_wifi_password) {
  memset(&config, 0, sizeof(config));
  strlcpy(config.device_id, device_id, sizeof(config.device_id));
  strlcpy(config.device_name, device_name, sizeof(config.device_name));
  strlcpy(config.wifi_ssid, default_wifi_ssid ? default_wifi_ssid : "",
          sizeof(config.wifi_ssid));
  strlcpy(config.wifi_password,
          default_wifi_password ? default_wifi_password : "",
          sizeof(config.wifi_password));
  config.ble_debug_enabled = 0;
  config.battery_sense_enabled = 1;
  setDefaultLoRa(config.lora);
  config.lora_tx_interval_s = 300;
  config.lora_tx_min_points = 3;
  config.lora_ack_timeout_ms = 800;
  config.lora_retry_backoff_s[0] = 60;
  config.lora_retry_backoff_s[1] = 120;
  config.lora_retry_backoff_s[2] = 300;
  config.lora_retry_backoff_s[3] = 600;
  config.min_distance_m = 2.5f;
  config.min_speed_kmph = 0.5f;
  config.max_hdop = 2.0f;
  config.min_satellites = 6;
  config.max_speed_mps = 20.0f;
  config.max_fix_age_s = 43200;
  config.gps_timeout_ms[0] = 30000;
  config.gps_timeout_ms[1] = 20000;
  config.gps_timeout_ms[2] = 10000;
  config.gps_timeout_ms[3] = 8000;
  config.gps_full_retry_interval_s = 3600;
  config.gps_initial_listen_ms = 5000;
  config.gps_light_sleep_chunk_ms = 5000;
  config.gps_listen_window_ms = 2000;
  config.movement_speed_threshold_kmph = 1.0f;
  config.movement_displacement_threshold_m = 10.0f;
  config.movement_evidence_distance_m = 4.5f;
  config.movement_evidence_step_m = 2.0f;
  config.movement_direction_tolerance_deg = 60.0f;
  config.movement_evidence_required = 2;
  config.history_point_spacing_m = 15.0f;
  config.save_distance_threshold_m = 250.0f;
  config.nvs_save_interval_s = 3600;
  config.moving_sleep_s = 60;
  config.stationary_sleep_s = 300;
  config.long_stationary_sleep_s = 600;
  config.no_fix_sleep_s[0] = 120;
  config.no_fix_sleep_s[1] = 300;
  config.no_fix_sleep_s[2] = 600;
  config.no_fix_sleep_s[3] = 900;
  config.stationary_fixes_for_long_sleep = 3;
  config.stationary_fixes_for_max_sleep = 12;
  finalize(config, DeviceRole::TRACKER, 1);
}

inline void makeDefaultGatewayConfig(
    GatewayConfigV1& config,
    const char* gateway_id,
    const char* gateway_name,
    const char* default_wifi_ssid,
    const char* default_wifi_password,
    const char* default_mqtt_host,
    uint16_t default_mqtt_port,
    const char* default_mqtt_username,
    const char* default_mqtt_password) {
  memset(&config, 0, sizeof(config));
  strlcpy(config.gateway_id, gateway_id, sizeof(config.gateway_id));
  strlcpy(config.gateway_name, gateway_name, sizeof(config.gateway_name));
  strlcpy(config.wifi_ssid, default_wifi_ssid ? default_wifi_ssid : "",
          sizeof(config.wifi_ssid));
  strlcpy(config.wifi_password,
          default_wifi_password ? default_wifi_password : "",
          sizeof(config.wifi_password));
  strlcpy(config.mqtt_host, default_mqtt_host ? default_mqtt_host : "",
          sizeof(config.mqtt_host));
  config.mqtt_port = default_mqtt_port ? default_mqtt_port : 1883;
  config.mqtt_tls_enabled = 0;
  strlcpy(config.mqtt_username,
          default_mqtt_username ? default_mqtt_username : "",
          sizeof(config.mqtt_username));
  strlcpy(config.mqtt_password,
          default_mqtt_password ? default_mqtt_password : "",
          sizeof(config.mqtt_password));
  strlcpy(config.mqtt_base_topic, "equine", sizeof(config.mqtt_base_topic));
  setDefaultLoRa(config.lora);
  config.mqtt_buffer_size = 1024;
  config.dedup_save_interval = 10;
  config.wifi_retry_interval_ms = 10000;
  config.mqtt_retry_interval_ms = 5000;
  finalize(config, DeviceRole::GATEWAY, 1);
}

}  // namespace EquineConfig
