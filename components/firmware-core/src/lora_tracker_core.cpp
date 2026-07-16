#include "../include/lora_tracker_core.h"

#include <math.h>

namespace LoraTrackerCore {

namespace {

bool supportedBandwidth(uint32_t bandwidth_hz) {
  return bandwidth_hz == 62500UL || bandwidth_hz == 125000UL ||
         bandwidth_hz == 250000UL || bandwidth_hz == 500000UL;
}

double requiredSnrDb(uint8_t spreading_factor) {
  static const double values[] = {-7.5, -10.0, -12.5, -15.0, -17.5, -20.0};
  return values[spreading_factor - 7];
}

RadioConfig makeRadio(
    uint32_t frequency_hz, uint32_t bandwidth_hz, int tx_power_dbm,
    int spreading_factor, int coding_rate_denominator, int preamble_symbols) {
  RadioConfig radio{};
  radio.frequency_hz = frequency_hz;
  radio.bandwidth_hz = bandwidth_hz;
  radio.tx_power_dbm = static_cast<int8_t>(tx_power_dbm);
  radio.spreading_factor = static_cast<uint8_t>(spreading_factor);
  radio.coding_rate_denominator = static_cast<uint8_t>(coding_rate_denominator);
  radio.preamble_symbols = static_cast<uint8_t>(preamble_symbols);
  return radio;
}

}  // namespace

bool validateGermanyRadio(const RadioConfig& radio) {
  if (!supportedBandwidth(radio.bandwidth_hz)) return false;
  const uint32_t half_bandwidth = radio.bandwidth_hz / 2UL;
  return radio.frequency_hz >= GERMANY_FREQUENCY_MIN_HZ + half_bandwidth &&
         radio.frequency_hz <= GERMANY_FREQUENCY_MAX_HZ - half_bandwidth &&
         radio.tx_power_dbm >= 2 &&
         radio.tx_power_dbm <= GERMANY_MAX_CONDUCTED_POWER_DBM &&
         radio.spreading_factor >= 7 && radio.spreading_factor <= 12 &&
         radio.coding_rate_denominator >= 5 &&
         radio.coding_rate_denominator <= 8 &&
         radio.preamble_symbols >= 6 && radio.preamble_symbols <= 32;
}

uint32_t estimateAirtimeMs(
    size_t packet_size, const RadioConfig& radio, bool crc_enabled) {
  if (packet_size == 0 || packet_size > 255 ||
      radio.spreading_factor < 7 || radio.spreading_factor > 12 ||
      radio.bandwidth_hz == 0 || radio.coding_rate_denominator < 5 ||
      radio.coding_rate_denominator > 8) {
    return 0;
  }
  const double symbol_s =
      static_cast<double>(1UL << radio.spreading_factor) / radio.bandwidth_hz;
  const int low_data_rate_optimize = symbol_s >= 0.016 ? 1 : 0;
  const int numerator =
      8 * static_cast<int>(packet_size) - 4 * radio.spreading_factor + 28 +
      (crc_enabled ? 16 : 0);
  const int denominator =
      4 * (static_cast<int>(radio.spreading_factor) -
           2 * low_data_rate_optimize);
  const double coded_blocks = numerator > 0
      ? ceil(static_cast<double>(numerator) / denominator)
      : 0.0;
  const double payload_symbols =
      8.0 + coded_blocks * radio.coding_rate_denominator;
  const double total_s =
      (static_cast<double>(radio.preamble_symbols) + 4.25 + payload_symbols) *
      symbol_s;
  return static_cast<uint32_t>(ceil(total_s * 1000.0));
}

uint32_t ackRelayGuardMs(
    size_t received_packet_size,
    const RadioConfig& radio,
    uint8_t incoming_hop,
    uint8_t hop_limit,
    uint16_t maximum_forwarding_delay_ms) {
  if (incoming_hop >= hop_limit) return 0;
  const uint32_t airtime_ms = estimateAirtimeMs(received_packet_size, radio);
  if (airtime_ms == 0 || UINT32_MAX - airtime_ms < maximum_forwarding_delay_ms) {
    return 0;
  }
  return airtime_ms + maximum_forwarding_delay_ms;
}

double receiverSensitivityDbm(const RadioConfig& radio, double noise_figure_db) {
  if (radio.spreading_factor < 7 || radio.spreading_factor > 12 ||
      radio.bandwidth_hz == 0 || !isfinite(noise_figure_db)) {
    return -999.0;
  }
  return -174.0 + 10.0 * log10(static_cast<double>(radio.bandwidth_hz)) +
         noise_figure_db + requiredSnrDb(radio.spreading_factor);
}

uint32_t trackerSleepSeconds(
    const TrackerPolicy& policy,
    bool fix_found,
    bool movement_accepted,
    uint16_t stationary_fix_streak,
    uint8_t consecutive_no_fix_cycles) {
  if (!fix_found) {
    if (consecutive_no_fix_cycles <= 1) return policy.no_fix_sleep_s[0];
    if (consecutive_no_fix_cycles == 2) return policy.no_fix_sleep_s[1];
    if (consecutive_no_fix_cycles == 3) return policy.no_fix_sleep_s[2];
    return policy.no_fix_sleep_s[3];
  }
  if (movement_accepted) return policy.moving_sleep_s;
  if (stationary_fix_streak >= policy.stationary_fixes_for_max_sleep) {
    return policy.long_stationary_sleep_s;
  }
  if (stationary_fix_streak >= policy.stationary_fixes_for_long_sleep) {
    return policy.stationary_sleep_s;
  }
  return policy.moving_sleep_s;
}

uint32_t trackerRetryBackoffSeconds(
    const TrackerPolicy& policy,
    uint8_t consecutive_failures) {
  if (consecutive_failures == 0) return 0;
  if (consecutive_failures == 1) return policy.retry_backoff_s[0];
  if (consecutive_failures == 2) return policy.retry_backoff_s[1];
  if (consecutive_failures == 3) return policy.retry_backoff_s[2];
  return policy.retry_backoff_s[3];
}

bool trackerBatchDue(
    const TrackerPolicy& policy,
    uint16_t queued_points,
    uint32_t seconds_since_ack,
    uint16_t history_capacity,
    uint16_t reserve_points) {
  if (queued_points == 0) return false;
  const uint16_t pressure_threshold = history_capacity > reserve_points
      ? history_capacity - reserve_points
      : history_capacity;
  return queued_points >= policy.tx_min_points ||
         seconds_since_ack >= policy.tx_interval_s ||
         queued_points >= pressure_threshold;
}

uint64_t mix64(uint64_t value) {
  value ^= value >> 30;
  value *= 0xbf58476d1ce4e5b9ULL;
  value ^= value >> 27;
  value *= 0x94d049bb133111ebULL;
  return value ^ (value >> 31);
}

uint32_t forwardingDelayMs(
    uint64_t device_hash,
    uint32_t boot_id,
    uint32_t counter,
    uint8_t message_type,
    uint8_t schema_version,
    uint64_t repeater_hash,
    uint16_t base_delay_ms,
    uint8_t slot_count,
    uint16_t slot_width_ms) {
  if (slot_count == 0) return base_delay_ms;
  uint64_t identity = device_hash;
  identity ^= (static_cast<uint64_t>(boot_id) << 32) | counter;
  identity ^= static_cast<uint64_t>(message_type) << 56;
  identity ^= static_cast<uint64_t>(schema_version) << 48;
  const uint64_t ranked = mix64(mix64(identity) ^ repeater_hash);
  return static_cast<uint32_t>(base_delay_ms) +
         static_cast<uint32_t>(ranked % slot_count) * slot_width_ms;
}

double refillAirtimeTokens(
    double tokens_ms,
    uint64_t elapsed_ms,
    uint32_t legal_budget_ms_per_hour,
    uint32_t capacity_ms) {
  if (capacity_ms == 0 || capacity_ms >= legal_budget_ms_per_hour) return 0.0;
  const uint32_t refill_ms_per_hour = legal_budget_ms_per_hour - capacity_ms;
  tokens_ms += static_cast<double>(elapsed_ms) * refill_ms_per_hour / 3600000.0;
  return tokens_ms > capacity_ms ? capacity_ms : tokens_ms;
}

bool consumeAirtimeTokens(double& tokens_ms, uint32_t airtime_ms) {
  if (airtime_ms == 0 || tokens_ms < airtime_ms) return false;
  tokens_ms -= airtime_ms;
  return true;
}

}  // namespace LoraTrackerCore

extern "C" {

int lt_core_version() { return 1; }

int lt_validate_germany_radio(
    uint32_t frequency_hz, uint32_t bandwidth_hz, int tx_power_dbm,
    int spreading_factor, int coding_rate_denominator, int preamble_symbols) {
  if (tx_power_dbm < -128 || tx_power_dbm > 127 ||
      spreading_factor < 0 || spreading_factor > 255 ||
      coding_rate_denominator < 0 || coding_rate_denominator > 255 ||
      preamble_symbols < 0 || preamble_symbols > 255) {
    return 0;
  }
  return LoraTrackerCore::validateGermanyRadio(LoraTrackerCore::makeRadio(
      frequency_hz, bandwidth_hz, tx_power_dbm, spreading_factor,
      coding_rate_denominator, preamble_symbols));
}

uint32_t lt_airtime_ms(
    uint32_t packet_size, uint32_t frequency_hz, uint32_t bandwidth_hz,
    int tx_power_dbm, int spreading_factor, int coding_rate_denominator,
    int preamble_symbols) {
  return LoraTrackerCore::estimateAirtimeMs(
      packet_size, LoraTrackerCore::makeRadio(
          frequency_hz, bandwidth_hz, tx_power_dbm, spreading_factor,
          coding_rate_denominator, preamble_symbols));
}

uint32_t lt_ack_relay_guard_ms(
    uint32_t packet_size, uint32_t bandwidth_hz, int spreading_factor,
    int coding_rate_denominator, int preamble_symbols,
    int incoming_hop, int hop_limit, int maximum_forwarding_delay_ms) {
  return LoraTrackerCore::ackRelayGuardMs(
      packet_size,
      LoraTrackerCore::makeRadio(
          868100000UL, bandwidth_hz, 14, spreading_factor,
          coding_rate_denominator, preamble_symbols),
      static_cast<uint8_t>(incoming_hop), static_cast<uint8_t>(hop_limit),
      static_cast<uint16_t>(maximum_forwarding_delay_ms));
}

double lt_receiver_sensitivity_dbm(
    uint32_t bandwidth_hz, int spreading_factor, double noise_figure_db) {
  return LoraTrackerCore::receiverSensitivityDbm(
      LoraTrackerCore::makeRadio(
          868100000UL, bandwidth_hz, 14, spreading_factor, 5, 8),
      noise_figure_db);
}

uint32_t lt_tracker_sleep_s(
    uint32_t moving_s, uint32_t stationary_s, uint32_t long_stationary_s,
    uint32_t no_fix_1_s, uint32_t no_fix_2_s, uint32_t no_fix_3_s,
    uint32_t no_fix_max_s, uint32_t long_after, uint32_t max_after,
    int fix_found, int movement_accepted, uint32_t stationary_streak,
    uint32_t no_fix_cycles) {
  LoraTrackerCore::TrackerPolicy policy{};
  policy.moving_sleep_s = moving_s;
  policy.stationary_sleep_s = stationary_s;
  policy.long_stationary_sleep_s = long_stationary_s;
  policy.no_fix_sleep_s[0] = no_fix_1_s;
  policy.no_fix_sleep_s[1] = no_fix_2_s;
  policy.no_fix_sleep_s[2] = no_fix_3_s;
  policy.no_fix_sleep_s[3] = no_fix_max_s;
  policy.stationary_fixes_for_long_sleep = static_cast<uint16_t>(long_after);
  policy.stationary_fixes_for_max_sleep = static_cast<uint16_t>(max_after);
  return LoraTrackerCore::trackerSleepSeconds(
      policy, fix_found != 0, movement_accepted != 0,
      static_cast<uint16_t>(stationary_streak),
      static_cast<uint8_t>(no_fix_cycles));
}

uint32_t lt_tracker_retry_s(
    uint32_t retry_1_s, uint32_t retry_2_s, uint32_t retry_3_s,
    uint32_t retry_max_s, uint32_t consecutive_failures) {
  LoraTrackerCore::TrackerPolicy policy{};
  policy.retry_backoff_s[0] = retry_1_s;
  policy.retry_backoff_s[1] = retry_2_s;
  policy.retry_backoff_s[2] = retry_3_s;
  policy.retry_backoff_s[3] = retry_max_s;
  return LoraTrackerCore::trackerRetryBackoffSeconds(
      policy, static_cast<uint8_t>(consecutive_failures));
}

int lt_tracker_batch_due(
    uint32_t tx_interval_s, uint32_t tx_min_points, uint32_t queued_points,
    uint32_t seconds_since_ack, uint32_t history_capacity) {
  LoraTrackerCore::TrackerPolicy policy{};
  policy.tx_interval_s = tx_interval_s;
  policy.tx_min_points = static_cast<uint16_t>(tx_min_points);
  return LoraTrackerCore::trackerBatchDue(
      policy, static_cast<uint16_t>(queued_points), seconds_since_ack,
      static_cast<uint16_t>(history_capacity));
}

uint32_t lt_forwarding_delay_ms(
    uint32_t device_hash_hi, uint32_t device_hash_lo, uint32_t boot_id,
    uint32_t counter, uint32_t message_type, uint32_t schema_version,
    uint32_t repeater_hash_hi, uint32_t repeater_hash_lo,
    uint32_t base_delay_ms, uint32_t slot_count, uint32_t slot_width_ms) {
  const uint64_t device_hash =
      (static_cast<uint64_t>(device_hash_hi) << 32) | device_hash_lo;
  const uint64_t repeater_hash =
      (static_cast<uint64_t>(repeater_hash_hi) << 32) | repeater_hash_lo;
  return LoraTrackerCore::forwardingDelayMs(
      device_hash, boot_id, counter, static_cast<uint8_t>(message_type),
      static_cast<uint8_t>(schema_version), repeater_hash,
      static_cast<uint16_t>(base_delay_ms), static_cast<uint8_t>(slot_count),
      static_cast<uint16_t>(slot_width_ms));
}

}  // extern "C"
