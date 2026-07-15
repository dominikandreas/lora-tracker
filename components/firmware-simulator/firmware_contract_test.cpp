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
      MessageType::HISTORY, HISTORY_SCHEMA_VERSION, hash, FLAG_HAS_TIMESTAMPS);
  assert(isSupportedHistoryFrame(header));
  assert(!isSupportedFrame(header, MessageType::ACK, ACK_SCHEMA_VERSION));

  char rendered_hash[17]{};
  formatDeviceHash(hash, rendered_hash, sizeof(rendered_hash));
  assert(strcmp(rendered_hash, "320f3b2f2cbe9920") == 0);

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
  makeDefaultTrackerConfig(tracker, "horse-1", "Horse 1", "stable", "secret");
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
  strlcpy(gateway.trackers[1].device_id, "horse-2", sizeof(gateway.trackers[1].device_id));
  strlcpy(gateway.trackers[1].device_name, "Horse 2", sizeof(gateway.trackers[1].device_name));
  gateway.trackers[1].enabled = 1;
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
