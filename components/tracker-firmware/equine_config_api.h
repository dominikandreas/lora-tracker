#pragma once

#include <Arduino.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "equine_config.h"

namespace EquineConfigApi {

constexpr uint16_t API_VERSION = 1;
constexpr size_t ERROR_SIZE = 128;
constexpr const char* KEEP_SECRET = "__KEEP__";
constexpr const char* CLEAR_SECRET = "__CLEAR__";
constexpr const char* PROVISIONED_KEY = "provisioned";

struct PatchStatus {
  bool changed;
  bool reboot_required;
  char error[ERROR_SIZE];

  PatchStatus() : changed(false), reboot_required(false), error{} {}
};

enum class FieldResult : uint8_t {
  UNKNOWN = 0,
  APPLIED = 1,
  INVALID = 2,
};

inline void setError(PatchStatus& status, const char* field, const char* reason) {
  snprintf(status.error, sizeof(status.error), "%s: %s",
           field ? field : "field", reason ? reason : "invalid value");
}

inline bool parseUnsigned(const char* value, uint32_t minimum,
                          uint32_t maximum, uint32_t& output) {
  if (!value || !value[0] || value[0] == '-') return false;
  errno = 0;
  char* end = nullptr;
  const unsigned long parsed = strtoul(value, &end, 10);
  if (errno != 0 || end == value || *end != '\0' ||
      parsed < minimum || parsed > maximum) {
    return false;
  }
  output = static_cast<uint32_t>(parsed);
  return true;
}

inline bool parseSigned(const char* value, int32_t minimum,
                        int32_t maximum, int32_t& output) {
  if (!value || !value[0]) return false;
  errno = 0;
  char* end = nullptr;
  const long parsed = strtol(value, &end, 10);
  if (errno != 0 || end == value || *end != '\0' ||
      parsed < minimum || parsed > maximum) {
    return false;
  }
  output = static_cast<int32_t>(parsed);
  return true;
}

inline bool parseFloatValue(const char* value, float minimum,
                            float maximum, float& output) {
  if (!value || !value[0]) return false;
  errno = 0;
  char* end = nullptr;
  const float parsed = strtof(value, &end);
  if (errno != 0 || end == value || *end != '\0' ||
      !isfinite(parsed) || parsed < minimum || parsed > maximum) {
    return false;
  }
  output = parsed;
  return true;
}

inline bool parseBool(const char* value, uint8_t& output) {
  if (!value) return false;
  if (strcmp(value, "1") == 0 || strcasecmp(value, "true") == 0 ||
      strcasecmp(value, "yes") == 0 || strcasecmp(value, "on") == 0) {
    output = 1;
    return true;
  }
  if (strcmp(value, "0") == 0 || strcasecmp(value, "false") == 0 ||
      strcasecmp(value, "no") == 0 || strcasecmp(value, "off") == 0) {
    output = 0;
    return true;
  }
  return false;
}

template <size_t N>
inline bool copyText(char (&destination)[N], const char* value,
                     bool allow_empty = true) {
  if (!value) return false;
  const size_t length = strlen(value);
  if ((!allow_empty && length == 0) || length >= N) return false;
  strlcpy(destination, value, N);
  return true;
}

template <size_t N>
inline bool copySecret(char (&destination)[N], const char* value,
                       bool& changed) {
  if (!value || value[0] == '\0' || strcmp(value, KEEP_SECRET) == 0) {
    return true;
  }
  if (strcmp(value, CLEAR_SECRET) == 0) {
    if (destination[0] != '\0') changed = true;
    destination[0] = '\0';
    return true;
  }
  if (strlen(value) >= N) return false;
  if (strncmp(destination, value, N) != 0) changed = true;
  strlcpy(destination, value, N);
  return true;
}

inline bool assignU32(uint32_t& field, const char* value,
                      uint32_t minimum, uint32_t maximum,
                      PatchStatus& status, const char* name,
                      bool reboot_required = false) {
  uint32_t parsed = 0;
  if (!parseUnsigned(value, minimum, maximum, parsed)) {
    setError(status, name, "expected an integer in range");
    return false;
  }
  if (field != parsed) {
    field = parsed;
    status.changed = true;
    status.reboot_required |= reboot_required;
  }
  return true;
}

inline bool assignU16(uint16_t& field, const char* value,
                      uint32_t minimum, uint32_t maximum,
                      PatchStatus& status, const char* name,
                      bool reboot_required = false) {
  uint32_t parsed = 0;
  if (!parseUnsigned(value, minimum, maximum, parsed)) {
    setError(status, name, "expected an integer in range");
    return false;
  }
  if (field != parsed) {
    field = static_cast<uint16_t>(parsed);
    status.changed = true;
    status.reboot_required |= reboot_required;
  }
  return true;
}

inline bool assignU8(uint8_t& field, const char* value,
                     uint32_t minimum, uint32_t maximum,
                     PatchStatus& status, const char* name,
                     bool reboot_required = false) {
  uint32_t parsed = 0;
  if (!parseUnsigned(value, minimum, maximum, parsed)) {
    setError(status, name, "expected an integer in range");
    return false;
  }
  if (field != parsed) {
    field = static_cast<uint8_t>(parsed);
    status.changed = true;
    status.reboot_required |= reboot_required;
  }
  return true;
}

inline bool assignI8(int8_t& field, const char* value,
                     int32_t minimum, int32_t maximum,
                     PatchStatus& status, const char* name,
                     bool reboot_required = false) {
  int32_t parsed = 0;
  if (!parseSigned(value, minimum, maximum, parsed)) {
    setError(status, name, "expected an integer in range");
    return false;
  }
  if (field != parsed) {
    field = static_cast<int8_t>(parsed);
    status.changed = true;
    status.reboot_required |= reboot_required;
  }
  return true;
}

inline bool assignFloat(float& field, const char* value,
                        float minimum, float maximum,
                        PatchStatus& status, const char* name) {
  float parsed = 0.0f;
  if (!parseFloatValue(value, minimum, maximum, parsed)) {
    setError(status, name, "expected a finite number in range");
    return false;
  }
  if (fabsf(field - parsed) > 0.00001f) {
    field = parsed;
    status.changed = true;
  }
  return true;
}

inline bool assignBool(uint8_t& field, const char* value,
                       PatchStatus& status, const char* name,
                       bool reboot_required = false) {
  uint8_t parsed = 0;
  if (!parseBool(value, parsed)) {
    setError(status, name, "expected true/false or 1/0");
    return false;
  }
  if (field != parsed) {
    field = parsed;
    status.changed = true;
    status.reboot_required |= reboot_required;
  }
  return true;
}

template <size_t N>
inline bool assignText(char (&field)[N], const char* value,
                       PatchStatus& status, const char* name,
                       bool allow_empty, bool reboot_required) {
  if (!value || strlen(value) >= N || (!allow_empty && value[0] == '\0')) {
    setError(status, name, "text is empty or too long");
    return false;
  }
  if (strncmp(field, value, N) != 0) {
    strlcpy(field, value, N);
    status.changed = true;
    status.reboot_required |= reboot_required;
  }
  return true;
}

template <size_t N>
inline bool assignSecret(char (&field)[N], const char* value,
                         PatchStatus& status, const char* name,
                         bool reboot_required) {
  bool changed = false;
  if (!copySecret(field, value, changed)) {
    setError(status, name, "secret is too long");
    return false;
  }
  if (changed) {
    status.changed = true;
    status.reboot_required |= reboot_required;
  }
  return true;
}

inline FieldResult applyLoRaField(EquineConfig::LoRaConfigV1& lora,
                                  const char* key, const char* value,
                                  PatchStatus& status) {
#define EQ_LORA_U32(field, minv, maxv) do { \
  uint32_t parsed = 0; \
  if (!parseUnsigned(value, minv, maxv, parsed)) { setError(status, key, "expected an integer in range"); return FieldResult::INVALID; } \
  if (lora.field != parsed) { lora.field = parsed; status.changed = true; status.reboot_required = true; } \
  return FieldResult::APPLIED; \
} while (0)
#define EQ_LORA_U8(field, minv, maxv) do { \
  uint32_t parsed = 0; \
  if (!parseUnsigned(value, minv, maxv, parsed)) { setError(status, key, "expected an integer in range"); return FieldResult::INVALID; } \
  if (lora.field != parsed) { lora.field = static_cast<uint8_t>(parsed); status.changed = true; status.reboot_required = true; } \
  return FieldResult::APPLIED; \
} while (0)
  if (strcmp(key, "lora_frequency_hz") == 0) {
    EQ_LORA_U32(frequency_hz, 863000000UL, 870000000UL);
  }
  if (strcmp(key, "lora_bandwidth_hz") == 0) {
    uint32_t parsed = 0;
    if (!parseUnsigned(value, 62500, 500000, parsed) ||
        !EquineConfig::isSupportedBandwidth(parsed)) {
      setError(status, key, "supported values: 62500, 125000, 250000, 500000");
      return FieldResult::INVALID;
    }
    if (lora.bandwidth_hz != parsed) {
      lora.bandwidth_hz = parsed;
      status.changed = true;
      status.reboot_required = true;
    }
    return FieldResult::APPLIED;
  }
  if (strcmp(key, "lora_tx_power_dbm") == 0) {
    int32_t parsed = 0;
    if (!parseSigned(value, 2, 22, parsed)) {
      setError(status, key, "expected an integer in range");
      return FieldResult::INVALID;
    }
    if (lora.tx_power_dbm != parsed) {
      lora.tx_power_dbm = static_cast<int8_t>(parsed);
      status.changed = true;
      status.reboot_required = true;
    }
    return FieldResult::APPLIED;
  }
  if (strcmp(key, "lora_sf") == 0) EQ_LORA_U8(spreading_factor, 7, 12);
  if (strcmp(key, "lora_coding_rate") == 0) EQ_LORA_U8(coding_rate_denominator, 5, 8);
  if (strcmp(key, "lora_preamble_length") == 0) EQ_LORA_U8(preamble_length, 6, 32);
  if (strcmp(key, "lora_sync_word") == 0) EQ_LORA_U8(sync_word, 0, 255);
#undef EQ_LORA_U32
#undef EQ_LORA_U8
  return FieldResult::UNKNOWN;
}

inline FieldResult applyTrackerField(EquineConfig::TrackerConfigV1& config,
                                     const char* key, const char* value,
                                     PatchStatus& status) {
  if (!key || !value) return FieldResult::INVALID;
  FieldResult common = applyLoRaField(config.lora, key, value, status);
  if (common != FieldResult::UNKNOWN) return common;

#define EQ_SET_U32(name, member, minv, maxv) do { if (strcmp(key, name) == 0) { \
  uint32_t parsed=0; if(!parseUnsigned(value,minv,maxv,parsed)){setError(status,key,"expected an integer in range");return FieldResult::INVALID;} \
  if(config.member!=parsed){config.member=parsed;status.changed=true;} return FieldResult::APPLIED; }} while(0)
#define EQ_SET_U16(name, member, minv, maxv) do { if (strcmp(key, name) == 0) { \
  uint32_t parsed=0; if(!parseUnsigned(value,minv,maxv,parsed)){setError(status,key,"expected an integer in range");return FieldResult::INVALID;} \
  if(config.member!=parsed){config.member=static_cast<uint16_t>(parsed);status.changed=true;} return FieldResult::APPLIED; }} while(0)
#define EQ_SET_U8(name, member, minv, maxv) do { if (strcmp(key, name) == 0) { \
  uint32_t parsed=0; if(!parseUnsigned(value,minv,maxv,parsed)){setError(status,key,"expected an integer in range");return FieldResult::INVALID;} \
  if(config.member!=parsed){config.member=static_cast<uint8_t>(parsed);status.changed=true;} return FieldResult::APPLIED; }} while(0)
#define EQ_SET_FLOAT(name, member, minv, maxv) do { if (strcmp(key, name) == 0) { \
  float parsed=0; if(!parseFloatValue(value,minv,maxv,parsed)){setError(status,key,"expected a finite number in range");return FieldResult::INVALID;} \
  if(fabsf(config.member-parsed)>0.00001f){config.member=parsed;status.changed=true;} return FieldResult::APPLIED; }} while(0)

  if (strcmp(key, "device_id") == 0)
    return assignText(config.device_id, value, status, key, false, true) ? FieldResult::APPLIED : FieldResult::INVALID;
  if (strcmp(key, "device_name") == 0)
    return assignText(config.device_name, value, status, key, false, false) ? FieldResult::APPLIED : FieldResult::INVALID;
  if (strcmp(key, "wifi_ssid") == 0)
    return assignText(config.wifi_ssid, value, status, key, true, true) ? FieldResult::APPLIED : FieldResult::INVALID;
  if (strcmp(key, "wifi_password") == 0)
    return assignSecret(config.wifi_password, value, status, key, true) ? FieldResult::APPLIED : FieldResult::INVALID;
  if (strcmp(key, "ble_debug_enabled") == 0)
    return assignBool(config.ble_debug_enabled, value, status, key) ? FieldResult::APPLIED : FieldResult::INVALID;
  if (strcmp(key, "battery_sense_enabled") == 0)
    return assignBool(config.battery_sense_enabled, value, status, key, true) ? FieldResult::APPLIED : FieldResult::INVALID;

  EQ_SET_U32("lora_tx_interval_s", lora_tx_interval_s, 10, 86400);
  EQ_SET_U16("lora_tx_min_points", lora_tx_min_points, 1, 100);
  EQ_SET_U16("lora_ack_timeout_ms", lora_ack_timeout_ms, 100, 10000);
  for (uint8_t i=0;i<4;i++) { char expected[32]; snprintf(expected,sizeof(expected),"lora_retry_backoff_%u_s",i+1); if(strcmp(key,expected)==0){uint32_t parsed=0;if(!parseUnsigned(value,10,86400,parsed)){setError(status,key,"expected an integer in range");return FieldResult::INVALID;}if(config.lora_retry_backoff_s[i]!=parsed){config.lora_retry_backoff_s[i]=parsed;status.changed=true;}return FieldResult::APPLIED;}}
  EQ_SET_FLOAT("min_distance_m", min_distance_m, 0.5f, 100.0f);
  EQ_SET_FLOAT("min_speed_kmph", min_speed_kmph, 0.0f, 30.0f);
  EQ_SET_FLOAT("max_hdop", max_hdop, 0.5f, 20.0f);
  EQ_SET_U16("min_satellites", min_satellites, 3, 64);
  EQ_SET_FLOAT("max_speed_mps", max_speed_mps, 1.0f, 100.0f);
  EQ_SET_U32("max_fix_age_s", max_fix_age_s, 60, 604800);
  for (uint8_t i=0;i<4;i++) { char expected[24]; snprintf(expected,sizeof(expected),"gps_timeout_%u_ms",i+1); if(strcmp(key,expected)==0){uint32_t parsed=0;if(!parseUnsigned(value,1000,180000,parsed)){setError(status,key,"expected an integer in range");return FieldResult::INVALID;}if(config.gps_timeout_ms[i]!=parsed){config.gps_timeout_ms[i]=parsed;status.changed=true;}return FieldResult::APPLIED;}}
  EQ_SET_U32("gps_full_retry_interval_s", gps_full_retry_interval_s, 60, 86400);
  EQ_SET_U16("gps_initial_listen_ms", gps_initial_listen_ms, 250, 30000);
  EQ_SET_U16("gps_light_sleep_chunk_ms", gps_light_sleep_chunk_ms, 250, 30000);
  EQ_SET_U16("gps_listen_window_ms", gps_listen_window_ms, 250, 30000);
  EQ_SET_FLOAT("movement_speed_threshold_kmph", movement_speed_threshold_kmph, 0.1f, 30.0f);
  EQ_SET_FLOAT("movement_displacement_threshold_m", movement_displacement_threshold_m, 1.0f, 200.0f);
  EQ_SET_FLOAT("movement_evidence_distance_m", movement_evidence_distance_m, 0.5f, 100.0f);
  EQ_SET_FLOAT("movement_evidence_step_m", movement_evidence_step_m, 0.5f, 100.0f);
  EQ_SET_FLOAT("movement_direction_tolerance_deg", movement_direction_tolerance_deg, 1.0f, 180.0f);
  EQ_SET_U8("movement_evidence_required", movement_evidence_required, 1, 10);
  EQ_SET_FLOAT("history_point_spacing_m", history_point_spacing_m, 1.0f, 1000.0f);
  EQ_SET_FLOAT("save_distance_threshold_m", save_distance_threshold_m, 10.0f, 10000.0f);
  EQ_SET_U32("nvs_save_interval_s", nvs_save_interval_s, 60, 86400);
  EQ_SET_U32("moving_sleep_s", moving_sleep_s, 10, 86400);
  EQ_SET_U32("stationary_sleep_s", stationary_sleep_s, 10, 86400);
  EQ_SET_U32("long_stationary_sleep_s", long_stationary_sleep_s, 10, 86400);
  for (uint8_t i=0;i<4;i++) { char expected[24]; snprintf(expected,sizeof(expected),"no_fix_sleep_%u_s",i+1); if(strcmp(key,expected)==0){uint32_t parsed=0;if(!parseUnsigned(value,10,86400,parsed)){setError(status,key,"expected an integer in range");return FieldResult::INVALID;}if(config.no_fix_sleep_s[i]!=parsed){config.no_fix_sleep_s[i]=parsed;status.changed=true;}return FieldResult::APPLIED;}}
  EQ_SET_U16("stationary_fixes_for_long_sleep", stationary_fixes_for_long_sleep, 1, 1000);
  EQ_SET_U16("stationary_fixes_for_max_sleep", stationary_fixes_for_max_sleep, 1, 1000);
#undef EQ_SET_U32
#undef EQ_SET_U16
#undef EQ_SET_U8
#undef EQ_SET_FLOAT
  return FieldResult::UNKNOWN;
}

inline bool parseTrackerIndexKey(const char* key, uint8_t& index,
                                 const char*& subfield) {
  if (!key || strncmp(key, "tracker.", 8) != 0) return false;
  const char* number = key + 8;
  char* end = nullptr;
  const unsigned long parsed = strtoul(number, &end, 10);
  if (end == number || *end != '.' || parsed >= EquineConfig::MAX_GATEWAY_TRACKERS) {
    return false;
  }
  index = static_cast<uint8_t>(parsed);
  subfield = end + 1;
  return subfield[0] != '\0';
}

inline FieldResult applyGatewayField(EquineConfig::GatewayConfigV1& config,
                                     const char* key, const char* value,
                                     PatchStatus& status) {
  if (!key || !value) return FieldResult::INVALID;
  FieldResult common = applyLoRaField(config.lora, key, value, status);
  if (common != FieldResult::UNKNOWN) return common;
  if (strcmp(key,"gateway_id")==0) return assignText(config.gateway_id,value,status,key,false,true)?FieldResult::APPLIED:FieldResult::INVALID;
  if (strcmp(key,"gateway_name")==0) return assignText(config.gateway_name,value,status,key,false,false)?FieldResult::APPLIED:FieldResult::INVALID;
  if (strcmp(key,"wifi_ssid")==0) return assignText(config.wifi_ssid,value,status,key,true,true)?FieldResult::APPLIED:FieldResult::INVALID;
  if (strcmp(key,"wifi_password")==0) return assignSecret(config.wifi_password,value,status,key,true)?FieldResult::APPLIED:FieldResult::INVALID;
  if (strcmp(key,"mqtt_host")==0) return assignText(config.mqtt_host,value,status,key,false,true)?FieldResult::APPLIED:FieldResult::INVALID;
  if (strcmp(key,"mqtt_username")==0) return assignText(config.mqtt_username,value,status,key,true,true)?FieldResult::APPLIED:FieldResult::INVALID;
  if (strcmp(key,"mqtt_password")==0) return assignSecret(config.mqtt_password,value,status,key,true)?FieldResult::APPLIED:FieldResult::INVALID;
  if (strcmp(key,"mqtt_base_topic")==0) return assignText(config.mqtt_base_topic,value,status,key,false,true)?FieldResult::APPLIED:FieldResult::INVALID;
  if (strcmp(key,"mqtt_tls_enabled")==0) return assignBool(config.mqtt_tls_enabled,value,status,key,true)?FieldResult::APPLIED:FieldResult::INVALID;
#define EQ_GW_U32(name, member, minv, maxv, rebootv) do { if(strcmp(key,name)==0){uint32_t parsed=0;if(!parseUnsigned(value,minv,maxv,parsed)){setError(status,key,"expected an integer in range");return FieldResult::INVALID;}if(config.member!=parsed){config.member=parsed;status.changed=true;status.reboot_required|=rebootv;}return FieldResult::APPLIED;}}while(0)
#define EQ_GW_U16(name, member, minv, maxv, rebootv) do { if(strcmp(key,name)==0){uint32_t parsed=0;if(!parseUnsigned(value,minv,maxv,parsed)){setError(status,key,"expected an integer in range");return FieldResult::INVALID;}if(config.member!=parsed){config.member=static_cast<uint16_t>(parsed);status.changed=true;status.reboot_required|=rebootv;}return FieldResult::APPLIED;}}while(0)
#define EQ_GW_U8(name, member, minv, maxv, rebootv) do { if(strcmp(key,name)==0){uint32_t parsed=0;if(!parseUnsigned(value,minv,maxv,parsed)){setError(status,key,"expected an integer in range");return FieldResult::INVALID;}if(config.member!=parsed){config.member=static_cast<uint8_t>(parsed);status.changed=true;status.reboot_required|=rebootv;}return FieldResult::APPLIED;}}while(0)
  EQ_GW_U16("mqtt_port", mqtt_port, 1, 65535, true);
  EQ_GW_U16("mqtt_buffer_size", mqtt_buffer_size, 512, 4096, true);
  EQ_GW_U16("dedup_save_interval", dedup_save_interval, 1, 1000, false);
  EQ_GW_U32("wifi_retry_interval_ms", wifi_retry_interval_ms, 1000, 3600000, true);
  EQ_GW_U32("mqtt_retry_interval_ms", mqtt_retry_interval_ms, 1000, 3600000, true);
  EQ_GW_U8("tracker_count", tracker_count, 0, EquineConfig::MAX_GATEWAY_TRACKERS, true);
#undef EQ_GW_U32
#undef EQ_GW_U16
#undef EQ_GW_U8
  uint8_t index=0; const char* subfield=nullptr;
  if (parseTrackerIndexKey(key,index,subfield)) {
    EquineConfig::GatewayTrackerConfigV1& tracker=config.trackers[index];
    if(strcmp(subfield,"id")==0) return assignText(tracker.device_id,value,status,key,false,true)?FieldResult::APPLIED:FieldResult::INVALID;
    if(strcmp(subfield,"name")==0) return assignText(tracker.device_name,value,status,key,false,true)?FieldResult::APPLIED:FieldResult::INVALID;
    if(strcmp(subfield,"enabled")==0) return assignBool(tracker.enabled,value,status,key,true)?FieldResult::APPLIED:FieldResult::INVALID;
    if(strcmp(subfield,"accepts_legacy_lora")==0) return assignBool(tracker.accepts_legacy_lora,value,status,key,true)?FieldResult::APPLIED:FieldResult::INVALID;
    if(strcmp(subfield,"publish_legacy_mqtt_aliases")==0) return assignBool(tracker.publish_legacy_mqtt_aliases,value,status,key,true)?FieldResult::APPLIED:FieldResult::INVALID;
  }
  return FieldResult::UNKNOWN;
}

inline int fromHex(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

inline bool decodeUrlInPlace(char* value) {
  if (!value) return false;
  char* read = value;
  char* write = value;
  while (*read) {
    if (*read == '+') {
      *write++ = ' ';
      read++;
    } else if (*read == '%') {
      const int high = fromHex(read[1]);
      const int low = fromHex(read[2]);
      if (high < 0 || low < 0) return false;
      *write++ = static_cast<char>((high << 4) | low);
      read += 3;
    } else {
      *write++ = *read++;
    }
  }
  *write = '\0';
  return true;
}

template <typename Handler>
inline bool forEachUrlEncodedPair(char* input, Handler handler,
                                  char* error, size_t error_size) {
  if (!input) return false;
  char* cursor = input;
  while (*cursor) {
    char* pair = cursor;
    char* amp = strchr(cursor, '&');
    if (amp) {
      *amp = '\0';
      cursor = amp + 1;
    } else {
      cursor += strlen(cursor);
    }
    if (pair[0] == '\0') continue;
    char* equal = strchr(pair, '=');
    if (!equal) {
      snprintf(error, error_size, "missing '=' in field");
      return false;
    }
    *equal = '\0';
    char* key = pair;
    char* value = equal + 1;
    if (!decodeUrlInPlace(key) || !decodeUrlInPlace(value)) {
      snprintf(error, error_size, "invalid URL encoding");
      return false;
    }
    if (!handler(key, value)) return false;
  }
  return true;
}

inline void appendJsonEscaped(String& out, const char* value) {
  if (!value) return;
  for (const unsigned char* p =
         reinterpret_cast<const unsigned char*>(value); *p; ++p) {
    switch (*p) {
      case '\\': out += "\\\\"; break;
      case '"': out += "\\\""; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (*p < 0x20) {
          char escaped[7];
          snprintf(escaped, sizeof(escaped), "\\u%04x", *p);
          out += escaped;
        } else {
          out += static_cast<char>(*p);
        }
    }
  }
}

inline bool isControlField(const char* key) {
  return strcmp(key, "expected_revision") == 0 ||
         strcmp(key, "reboot") == 0 ||
         strcmp(key, "confirm") == 0;
}

}  // namespace EquineConfigApi
