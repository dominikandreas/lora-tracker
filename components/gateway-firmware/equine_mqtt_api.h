#pragma once

#include <Arduino.h>
#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace EquineMqttApi {

constexpr uint8_t API_VERSION = 1;
constexpr uint8_t POINT_SCHEMA_VERSION = 2;
constexpr uint8_t COMMAND_SCHEMA_VERSION = 1;
constexpr uint8_t HISTORY_SCHEMA_VERSION = 2;
constexpr size_t MAX_REQUEST_ID_LENGTH = 48;
constexpr size_t MAX_COMMAND_LENGTH = 40;
constexpr size_t MAX_COMMAND_PAYLOAD = 512;

inline void formatTrackerTopic(char* output, size_t output_size,
                               const char* base_topic, const char* device_hash,
                               const char* suffix) {
  if (!output || output_size == 0) return;
  snprintf(output, output_size, "%s/v%u/trackers/%s/%s",
           base_topic ? base_topic : "lora-tracker", API_VERSION,
           device_hash ? device_hash : "unknown",
           suffix ? suffix : "");
}

inline void formatGatewayTopic(char* output, size_t output_size,
                               const char* base_topic, const char* gateway_hash,
                               const char* suffix) {
  if (!output || output_size == 0) return;
  snprintf(output, output_size, "%s/v%u/gateways/%s/%s",
           base_topic ? base_topic : "lora-tracker", API_VERSION,
           gateway_hash ? gateway_hash : "unknown",
           suffix ? suffix : "");
}

inline bool isSafeRequestId(const char* value) {
  if (!value) return false;
  const size_t length = strlen(value);
  if (length == 0 || length > MAX_REQUEST_ID_LENGTH) return false;
  for (size_t i = 0; i < length; i++) {
    const char c = value[i];
    if (!(isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' ||
          c == '.')) {
      return false;
    }
  }
  return true;
}

inline const char* skipWhitespace(const char* cursor) {
  while (cursor && *cursor && isspace(static_cast<unsigned char>(*cursor))) {
    cursor++;
  }
  return cursor;
}

inline const char* findJsonValue(const char* json, const char* key) {
  if (!json || !key || !key[0]) return nullptr;
  char needle[80];
  const int written = snprintf(needle, sizeof(needle), "\"%s\"", key);
  if (written <= 0 || written >= static_cast<int>(sizeof(needle))) return nullptr;

  const char* cursor = json;
  while ((cursor = strstr(cursor, needle)) != nullptr) {
    cursor += strlen(needle);
    cursor = skipWhitespace(cursor);
    if (*cursor != ':') continue;
    cursor++;
    return skipWhitespace(cursor);
  }
  return nullptr;
}

inline bool jsonGetString(const char* json, const char* key,
                          char* output, size_t output_size) {
  if (!output || output_size == 0) return false;
  output[0] = '\0';
  const char* cursor = findJsonValue(json, key);
  if (!cursor || *cursor != '"') return false;
  cursor++;

  size_t used = 0;
  while (*cursor && *cursor != '"') {
    char value = *cursor++;
    if (value == '\\') {
      const char escaped = *cursor++;
      switch (escaped) {
        case '"': value = '"'; break;
        case '\\': value = '\\'; break;
        case '/': value = '/'; break;
        case 'n': value = '\n'; break;
        case 'r': value = '\r'; break;
        case 't': value = '\t'; break;
        default: return false;
      }
    }
    if (used + 1 >= output_size) return false;
    output[used++] = value;
  }
  if (*cursor != '"') return false;
  output[used] = '\0';
  return true;
}

inline bool jsonGetUnsigned(const char* json, const char* key,
                            uint32_t& output) {
  const char* cursor = findJsonValue(json, key);
  if (!cursor || !isdigit(static_cast<unsigned char>(*cursor))) return false;
  char* end = nullptr;
  const unsigned long parsed = strtoul(cursor, &end, 10);
  if (end == cursor || parsed > 0xFFFFFFFFUL) return false;
  output = static_cast<uint32_t>(parsed);
  return true;
}

inline bool jsonGetBool(const char* json, const char* key, bool& output) {
  const char* cursor = findJsonValue(json, key);
  if (!cursor) return false;
  if (strncmp(cursor, "true", 4) == 0) {
    output = true;
    return true;
  }
  if (strncmp(cursor, "false", 5) == 0) {
    output = false;
    return true;
  }
  return false;
}

inline void appendJsonEscaped(char* output, size_t output_size,
                              size_t& used, const char* value) {
  if (!output || output_size == 0 || used >= output_size) return;
  const char* cursor = value ? value : "";
  while (*cursor && used + 1 < output_size) {
    const char c = *cursor++;
    const char* escape = nullptr;
    switch (c) {
      case '"': escape = "\\\""; break;
      case '\\': escape = "\\\\"; break;
      case '\n': escape = "\\n"; break;
      case '\r': escape = "\\r"; break;
      case '\t': escape = "\\t"; break;
      default: break;
    }
    if (escape) {
      while (*escape && used + 1 < output_size) output[used++] = *escape++;
    } else if (static_cast<unsigned char>(c) >= 0x20) {
      output[used++] = c;
    }
  }
  output[used] = '\0';
}

}  // namespace EquineMqttApi
