#pragma once

#include <stddef.h>
#include <stdint.h>

namespace LoraTrackerCore {

constexpr uint32_t GERMANY_FREQUENCY_MIN_HZ = 868000000UL;
constexpr uint32_t GERMANY_FREQUENCY_MAX_HZ = 868600000UL;
constexpr int8_t GERMANY_MAX_CONDUCTED_POWER_DBM = 14;
constexpr uint32_t GERMANY_AIRTIME_BUDGET_MS_PER_HOUR = 36000UL;

struct RadioConfig {
  uint32_t frequency_hz;
  uint32_t bandwidth_hz;
  int8_t tx_power_dbm;
  uint8_t spreading_factor;
  uint8_t coding_rate_denominator;
  uint8_t preamble_symbols;
};

struct TrackerPolicy {
  uint32_t moving_sleep_s;
  uint32_t stationary_sleep_s;
  uint32_t long_stationary_sleep_s;
  uint32_t no_fix_sleep_s[4];
  uint16_t stationary_fixes_for_long_sleep;
  uint16_t stationary_fixes_for_max_sleep;
  uint32_t tx_interval_s;
  uint16_t tx_min_points;
  uint32_t retry_backoff_s[4];
};

bool validateGermanyRadio(const RadioConfig& radio);
uint32_t estimateAirtimeMs(
    size_t packet_size, const RadioConfig& radio, bool crc_enabled = true);
uint32_t ackRelayGuardMs(
    size_t received_packet_size,
    const RadioConfig& radio,
    uint8_t incoming_hop,
    uint8_t hop_limit,
    uint16_t maximum_forwarding_delay_ms = 400);
double receiverSensitivityDbm(const RadioConfig& radio, double noise_figure_db = 6.0);
uint32_t trackerSleepSeconds(
    const TrackerPolicy& policy,
    bool fix_found,
    bool movement_accepted,
    uint16_t stationary_fix_streak,
    uint8_t consecutive_no_fix_cycles);
uint32_t trackerRetryBackoffSeconds(
    const TrackerPolicy& policy,
    uint8_t consecutive_failures);
bool trackerBatchDue(
    const TrackerPolicy& policy,
    uint16_t queued_points,
    uint32_t seconds_since_ack,
    uint16_t history_capacity,
    uint16_t reserve_points = 16);
uint64_t mix64(uint64_t value);
uint32_t forwardingDelayMs(
    uint64_t device_hash,
    uint32_t boot_id,
    uint32_t counter,
    uint8_t message_type,
    uint8_t schema_version,
    uint64_t repeater_hash,
    uint16_t base_delay_ms,
    uint8_t slot_count,
    uint16_t slot_width_ms);
double refillAirtimeTokens(
    double tokens_ms,
    uint64_t elapsed_ms,
    uint32_t legal_budget_ms_per_hour,
    uint32_t capacity_ms);
bool consumeAirtimeTokens(double& tokens_ms, uint32_t airtime_ms);

}  // namespace LoraTrackerCore

extern "C" {
int lt_core_version();
int lt_validate_germany_radio(
    uint32_t frequency_hz, uint32_t bandwidth_hz, int tx_power_dbm,
    int spreading_factor, int coding_rate_denominator, int preamble_symbols);
uint32_t lt_airtime_ms(
    uint32_t packet_size, uint32_t frequency_hz, uint32_t bandwidth_hz,
    int tx_power_dbm, int spreading_factor, int coding_rate_denominator,
    int preamble_symbols);
uint32_t lt_ack_relay_guard_ms(
    uint32_t packet_size, uint32_t bandwidth_hz, int spreading_factor,
    int coding_rate_denominator, int preamble_symbols,
    int incoming_hop, int hop_limit, int maximum_forwarding_delay_ms);
double lt_receiver_sensitivity_dbm(
    uint32_t bandwidth_hz, int spreading_factor, double noise_figure_db);
uint32_t lt_tracker_sleep_s(
    uint32_t moving_s, uint32_t stationary_s, uint32_t long_stationary_s,
    uint32_t no_fix_1_s, uint32_t no_fix_2_s, uint32_t no_fix_3_s,
    uint32_t no_fix_max_s, uint32_t long_after, uint32_t max_after,
    int fix_found, int movement_accepted, uint32_t stationary_streak,
    uint32_t no_fix_cycles);
uint32_t lt_tracker_retry_s(
    uint32_t retry_1_s, uint32_t retry_2_s, uint32_t retry_3_s,
    uint32_t retry_max_s, uint32_t consecutive_failures);
int lt_tracker_batch_due(
    uint32_t tx_interval_s, uint32_t tx_min_points, uint32_t queued_points,
    uint32_t seconds_since_ack, uint32_t history_capacity);
uint32_t lt_forwarding_delay_ms(
    uint32_t device_hash_hi, uint32_t device_hash_lo, uint32_t boot_id,
    uint32_t counter, uint32_t message_type, uint32_t schema_version,
    uint32_t repeater_hash_hi, uint32_t repeater_hash_lo,
    uint32_t base_delay_ms, uint32_t slot_count, uint32_t slot_width_ms);
}
