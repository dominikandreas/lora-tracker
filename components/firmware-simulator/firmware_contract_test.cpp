#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "equine_config.h"

namespace {

void testProtocol() {
  using namespace EquineProtocol;

  const uint64_t hash = deviceIdHash("horse-1");
  assert(hash == 0x320F3B2F2CBE9920ULL);
  const FrameHeaderV1 header = makeFrameHeader(
      MessageType::HISTORY, HISTORY_SCHEMA_VERSION, hash,
      FLAG_HAS_TIMESTAMPS | FLAG_ENCRYPTED);
  assert(isSupportedHistoryFrame(header));
  assert(!isSupportedFrame(header, MessageType::ACK, ACK_SCHEMA_VERSION));

  char rendered_hash[17]{};
  formatDeviceHash(hash, rendered_hash, sizeof(rendered_hash));
  assert(strcmp(rendered_hash, "320f3b2f2cbe9920") == 0);

  const SecureFrameHeaderV2 secure = makeSecureFrameHeader(
      MessageType::HISTORY, HISTORY_SCHEMA_VERSION, hash,
      0x0102030405060708ULL, 7, 42, FLAG_HAS_TIMESTAMPS);
  uint8_t nonce[AEAD_NONCE_SIZE]{};
  makeAeadNonce(secure, nonce);
  const uint8_t expected_nonce[] = {
    0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01,
    0x2a, 0x00, 0x00, 0x00};
  assert(memcmp(nonce, expected_nonce, sizeof(nonce)) == 0);
  HistoryPayloadV2 payload{};
  payload.first_seq = 123;
  assert(payload.first_seq == 123 && sizeof(payload) == 11);

  FrameHeaderV1 unknown_flags = header;
  unknown_flags.flags |= 0x80;
  assert(!isSupportedHistoryFrame(unknown_flags));

  const uint32_t values[] = {0, 1, 127, 128, 16383, 16384, UINT32_MAX};
  for (uint32_t value : values) {
    uint8_t encoded[MAX_ULEB128_U32_BYTES]{};
    const size_t written = encodeUleb128U32(value, encoded, sizeof(encoded));
    assert(written == uleb128SizeU32(value));
    uint32_t decoded = 0;
    size_t consumed = 0;
    assert(decodeUleb128U32(encoded, written, decoded, consumed));
    assert(decoded == value && consumed == written);
  }
  const uint8_t overflow[] = {0xff, 0xff, 0xff, 0xff, 0x70};
  uint32_t decoded = 0;
  size_t consumed = 0;
  assert(!decodeUleb128U32(overflow, sizeof(overflow), decoded, consumed));
}

void testConfiguration() {
  using namespace EquineConfig;

  TrackerConfigV1 tracker{};
  uint8_t key[EquineProtocol::AEAD_KEY_SIZE];
  memset(key, 0x5a, sizeof(key));
  makeDefaultTrackerConfig(
      tracker, "horse-1", "Horse 1", "stable", "secret", key);
  assert(validateTrackerConfig(tracker));
  tracker.min_satellites = 2;
  assert(!validateTrackerConfig(tracker));
  tracker.min_satellites = 6;
  finalize(tracker, DeviceRole::TRACKER, tracker.header.revision);
  assert(validateTrackerConfig(tracker));
  tracker.header.crc32 ^= 1;
  assert(!validateTrackerConfig(tracker));

  GatewayConfigV1 gateway{};
  makeDefaultGatewayConfig(gateway, "home", "Home gateway", "stable", "secret",
                           "mqtt.example", 1883, "", "");
  gateway.tracker_count = 2;
  strlcpy(gateway.trackers[0].device_id, "horse-1", sizeof(gateway.trackers[0].device_id));
  strlcpy(gateway.trackers[0].device_name, "Horse 1", sizeof(gateway.trackers[0].device_name));
  gateway.trackers[0].enabled = 1;
  memcpy(gateway.trackers[0].lora_aead_key, key, sizeof(key));
  strlcpy(gateway.trackers[1].device_id, "horse-2", sizeof(gateway.trackers[1].device_id));
  strlcpy(gateway.trackers[1].device_name, "Horse 2", sizeof(gateway.trackers[1].device_name));
  gateway.trackers[1].enabled = 1;
  memcpy(gateway.trackers[1].lora_aead_key, key, sizeof(key));
  finalize(gateway, DeviceRole::GATEWAY, 1);
  assert(validateEnvelope(gateway, DeviceRole::GATEWAY));
  assert(isValidCanonicalId(gateway.gateway_id, sizeof(gateway.gateway_id)));
  assert(isValidDisplayName(gateway.gateway_name, sizeof(gateway.gateway_name)));
  assert(isValidCanonicalId(gateway.mqtt_base_topic, sizeof(gateway.mqtt_base_topic)));
  assert(validateLoRa(gateway.lora));
  assert(isNullTerminated(gateway.wifi_ssid, sizeof(gateway.wifi_ssid)));
  assert(isNullTerminated(gateway.wifi_password, sizeof(gateway.wifi_password)));
  assert(isNullTerminated(gateway.mqtt_host, sizeof(gateway.mqtt_host)));
  assert(strnlen(gateway.mqtt_host, sizeof(gateway.mqtt_host)) > 0);
  assert(isNullTerminated(gateway.mqtt_username, sizeof(gateway.mqtt_username)));
  assert(isNullTerminated(gateway.mqtt_password, sizeof(gateway.mqtt_password)));
  assert(gateway.mqtt_port != 0 && gateway.mqtt_tls_enabled <= 1);
  assert(gateway.mqtt_buffer_size >= 512 && gateway.mqtt_buffer_size <= 4096);
  assert(gateway.dedup_save_interval > 0 && gateway.dedup_save_interval <= 1000);
  assert(gateway.wifi_retry_interval_ms >= 1000 && gateway.mqtt_retry_interval_ms >= 1000);
  assert(gateway.tracker_count <= MAX_GATEWAY_TRACKERS);
  assert(isValidCanonicalId(gateway.trackers[0].device_id, sizeof(gateway.trackers[0].device_id)));
  assert(isValidDisplayName(gateway.trackers[0].device_name, sizeof(gateway.trackers[0].device_name)));
  assert(isValidCanonicalId(gateway.trackers[1].device_id, sizeof(gateway.trackers[1].device_id)));
  assert(isValidDisplayName(gateway.trackers[1].device_name, sizeof(gateway.trackers[1].device_name)));
  assert(EquineProtocol::deviceIdHash(gateway.trackers[0].device_id) !=
         EquineProtocol::deviceIdHash(gateway.trackers[1].device_id));
  assert(validateGatewayConfig(gateway));
  strlcpy(gateway.trackers[1].device_id, "horse-1", sizeof(gateway.trackers[1].device_id));
  finalize(gateway, DeviceRole::GATEWAY, 1);
  assert(!validateGatewayConfig(gateway));
}

}  // namespace

int main() {
  testProtocol();
  testConfiguration();
  return 0;
}
