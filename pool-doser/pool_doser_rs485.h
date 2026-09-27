#pragma once

#include "esphome.h"
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "pool_doser_control.h"
#include "esphome/components/pool_storage/pool_storage.h"

namespace pool_doser {

// v5 binds every exchange to both controller sessions. Mixed versions fail off.
static constexpr uint8_t PROTOCOL_VERSION = 5;
static constexpr uint8_t SOF_1 = 0xA5;
static constexpr uint8_t SOF_2 = 0x5A;
static constexpr size_t FRAME_SIZE = 45;
static constexpr uint32_t LINK_TIMEOUT_MS = 1000;
static constexpr uint32_t MAX_DOSE_MS = 900000;
static constexpr uint32_t MIN_DOSE_MS = 1000;
static constexpr uint32_t RESPONSE_TIMEOUT_MS = 150;
static constexpr uint8_t MAX_REQUEST_RETRIES = 2;

enum Command : uint8_t {
  STATUS_REQUEST = 1,
  STATUS_RESPONSE = 2,
  OPEN_B = 3,
  TEST_CLOSE_B = 4,
  DOSE_B = 5,
  ABORT_B = 6,
  CLEAR_FAULT = 7,
  // Command-specific post-transition state probe.
  PROBE_STATUS = 8,
};

enum Flags : uint16_t {
  RELAY_B_CLOSED = 1 << 0,
  OUTLET_LIVE = 1 << 1,
  B_ACTIVE = 1 << 2,
  B_FAULT = 1 << 3,
  B_OUTPUT_PROVEN = 1 << 4,
  MAINTENANCE_LOCKED = 1 << 5,
  B_WIFI_ENABLED = 1 << 6,
  ALLOW_MAINTENANCE_WIFI = 1 << 7,
  B_DOSING_READY = 1 << 8,
};

// Optional Wi-Fi diagnostic bits; zero means no connected signal is known.
static constexpr uint8_t WIFI_RSSI_SHIFT = 9;
static constexpr uint16_t WIFI_RSSI_MASK = 0xFE00;

inline uint16_t encode_wifi_rssi(int32_t rssi) {
  if (rssi < -127 || rssi >= 0) return 0;
  return static_cast<uint16_t>(-rssi) << WIFI_RSSI_SHIFT;
}

enum Fault : uint8_t {
  FAULT_NONE = 0,
  FAULT_BAD_COMMAND = 1,
  FAULT_LINK_LOST = 2,
  FAULT_POWER_DURING_A_OPEN_TEST = 3,
  FAULT_OUTPUT_LOST = 4,
  FAULT_INTERRUPTED = 5,
  FAULT_OUTPUT_LIVE_WHEN_OFF = 6,
};

inline bool physical_fault(uint8_t fault) {
  return fault == FAULT_POWER_DURING_A_OPEN_TEST || fault == FAULT_OUTPUT_LOST ||
         fault == FAULT_OUTPUT_LIVE_WHEN_OFF;
}
inline bool interruption_fault(uint8_t fault) {
  return fault == FAULT_LINK_LOST || fault == FAULT_INTERRUPTED || fault == FAULT_BAD_COMMAND;
}

struct Frame {
  uint8_t command{0};
  uint32_t sequence{0};
  uint64_t master_session{0}, slave_session{0};
  uint32_t transaction{0};
  // Request duration, or acknowledged request sequence in STATUS_RESPONSE.
  uint32_t duration_ms{0};
  uint16_t flags{0};
  uint32_t remaining_ms{0};
  uint32_t live_ms{0};  // Reserved v5 field; transmitted as zero.
  uint8_t fault{0};
};

inline float slave_wifi_rssi(bool connected, const Frame &frame) {
  const uint16_t required = MAINTENANCE_LOCKED | B_WIFI_ENABLED;
  const uint16_t magnitude = (frame.flags & WIFI_RSSI_MASK) >> WIFI_RSSI_SHIFT;
  if (!connected || frame.command != STATUS_RESPONSE ||
      (frame.flags & required) != required || magnitude == 0)
    return std::numeric_limits<float>::quiet_NaN();
  return -static_cast<float>(magnitude);
}

inline bool state_edit_allowed(bool dose_request_running, bool sequence_running,
                               bool delivery_open) {
  return !dose_request_running && !sequence_running && !delivery_open;
}

inline bool delivery_start_interlocks_passed(
    bool settings_valid, bool connected, bool request_failed, uint8_t slave_fault,
    bool flow_has_state, bool flow_state, bool maintenance_lockout, bool relay_fault,
    bool transaction_matches, bool relay_a_closed, bool slave_active,
    bool relay_b_closed, bool outlet_live) {
  return settings_valid && connected && !request_failed && slave_fault == FAULT_NONE &&
         flow_has_state && flow_state && !maintenance_lockout && !relay_fault &&
         transaction_matches && !relay_a_closed && !slave_active && !relay_b_closed && !outlet_live;
}

inline bool one_open_relay_proof_passed(
    bool fresh_probe_ack, bool transaction_matches, bool relay_b_closed,
    bool expected_relay_b_closed, bool outlet_live, uint8_t slave_fault) {
  return fresh_probe_ack && transaction_matches &&
         relay_b_closed == expected_relay_b_closed && !outlet_live &&
         slave_fault == FAULT_NONE;
}

inline bool verified_slave_shutdown(bool connected, bool fresh_shutdown_state,
                                    bool transaction_matches, uint16_t flags,
                                    uint8_t slave_fault) {
  return connected && fresh_shutdown_state && transaction_matches &&
         !(flags & B_ACTIVE) && !(flags & RELAY_B_CLOSED) &&
         !(flags & OUTLET_LIVE) && !(flags & B_FAULT) &&
         slave_fault == FAULT_NONE;
}

inline uint32_t crc32(const uint8_t *data, size_t length) {
  uint32_t crc = 0xFFFFFFFFU;
  for (size_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++)
      crc = (crc & 1U) ? (crc >> 1U) ^ 0xEDB88320U : crc >> 1U;
  }
  return ~crc;
}

inline bool calibration_factor_valid(float factor) {
  return std::isfinite(factor) && factor >= 0.01f && factor <= 10.0f;
}

// The factor multiplies run time; the corresponding measured rate is inverse.
inline float calibrated_output_rate(float rated_gpd, float factor) {
  if (!std::isfinite(rated_gpd) || rated_gpd <= 0.0f ||
      !calibration_factor_valid(factor))
    return std::numeric_limits<float>::quiet_NaN();
  return rated_gpd / (11.25f * factor);
}

// The Master's only saved record: deliberate configuration, calibration and
// maintenance intent. Runtime state never enters it.
struct Settings {
  uint32_t magic{0x50445332U};
  uint16_t version{2}, calibration_factor_milli{1000};
  float pump_gpd{0}, max_single_dose_oz{13}, pre_run_s{60}, min_rest_s{1800};
  uint8_t maintenance{1}, reserved[3]{};
  uint32_t checksum{0};
  float calibration_factor() const { return calibration_factor_milli / 1000.0f; }
};
static_assert(sizeof(Settings) == 32, "Version the settings layout when changing it");
inline uint32_t settings_checksum(const Settings &s) {
  return crc32(reinterpret_cast<const uint8_t *>(&s), offsetof(Settings, checksum));
}
inline bool settings_valid(const Settings &s) {
  return s.magic == 0x50445332U && s.version == 2 &&
    calibration_factor_valid(s.calibration_factor()) &&
    std::isfinite(s.pump_gpd) && s.pump_gpd >= 0 && s.pump_gpd <= 200 &&
    std::isfinite(s.max_single_dose_oz) && s.max_single_dose_oz >= 0.5f && s.max_single_dose_oz <= 128 &&
    std::isfinite(s.pre_run_s) && s.pre_run_s >= 0 && s.pre_run_s <= 1800 &&
    std::isfinite(s.min_rest_s) && s.min_rest_s >= 0 && s.min_rest_s <= 43200 &&
    s.maintenance <= 1 && s.checksum == settings_checksum(s);
}
class SettingsStore {
 public:
  bool begin() {
    Settings s;
    healthy_ = pool_storage::load("doser_v2", &s, sizeof(s)) && settings_valid(s);
    if (healthy_) { state_ = s; loaded_ = true; }
    return healthy_;
  }
  // A single-field edit rebuilds the whole record from displayed values, so it
  // must not save built-in defaults over a missing or unreadable record.
  bool save_edit(const Settings &next) { return loaded_ && save(next); }
  bool save(Settings next) {
    next.checksum = settings_checksum(next);
    if (!settings_valid(next)) { healthy_ = false; return false; }
    if (healthy_ && std::memcmp(&state_, &next, sizeof(next)) == 0) return true;
    healthy_ = pool_storage::save("doser_v2", &next, sizeof(next));
    if (healthy_) { state_ = next; loaded_ = true; }
    return healthy_;
  }
  const Settings &state() const { return state_; }
  bool healthy() const { return healthy_; }
  bool loaded() const { return loaded_; }
 private:
  Settings state_{};
  bool healthy_{false};
  bool loaded_{false};
};
inline SettingsStore settings_store;

inline bool sequence_requires_flow(bool sequence_running,
                                   bool dosing_active) {
  return sequence_running || dosing_active;
}

inline bool should_abort_for_flow(bool sequence_running, bool dosing_active,
                                  bool flow_has_state, bool flow_state) {
  return sequence_requires_flow(sequence_running, dosing_active) &&
         (!flow_has_state || !flow_state);
}

inline bool should_abort_for_slave_output(
    bool dosing_active, bool output_voltage_proven,
    uint32_t elapsed_ms, uint32_t total_ms, bool outlet_live, bool slave_active,
    uint8_t slave_fault, bool abort_already_issued) {
  return dosing_active && output_voltage_proven && elapsed_ms + 750U < total_ms &&
         (!outlet_live || !slave_active || slave_fault != FAULT_NONE) &&
         !abort_already_issued;
}

inline bool should_abort_link_failure(
    bool sequence_running, bool dosing_active, bool connected,
    bool request_failed, bool abort_already_issued) {
  return (sequence_running || dosing_active) &&
         (!connected || request_failed) && !abort_already_issued;
}

// Live voltage with settled-open relays is contradictory.
inline bool should_latch_idle_outlet_fault(
    bool sequence_running, bool dosing_active, bool relay_a_closed,
    bool relay_b_closed, bool outlet_live, bool relays_settled,
    bool relay_fault_already_latched) {
  return !sequence_running && !dosing_active &&
         !relay_a_closed && !relay_b_closed && outlet_live && relays_settled &&
         !relay_fault_already_latched;
}

inline uint16_t crc16(const uint8_t *data, size_t length) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++)
      crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
  }
  return crc;
}

inline void put16(uint8_t *p, uint16_t value) {
  p[0] = static_cast<uint8_t>(value & 0xFFU);
  p[1] = static_cast<uint8_t>(value >> 8U);
}

inline void put32(uint8_t *p, uint32_t value) {
  for (uint8_t i = 0; i < 4; i++)
    p[i] = static_cast<uint8_t>(value >> (8U * i));
}

inline uint16_t get16(const uint8_t *p) {
  return (uint16_t) p[0] | ((uint16_t) p[1] << 8);
}

inline uint32_t get32(const uint8_t *p) {
  return (uint32_t) p[0] | ((uint32_t) p[1] << 8) |
         ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
}

inline void put64(uint8_t *p, uint64_t value) { put32(p, value); put32(p + 4, value >> 32); }
inline uint64_t get64(const uint8_t *p) { return get32(p) | (uint64_t(get32(p + 4)) << 32); }
inline uint64_t new_session() {
  uint64_t value = (uint64_t(esphome::random_uint32()) << 32) | esphome::random_uint32();
  return value ? value : 1;
}

class Link {
 public:
  explicit Link(bool master = false) : master_(master) {}
  uint64_t local_session{0}, peer_session{0};
  bool peer_changed{false};
  uint8_t transaction_stage{0};
  uint32_t last_transaction{0};
  bool begin_transaction(const Frame &frame) {
    if (frame.command == TEST_CLOSE_B) {
      if (!frame.transaction || (last_transaction && int32_t(frame.transaction - last_transaction) <= 0)) return false;
      last_transaction = transaction = frame.transaction;
      transaction_stage = 1;
      return true;
    }
    if (frame.command == DOSE_B && frame.transaction == transaction && transaction_stage == 2) {
      transaction_stage = 3;
      return true;
    }
    return false;
  }
  void check_idle_output(bool detector_known) {
    if (relay_b_closed || active) relay_open_at_ = millis();
    else if (detector_known && outlet_live && uint32_t(millis() - relay_open_at_) >= 700)
      fault = FAULT_OUTPUT_LIVE_WHEN_OFF;
  }
  void invalidate_delivery() {
    transaction_stage = 0;
    queued_valid_ = false;
    cancel_retries_ = true;
  }

  Frame received{};
  bool frame_ready{false};
  uint32_t last_rx_ms{0};
  uint32_t next_sequence{1};
  uint32_t transaction{0};
  bool relay_b_closed{false};
  bool outlet_live{false};
  bool active{false};
  bool output_proven{false};
  uint8_t fault{FAULT_NONE};
  uint8_t active_command{0};
  uint32_t deadline_ms{0};
  uint32_t last_command_sequence{0};
  uint8_t last_command{0};
  uint32_t last_command_transaction{0};
  uint32_t last_command_duration_ms{0};
  uint16_t last_command_flags{0};
  bool request_failed{false};
  uint8_t last_acked_command{0};
  uint32_t last_acked_transaction{0};
  uint32_t last_ack_ms{0};
  uint32_t ack_count{0};
  // Applied when sending, including commands queued before a lockout change.
  uint16_t request_flags{0};
  uint32_t request_generation{0};
  bool response_matched{false};
  Frame matched_response{};
  uint16_t matched_request_flags{0};
  uint32_t matched_request_generation{0};

  // The Slave transmits only in response, preventing bus contention.
  template<typename Uart> void service_master(Uart *uart) {
    ensure_session_();
    const uint32_t now = millis();
    if (request_waiting_) {
      if ((uint32_t) (now - request_sent_ms_) < RESPONSE_TIMEOUT_MS) return;
      if (cancel_retries_ || request_retries_ >= MAX_REQUEST_RETRIES) {
        request_waiting_ = false;
        request_failed = true;
        // Rediscover through the next status request only. Other commands keep
        // the known Slave session, so an abort still reaches a Slave that did not restart.
        if (awaiting_command_ == STATUS_REQUEST) rediscover_ = true;
      } else {
        uart->write_array(request_bytes_.data(), FRAME_SIZE);
        uart->flush();
        request_sent_ms_ = millis();
        request_retries_++;
        return;
      }
    }

    if (!queued_valid_) return;
    if ((request_failed || !peer_session || rediscover_) &&
        (queued_command_ == TEST_CLOSE_B || queued_command_ == DOSE_B)) {
      // Failed exchanges block later energizing commands.
      queued_valid_ = false;
      return;
    }
    encode_(request_bytes_.data(), queued_command_, next_sequence++,
            queued_transaction_, queued_duration_, request_flags, 0, 0, 0);
    awaiting_sequence_ = get32(request_bytes_.data() + 4);
    awaiting_command_ = queued_command_;
    awaiting_generation_ = request_generation;
    uart->write_array(request_bytes_.data(), FRAME_SIZE);
    uart->flush();
    request_sent_ms_ = millis();
    request_retries_ = 0;
    request_waiting_ = true;
    queued_valid_ = false;
    cancel_retries_ = false;
  }

  void queue_request(uint8_t command, uint32_t transaction_id = 0,
                     uint32_t duration = 0) {
    const uint8_t priority = command == ABORT_B ? 3 :
                             command == STATUS_REQUEST ? 1 : 2;
    if (queued_valid_ && priority < queued_priority_) return;
    queued_valid_ = true;
    queued_priority_ = priority;
    queued_command_ = command;
    queued_transaction_ = transaction_id;
    queued_duration_ = duration;
    // Never retry an energizing command after an abort has been requested.
    if (command == ABORT_B) cancel_retries_ = true;
  }

  template<typename Uart> void send(Uart *uart, uint8_t command,
                                     uint32_t transaction_id = 0,
                                     uint32_t duration = 0,
                                     uint16_t flags = 0,
                                     uint32_t remaining = 0,
                                     uint32_t live = 0,
                                     uint8_t fault_code = 0) {
    ensure_session_();
    uint8_t bytes[FRAME_SIZE]{};
    encode_(bytes, command, next_sequence++, transaction_id, duration, flags,
            remaining, live, fault_code);
    uart->write_array(bytes, FRAME_SIZE);
    uart->flush();
  }

  template<typename Uart> void poll(Uart *uart) {
    ensure_session_();
    response_matched = false;
    while (uart->available()) {
      uint8_t value;
      if (!uart->read_byte(&value)) break;
      if (rx_length_ == 0 && value != SOF_1) continue;
      if (rx_length_ == 1 && value != SOF_2) {
        rx_length_ = value == SOF_1 ? 1 : 0;
        continue;
      }
      rx_[rx_length_++] = value;
      if (rx_length_ != FRAME_SIZE) continue;
      if (rx_[2] == PROTOCOL_VERSION &&
          get16(rx_.data() + 43) == crc16(rx_.data(), 43)) {
        Frame frame;
        frame.command = rx_[3];
        frame.sequence = get32(rx_.data() + 4);
        frame.transaction = get32(rx_.data() + 8);
        frame.duration_ms = get32(rx_.data() + 12);
        frame.flags = get16(rx_.data() + 16);
        frame.remaining_ms = get32(rx_.data() + 18);
        frame.live_ms = get32(rx_.data() + 22);
        frame.fault = rx_[26];
        frame.master_session = get64(rx_.data() + 27);
        frame.slave_session = get64(rx_.data() + 35);
        if (!accept_session_(frame)) { rx_length_ = 0; continue; }
        received = frame;
        frame_ready = true;
        last_rx_ms = millis();
        if (received.command == STATUS_RESPONSE && request_waiting_ &&
            received.duration_ms == awaiting_sequence_ && !peer_changed) {
          response_matched = true;
          matched_response = received;
          matched_request_flags = get16(request_bytes_.data() + 16);
          matched_request_generation = awaiting_generation_;
          request_waiting_ = false;
          request_failed = false;
          if (awaiting_command_ != STATUS_REQUEST) {
            last_acked_command = awaiting_command_;
            last_acked_transaction = received.transaction;
            last_ack_ms = last_rx_ms;
            ack_count++;
          }
          cancel_retries_ = false;
        }
      }
      rx_length_ = 0;
    }
  }

  // An interrupted OTA may leave pre-update requests in the UART buffer.
  // Require a new exchange instead of replaying that buffered authorization.
  template<typename Uart> void discard_input(Uart *uart) {
    uint8_t ignored;
    while (uart->available() && uart->read_byte(&ignored)) {}
    rx_length_ = 0;
    frame_ready = false;
    last_rx_ms = 0;
  }

  bool connected() const {
    return last_rx_ms != 0 && (uint32_t) (millis() - last_rx_ms) <= LINK_TIMEOUT_MS;
  }

  bool fresh_ack(uint8_t command, uint32_t transaction_id,
                 uint32_t previous_ack_count) const {
    return connected() && !peer_changed && ack_count != previous_ack_count && last_acked_command == command &&
           last_acked_transaction == transaction_id && !request_failed;
  }

  uint32_t remaining() const {
    if (!active || (int32_t) (deadline_ms - millis()) <= 0) return 0;
    return deadline_ms - millis();
  }

  bool is_duplicate_command(const Frame &frame) const {
    return frame.sequence == last_command_sequence &&
           frame.command == last_command &&
           frame.transaction == last_command_transaction &&
           frame.duration_ms == last_command_duration_ms &&
           frame.flags == last_command_flags;
  }

  void remember_command(const Frame &frame) {
    last_command_sequence = frame.sequence;
    last_command = frame.command;
    last_command_transaction = frame.transaction;
    last_command_duration_ms = frame.duration_ms;
    last_command_flags = frame.flags;
  }

  void observe_outlet(bool live) {
    if (live == outlet_live) return;
    outlet_live = live;
    if (live) output_proven = true;
  }

 private:
  void encode_(uint8_t *bytes, uint8_t command, uint32_t sequence,
                      uint32_t transaction_id, uint32_t duration,
                      uint16_t flags, uint32_t remaining, uint32_t live,
                      uint8_t fault_code) {
    bytes[0] = SOF_1;
    bytes[1] = SOF_2;
    bytes[2] = PROTOCOL_VERSION;
    bytes[3] = command;
    put32(bytes + 4, sequence);
    put32(bytes + 8, transaction_id);
    put32(bytes + 12, duration);
    put16(bytes + 16, flags);
    put32(bytes + 18, remaining);
    put32(bytes + 22, live);
    bytes[26] = fault_code;
    put64(bytes + 27, master_ ? local_session : peer_session);
    put64(bytes + 35, !master_ ? local_session :
                      command == STATUS_REQUEST && rediscover_ ? 0 : peer_session);
    put16(bytes + 43, crc16(bytes, 43));
  }

  void ensure_session_() { if (!local_session) local_session = new_session(); }
  bool accept_session_(const Frame &frame) {
    if (master_) {
      if (frame.command != STATUS_RESPONSE || frame.master_session != local_session ||
          !frame.slave_session || !request_waiting_ || frame.duration_ms != awaiting_sequence_) return false;
      if (peer_session && peer_session != frame.slave_session) {
        peer_changed = true;
        invalidate_delivery();
        request_waiting_ = false;
        request_failed = true;
      }
      peer_session = frame.slave_session;
      rediscover_ = false;
      return true;
    }
    if (!frame.master_session || frame.command == STATUS_RESPONSE) return false;
    // Discovery never energizes. A changed peer gets a new unpredictable lease:
    // even a delayed old discovery cannot authorize an old energizing command.
    if (frame.command == STATUS_REQUEST && frame.slave_session == 0) {
      if (peer_session != frame.master_session) {
        peer_changed = peer_session != 0;
        local_session = new_session();
        peer_session = frame.master_session;
        have_sequence_ = false;
        last_command_sequence = 0;
        last_transaction = transaction = 0;
        invalidate_delivery();
      }
    } else if (frame.master_session != peer_session || frame.slave_session != local_session) return false;
    if (have_sequence_ && int32_t(frame.sequence - received_sequence_) <= 0) {
      // Accept only an identical retry of the latest received frame, including
      // status requests. The Slave separately deduplicates command execution.
      return frame.sequence == received_sequence_ &&
             frame.command == received.command &&
             frame.master_session == received.master_session &&
             frame.slave_session == received.slave_session &&
             frame.transaction == received.transaction &&
             frame.duration_ms == received.duration_ms &&
             frame.flags == received.flags &&
             frame.remaining_ms == received.remaining_ms &&
             frame.live_ms == received.live_ms && frame.fault == received.fault;
    }
    have_sequence_ = true;
    received_sequence_ = frame.sequence;
    return true;
  }
  bool master_{false}, have_sequence_{false}, rediscover_{false};
  uint32_t received_sequence_{0};
  uint32_t relay_open_at_{0};
  std::array<uint8_t, FRAME_SIZE> rx_{};
  size_t rx_length_{0};
  std::array<uint8_t, FRAME_SIZE> request_bytes_{};
  bool request_waiting_{false};
  bool cancel_retries_{false};
  uint32_t awaiting_sequence_{0};
  uint8_t awaiting_command_{0};
  uint32_t awaiting_generation_{0};
  uint32_t request_sent_ms_{0};
  uint8_t request_retries_{0};
  bool queued_valid_{false};
  uint8_t queued_priority_{0};
  uint8_t queued_command_{0};
  uint32_t queued_transaction_{0};
  uint32_t queued_duration_{0};
};

class MasterMaintenance {
 public:
  void request(bool locked) {
    if (requested_ != locked) {
      requested_ = locked;
      generation_++;
    }
    if (locked) locked_ = true;
  }

  bool locked() const { return locked_; }
  bool requested() const { return requested_; }
  uint32_t generation() const { return generation_; }

  uint16_t flags(bool outputs_safe) const {
    if (!requested_) return 0;
    return MAINTENANCE_LOCKED | (outputs_safe ? ALLOW_MAINTENANCE_WIFI : 0);
  }

  bool release(const Link &link, bool outputs_safe) {
    if (!locked_ || requested_ || !outputs_safe || !link.response_matched ||
        link.matched_request_generation != generation_ ||
        (link.matched_request_flags & MAINTENANCE_LOCKED)) return false;
    const auto &frame = link.matched_response;
    const uint16_t unsafe = MAINTENANCE_LOCKED | B_WIFI_ENABLED |
                           RELAY_B_CLOSED | B_ACTIVE | OUTLET_LIVE;
    if (!(frame.flags & B_DOSING_READY) || (frame.flags & unsafe)) return false;
    locked_ = false;
    return true;
  }

 private:
  // Even a restored OFF must obtain a fresh Wi-Fi-off acknowledgment at boot.
  bool requested_{true};
  bool locked_{true};
  uint32_t generation_{1};
};

class SlaveMaintenance {
 public:
  void observe(const Frame &frame) {
    if (ota_active_ || frame.command == STATUS_RESPONSE ||
        frame.command < STATUS_REQUEST || frame.command > PROBE_STATUS) return;
    known_ = true;
    last_authorization_ms_ = millis();
    locked_ = frame.flags & MAINTENANCE_LOCKED;
    wifi_allowed_ = locked_ && (frame.flags & ALLOW_MAINTENANCE_WIFI);
  }

  void expire() {
    if (!ota_active_ && known_ &&
        (uint32_t) (millis() - last_authorization_ms_) > LINK_TIMEOUT_MS)
      invalidate();
  }

  bool blocks_dosing() const { return !known_ || locked_ || ota_active_; }
  bool wants_wifi(bool outputs_safe) const {
    return known_ && locked_ && wifi_allowed_ && outputs_safe;
  }

  void begin_ota() { ota_active_ = true; locked_ = true; }
  void fail_ota() { ota_active_ = false; invalidate(); }

 private:
  void invalidate() { known_ = false; locked_ = true; wifi_allowed_ = false; }
  bool known_{false};
  bool locked_{true};
  bool wifi_allowed_{false};
  bool ota_active_{false};
  uint32_t last_authorization_ms_{0};
};

inline Link master_link{true};
inline Link slave_link;
inline MasterMaintenance master_maintenance;
inline SlaveMaintenance slave_maintenance;

}  // namespace pool_doser
