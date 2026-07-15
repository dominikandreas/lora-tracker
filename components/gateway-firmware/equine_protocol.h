#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

// Shared on-wire definitions for trackers, gateways, relays and apps.
//
// Wire encoding is packed little-endian, matching the ESP32 endpoints.
// Applications on other platforms must decode integer fields explicitly as
// little-endian values rather than mapping these structs directly.
namespace EquineProtocol {

constexpr uint16_t FRAME_MAGIC = 0x5145;  // bytes "EQ" on little-endian targets
constexpr uint8_t TRANSPORT_VERSION = 1;
constexpr uint8_t HISTORY_SCHEMA_VERSION_V1 = 1;
constexpr uint8_t HISTORY_SCHEMA_VERSION = 2;
constexpr uint8_t ACK_SCHEMA_VERSION = 1;
constexpr uint8_t MQTT_API_VERSION = 1;
constexpr size_t MAX_ULEB128_U32_BYTES = 5;

// Flags are part of the stable envelope so encryption and relaying do not need
// another envelope layout. FLAG_HAS_TIMESTAMPS applies to history schema v2.
enum FrameFlags : uint8_t {
  FLAG_NONE           = 0,
  FLAG_ENCRYPTED      = 1U << 0,
  FLAG_RELAYED        = 1U << 1,
  FLAG_HAS_TIMESTAMPS = 1U << 2,
};

enum class MessageType : uint8_t {
  HISTORY = 1,
  ACK = 2,
  CONFIG = 3,
  RELAY = 4,
};

struct FrameHeaderV1 {
  uint16_t magic;
  uint8_t transport_version;
  uint8_t message_type;
  uint8_t schema_version;
  uint8_t flags;
  uint64_t device_id_hash;
} __attribute__((packed));

// Kept for rolling upgrades. Schema v1 has no point timestamps.
struct HistoryHeaderV1 {
  FrameHeaderV1 frame;
  uint32_t boot_id;
  uint32_t first_seq;
  uint16_t total_dist_dam;
  uint8_t batt_pct;
} __attribute__((packed));

// Schema v2 adds one absolute UTC timestamp for the root point. Every later
// point stores an unsigned LEB128 delta in seconds from the previous point.
// A normal 40-120 second interval costs one byte; up to 16383 seconds costs two.
struct HistoryHeaderV2 {
  FrameHeaderV1 frame;
  uint32_t boot_id;
  uint32_t first_seq;
  uint32_t root_unix_time_s;
  uint16_t total_dist_dam;
  uint8_t batt_pct;
} __attribute__((packed));

struct AckPayloadV1 {
  FrameHeaderV1 frame;
  uint32_t boot_id;
  uint32_t acked_seq;
} __attribute__((packed));

struct AnchorPointV1 {
  int16_t dlat;
  int16_t dlon;
} __attribute__((packed));

struct DeltaPointV1 {
  int8_t dlat;
  int8_t dlon;
} __attribute__((packed));

static_assert(sizeof(FrameHeaderV1) == 14, "Unexpected protocol frame size");
static_assert(sizeof(HistoryHeaderV1) == 25, "Unexpected v1 history header size");
static_assert(sizeof(HistoryHeaderV2) == 29, "Unexpected v2 history header size");
static_assert(sizeof(AckPayloadV1) == 22, "Unexpected ACK size");
static_assert(sizeof(AnchorPointV1) == 4, "Unexpected anchor size");
static_assert(sizeof(DeltaPointV1) == 2, "Unexpected delta size");

// Stable 64-bit routing identifier derived from the canonical device ID.
// FNV-1a is intentionally used only for compact identification/routing. It is
// NOT a password hash, authentication mechanism or encryption key.
inline uint64_t deviceIdHash(const char* device_id) {
  constexpr uint64_t OFFSET_BASIS = 14695981039346656037ULL;
  constexpr uint64_t PRIME = 1099511628211ULL;

  uint64_t hash = OFFSET_BASIS;
  if (!device_id) return hash;

  while (*device_id) {
    hash ^= static_cast<uint8_t>(*device_id++);
    hash *= PRIME;
  }
  return hash;
}

inline FrameHeaderV1 makeFrameHeader(
    MessageType type,
    uint8_t schema_version,
    uint64_t device_id_hash,
    uint8_t flags = FLAG_NONE) {
  FrameHeaderV1 header{};
  header.magic = FRAME_MAGIC;
  header.transport_version = TRANSPORT_VERSION;
  header.message_type = static_cast<uint8_t>(type);
  header.schema_version = schema_version;
  header.flags = flags;
  header.device_id_hash = device_id_hash;
  return header;
}

inline bool hasValidMagic(const FrameHeaderV1& header) {
  return header.magic == FRAME_MAGIC;
}

inline bool isSupportedFrame(
    const FrameHeaderV1& header,
    MessageType expected_type,
    uint8_t expected_schema_version) {
  return header.magic == FRAME_MAGIC &&
         header.transport_version == TRANSPORT_VERSION &&
         header.message_type == static_cast<uint8_t>(expected_type) &&
         header.schema_version == expected_schema_version;
}

inline bool isSupportedHistoryFrame(const FrameHeaderV1& header) {
  return header.magic == FRAME_MAGIC &&
         header.transport_version == TRANSPORT_VERSION &&
         header.message_type == static_cast<uint8_t>(MessageType::HISTORY) &&
         (header.schema_version == HISTORY_SCHEMA_VERSION_V1 ||
          header.schema_version == HISTORY_SCHEMA_VERSION);
}

inline size_t uleb128SizeU32(uint32_t value) {
  size_t size = 1;
  while (value >= 0x80U) {
    value >>= 7U;
    size++;
  }
  return size;
}

inline size_t encodeUleb128U32(uint32_t value, uint8_t* output, size_t output_size) {
  if (!output) return 0;
  const size_t required = uleb128SizeU32(value);
  if (required > output_size) return 0;

  size_t written = 0;
  do {
    uint8_t byte = static_cast<uint8_t>(value & 0x7FU);
    value >>= 7U;
    if (value != 0) byte |= 0x80U;
    output[written++] = byte;
  } while (value != 0);
  return written;
}

inline bool decodeUleb128U32(
    const uint8_t* input,
    size_t input_size,
    uint32_t& value,
    size_t& bytes_read) {
  value = 0;
  bytes_read = 0;
  if (!input) return false;

  uint32_t shift = 0;
  for (size_t index = 0;
       index < input_size && index < MAX_ULEB128_U32_BYTES;
       index++) {
    const uint8_t byte = input[index];
    const uint32_t payload = static_cast<uint32_t>(byte & 0x7FU);

    // On the fifth byte only four payload bits are legal for uint32_t.
    if (index == 4 && (payload & 0x70U) != 0) return false;
    value |= payload << shift;
    bytes_read++;
    if ((byte & 0x80U) == 0) return true;
    shift += 7U;
  }
  return false;
}

inline void formatDeviceHash(uint64_t hash, char* output, size_t output_size) {
  if (!output || output_size == 0) return;
  const uint32_t high = static_cast<uint32_t>(hash >> 32);
  const uint32_t low = static_cast<uint32_t>(hash & 0xFFFFFFFFULL);
  snprintf(output, output_size, "%08lx%08lx",
           static_cast<unsigned long>(high),
           static_cast<unsigned long>(low));
}

}  // namespace EquineProtocol
