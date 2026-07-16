#pragma once

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../firmware-core/include/lora_tracker_core.h"
#include "equine_protocol.h"

// Link-layer forwarding for end-to-end encrypted LoRa frames. The mutable
// header is deliberately outside the AES-GCM frame: repeaters can advance the
// hop count but cannot inspect or alter authenticated tracker data or ACKs.
namespace EquineRelay {

constexpr uint16_t LINK_MAGIC = 0x524c;  // wire bytes "LR"
constexpr uint8_t LINK_VERSION = 1;
constexpr uint8_t MAX_HOPS = 4;
constexpr size_t MAX_PACKET_SIZE = 255;
constexpr uint32_t ACK_DUPLICATE_CACHE_MS = 5000;

enum LinkFlags : uint8_t {
  LINK_FLAG_NONE = 0,
};

struct LinkHeaderV1 {
  uint16_t magic;
  uint8_t version;
  uint8_t hop_count;
  uint8_t hop_limit;
  uint8_t flags;
} __attribute__((packed));

struct FrameIdentityV1 {
  uint64_t device_id_hash;
  uint32_t boot_id;
  uint32_t counter;
  uint8_t message_type;
  uint8_t schema_version;
} __attribute__((packed));

static_assert(sizeof(LinkHeaderV1) == 6, "Unexpected relay link header size");
static_assert(sizeof(FrameIdentityV1) == 18, "Unexpected relay identity size");
static_assert(sizeof(LinkHeaderV1) + sizeof(EquineProtocol::SecureFrameHeaderV2) +
                EquineProtocol::AEAD_TAG_SIZE <= MAX_PACKET_SIZE,
              "Minimum linked secure frame exceeds LoRa packet limit");

inline LinkHeaderV1 makeOriginHeader(uint8_t hop_limit) {
  LinkHeaderV1 header{};
  header.magic = LINK_MAGIC;
  header.version = LINK_VERSION;
  header.hop_count = 0;
  header.hop_limit = hop_limit > MAX_HOPS ? MAX_HOPS : hop_limit;
  header.flags = LINK_FLAG_NONE;
  return header;
}

inline bool isSupportedLinkHeader(const LinkHeaderV1& header) {
  return header.magic == LINK_MAGIC &&
         header.version == LINK_VERSION &&
         header.flags == LINK_FLAG_NONE &&
         header.hop_limit <= MAX_HOPS &&
         header.hop_count <= header.hop_limit;
}

inline bool isForwardableSecureHeader(
    const EquineProtocol::SecureFrameHeaderV2& secure) {
  if (EquineProtocol::isSupportedHistoryFrame(secure.frame)) return true;
  return EquineProtocol::isSupportedFrame(
    secure.frame,
    EquineProtocol::MessageType::ACK,
    EquineProtocol::ACK_SCHEMA_VERSION);
}

inline bool parseLinkedFrame(
    const uint8_t* packet,
    size_t packet_size,
    LinkHeaderV1& link,
    EquineProtocol::SecureFrameHeaderV2& secure) {
  constexpr size_t MIN_SIZE =
    sizeof(LinkHeaderV1) + sizeof(EquineProtocol::SecureFrameHeaderV2) +
    EquineProtocol::AEAD_TAG_SIZE;
  if (!packet || packet_size < MIN_SIZE || packet_size > MAX_PACKET_SIZE) {
    return false;
  }
  memcpy(&link, packet, sizeof(link));
  memcpy(&secure, packet + sizeof(link), sizeof(secure));
  return isSupportedLinkHeader(link) && isForwardableSecureHeader(secure);
}

inline bool advanceHop(uint8_t* packet, size_t packet_size, uint8_t local_hop_cap) {
  LinkHeaderV1 link{};
  EquineProtocol::SecureFrameHeaderV2 secure{};
  if (!parseLinkedFrame(packet, packet_size, link, secure)) return false;
  const uint8_t effective_limit =
    link.hop_limit < local_hop_cap ? link.hop_limit : local_hop_cap;
  if (link.hop_count >= effective_limit || link.hop_count == UINT8_MAX) {
    return false;
  }
  link.hop_count++;
  memcpy(packet, &link, sizeof(link));
  return true;
}

inline FrameIdentityV1 frameIdentity(
    const EquineProtocol::SecureFrameHeaderV2& secure) {
  FrameIdentityV1 identity{};
  identity.device_id_hash = secure.frame.device_id_hash;
  identity.boot_id = secure.boot_id;
  identity.counter = secure.counter;
  identity.message_type = secure.frame.message_type;
  identity.schema_version = secure.frame.schema_version;
  return identity;
}

inline bool sameIdentity(
    const FrameIdentityV1& left,
    const FrameIdentityV1& right) {
  return left.device_id_hash == right.device_id_hash &&
         left.boot_id == right.boot_id &&
         left.counter == right.counter &&
         left.message_type == right.message_type &&
         left.schema_version == right.schema_version;
}

inline uint64_t mix64(uint64_t value) {
  value ^= value >> 30;
  value *= 0xbf58476d1ce4e5b9ULL;
  value ^= value >> 27;
  value *= 0x94d049bb133111ebULL;
  return value ^ (value >> 31);
}

inline uint64_t identityFingerprint(const FrameIdentityV1& identity) {
  uint64_t value = identity.device_id_hash;
  value ^= (static_cast<uint64_t>(identity.boot_id) << 32) | identity.counter;
  value ^= static_cast<uint64_t>(identity.message_type) << 56;
  value ^= static_cast<uint64_t>(identity.schema_version) << 48;
  return mix64(value);
}

inline uint32_t forwardingDelayMs(
    const FrameIdentityV1& identity,
    uint64_t repeater_id_hash,
    uint16_t base_delay_ms,
    uint8_t slot_count,
    uint16_t slot_width_ms) {
  return LoraTrackerCore::forwardingDelayMs(
    identity.device_id_hash,
    identity.boot_id,
    identity.counter,
    identity.message_type,
    identity.schema_version,
    repeater_id_hash,
    base_delay_ms,
    slot_count,
    slot_width_ms);
}

inline uint32_t duplicateCacheTtlMs(
    const FrameIdentityV1& identity,
    uint16_t ordinary_ttl_s) {
  if (identity.message_type ==
      static_cast<uint8_t>(EquineProtocol::MessageType::ACK)) {
    return ACK_DUPLICATE_CACHE_MS;
  }
  const uint32_t ttl_ms = static_cast<uint32_t>(ordinary_ttl_s) * 1000UL;
  return ttl_ms < ACK_DUPLICATE_CACHE_MS ? ACK_DUPLICATE_CACHE_MS : ttl_ms;
}

inline bool peerForwardSuppressesPending(
    uint8_t incoming_hop,
    uint8_t pending_outgoing_hop) {
  return incoming_hop >= pending_outgoing_hop;
}

// Semtech LoRa time-on-air equation for explicit-header packets. This is used
// by the repeater's conservative token bucket; the radio's measured transmit
// duration is also charged after each send.
inline uint32_t estimateAirtimeMs(
    size_t packet_size,
    uint8_t spreading_factor,
    uint32_t bandwidth_hz,
    uint8_t coding_rate_denominator,
    uint8_t preamble_symbols,
    bool crc_enabled = true) {
  const LoraTrackerCore::RadioConfig radio{
    868100000UL, bandwidth_hz, 14, spreading_factor,
    coding_rate_denominator, preamble_symbols};
  return LoraTrackerCore::estimateAirtimeMs(
    packet_size, radio, crc_enabled);
}

inline double refillRollingHourAirtimeTokens(
    double tokens_ms,
    uint64_t elapsed_ms,
    uint32_t legal_budget_ms_per_hour,
    uint32_t capacity_ms) {
  return LoraTrackerCore::refillAirtimeTokens(
    tokens_ms, elapsed_ms, legal_budget_ms_per_hour, capacity_ms);
}

inline uint32_t maxFrameAirtimeMs(
    uint8_t spreading_factor,
    uint32_t bandwidth_hz,
    uint8_t coding_rate_denominator,
    uint8_t preamble_symbols) {
  return estimateAirtimeMs(
    MAX_PACKET_SIZE, spreading_factor, bandwidth_hz,
    coding_rate_denominator, preamble_symbols);
}

inline bool consumeAirtimeTokens(double& tokens_ms, uint32_t airtime_ms) {
  return LoraTrackerCore::consumeAirtimeTokens(tokens_ms, airtime_ms);
}

}  // namespace EquineRelay
