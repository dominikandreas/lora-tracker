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
  gateway.trackers[0].accepts_legacy_lora = 1;
  strlcpy(gateway.trackers[1].device_id, "horse-2", sizeof(gateway.trackers[1].device_id));
  strlcpy(gateway.trackers[1].device_name, "Horse 2", sizeof(gateway.trackers[1].device_name));
  gateway.trackers[1].enabled = 1;
  finalize(gateway, DeviceRole::GATEWAY, 1);
  assert(validateGatewayConfig(gateway));
  gateway.trackers[1].accepts_legacy_lora = 1;
  finalize(gateway, DeviceRole::GATEWAY, 1);
  assert(!validateGatewayConfig(gateway));
}

}  // namespace

int main() {
  testProtocol();
  testConfiguration();
  return 0;
}
