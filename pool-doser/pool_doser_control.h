#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace pool_doser {

constexpr uint32_t ACID_AVERAGE_MS = 120000;
constexpr uint32_t ACID_FLOW_MS = 300000;
constexpr uint32_t ACID_MIX_MS = 300000;
constexpr uint32_t ACID_SAMPLE_FRESH_MS = 20000;
constexpr uint32_t ACID_HEARTBEAT_MS = 15000;
constexpr uint32_t ACID_TOKEN_MS = 10000;
constexpr float ACID_PULSE_OZ = 2.0f;

enum class DoseResult : uint8_t { NONE, ACCEPTED, COMPLETED, INTERRUPTED };

// Per-request state only. HA owns the Auto switch and the pH dosing decision.
struct AutomaticState {
  uint8_t automatic{0};
  DoseResult result{DoseResult::NONE};
  char request_id[64]{}, reason[80]{};
  float requested_oz{0};
  uint64_t accepted_sequence{0};
};
inline AutomaticState automatic_state;

// Elapsed countdown; no saved time or accounting history.
class RestTimer {
 public:
  void begin(uint32_t now) { remaining_ = 0; at_ = now; }
  void tick(uint32_t now) {
    const uint32_t elapsed = now - at_;
    at_ = now;
    remaining_ = elapsed >= remaining_ ? 0 : remaining_ - elapsed;
  }
  void start(uint32_t now, uint32_t duration) { remaining_ = duration; at_ = now; }
  uint32_t remaining(uint32_t now) { tick(now); return remaining_; }
 private:
  uint32_t remaining_{0}, at_{0};
};
inline RestTimer rest_timer;

inline const char *dose_result_name(DoseResult result) {
  switch (result) {
    case DoseResult::ACCEPTED: return "accepted";
    case DoseResult::COMPLETED: return "completed";
    case DoseResult::INTERRUPTED: return "interrupted";
    default: return "none";
  }
}

inline void set_request_reason(AutomaticState &s, const std::string &reason) {
  std::snprintf(s.reason, sizeof(s.reason), "%s", reason.c_str());
}

// No relay operations or autonomous deliveries. HA must query status and use
// that query's short-lived authorization for every single bounded request.
class AcidControl {
 public:
  void begin(const std::string &boot, uint32_t now, AutomaticState &s) {
    *this = AcidControl{};
    boot_ = boot;
    last_tick_ = now;
    mix_at_ = now;
    s = AutomaticState{};
  }

  void tick(uint32_t now, bool flow) {
    const uint32_t elapsed = now - last_tick_;
    last_tick_ = now;
    const uint32_t mix_elapsed = now - mix_at_;
    mix_ms_ = mix_elapsed >= mix_ms_ ? 0 : mix_ms_ - mix_elapsed;
    mix_at_ = now;
    if (!flow) flow_ms_ = 0;
    else if (was_flow_)
      flow_ms_ += elapsed >= ACID_FLOW_MS - flow_ms_ ? ACID_FLOW_MS - flow_ms_ : elapsed;
    was_flow_ = flow;
    if (heartbeat_ && now - heartbeat_at_ >= ACID_HEARTBEAT_MS) pause();
    if (count_ && (now - samples_[count_ - 1].at >= ACID_SAMPLE_FRESH_MS ||
                  !sample_fresh_(now, last_sequence_))) clear_samples_();
    if (!token_.empty() && now - issued_at_ >= ACID_TOKEN_MS) token_.clear();
  }

  void pause() {
    heartbeat_ = false;
    clear_samples_();
  }

  void set_clock(uint32_t now, int64_t utc_ms, bool healthy, uint32_t generation) {
    if (!healthy || generation != clock_generation_) clear_samples_();
    clock_generation_ = generation;
    clock_healthy_ = healthy;
    clock_at_ = now;
    utc_ms_ = utc_ms;
  }

  void query(uint32_t now, const std::string &session) {
    // Also safe when called before the regular tick on a delayed main loop.
    if (session.empty() || session != client_session_ ||
        (heartbeat_ && now - heartbeat_at_ >= ACID_HEARTBEAT_MS)) pause();
    client_session_ = session;
    if (session.empty()) return;
    heartbeat_ = true;
    heartbeat_at_ = now;
  }

  void observe(uint32_t now, bool valid, const std::string &boot,
               const std::string &sample_id, float ph) {
    int64_t sequence = 0;
    if (!valid || !heartbeat_ || now - heartbeat_at_ >= ACID_HEARTBEAT_MS ||
        boot.empty() || boot.size() > 64 || !parse_sample_(sample_id, sequence) ||
        !sample_fresh_(now, sequence) ||
        !std::isfinite(ph) || ph < 0 || ph > 14) {
      clear_samples_();
      return;
    }
    if (boot != atlas_boot_) {
      clear_samples_();
      atlas_boot_ = boot;
      have_sequence_ = false;
    }
    // Expire ordering with observation continuity, including calendar changes.
    if (count_ && (now - samples_[count_ - 1].at >= ACID_SAMPLE_FRESH_MS ||
                   !sample_fresh_(now, last_sequence_))) clear_samples_();
    // Real Unix milliseconds: no half-range or uptime rollover comparison.
    if (have_sequence_ && sequence <= last_sequence_) return;
    // Atlas samples much more slowly than 1 Hz. Bound memory and prevent a
    // burst of messages from manufacturing two minutes of observation.
    if (count_ && now - samples_[count_ - 1].at < 1000) return;
    have_sequence_ = true;
    last_sequence_ = sequence;
    while (count_ > 1 && now - samples_[1].at >= ACID_AVERAGE_MS) {
      for (size_t i = 1; i < count_; ++i) samples_[i - 1] = samples_[i];
      --count_;
    }
    if (count_ == samples_.size()) { clear_samples_(); }
    samples_[count_++] = {now, ph};
  }

  float average(uint32_t now) const {
    if (!heartbeat_ || now - heartbeat_at_ >= ACID_HEARTBEAT_MS || !count_ ||
        !sample_fresh_(now, last_sequence_) ||
        now - samples_[count_ - 1].at >= ACID_SAMPLE_FRESH_MS ||
        now - samples_[0].at < ACID_AVERAGE_MS) return NAN;
    double weighted = 0;
    for (size_t i = 0; i < count_; ++i) {
      const uint32_t start_age = std::min(now - samples_[i].at, ACID_AVERAGE_MS);
      const uint32_t end_age = i + 1 < count_ ? now - samples_[i + 1].at : 0;
      if (end_age < start_age) weighted += static_cast<double>(samples_[i].ph) * (start_age - end_age);
    }
    return static_cast<float>(weighted / ACID_AVERAGE_MS);
  }

  std::string reason(uint32_t now, const AutomaticState &s) const {
    if (s.result == DoseResult::ACCEPTED) return "request_pending";
    if (!clock_healthy_) return "clock_unavailable";
    if (!heartbeat_ || now - heartbeat_at_ >= ACID_HEARTBEAT_MS) return "ha_unavailable";
    if (!std::isfinite(average(now))) return "fresh_ph_window";
    if (flow_ms_ < ACID_FLOW_MS) return "continuous_flow";
    if (mix_ms_) return "post_dose_wait";
    return "ready";
  }

  std::string issue(uint32_t now, bool ready) {
    token_.clear();
    if (ready) { token_ = next_id(); issued_at_ = now; }
    return token_;
  }

  bool consume(uint32_t now, const std::string &token) {
    const bool valid = !token.empty() && token == token_ && now - issued_at_ < ACID_TOKEN_MS;
    // A stale/duplicate request must not consume somebody else's new token.
    if (valid) token_.clear();
    return valid;
  }

  const std::string &boot() const { return boot_; }
  std::string next_id() { return boot_ + "-" + std::to_string(++serial_); }
  void invalidate_token() { token_.clear(); }
  uint32_t flow_remaining_ms() const { return ACID_FLOW_MS - flow_ms_; }
  uint32_t mix_remaining_ms() const { return mix_ms_; }

  void accept(AutomaticState &s, const std::string &request, float ounces, bool automatic) {
    invalidate_token();
    s.result = DoseResult::ACCEPTED;
    s.automatic = automatic;
    std::snprintf(s.request_id, sizeof(s.request_id), "%s", request.c_str());
    set_request_reason(s, "accepted");
    s.requested_oz = ounces;
    ++s.accepted_sequence;
  }

  void finish(uint32_t now, AutomaticState &s, bool completed, const std::string &reason) {
    if (s.result != DoseResult::ACCEPTED) return;
    s.result = completed ? DoseResult::COMPLETED : DoseResult::INTERRUPTED;
    set_request_reason(s, completed ? "completed" : reason);
    mix_ms_ = ACID_MIX_MS;
    mix_at_ = now;
    invalidate_token();
  }

 private:
  static bool parse_sample_(const std::string &value, int64_t &out) {
    if (value.size() != 13) return false;
    uint64_t parsed = 0;
    for (char c : value) {
      if (c < '0' || c > '9') return false;
      parsed = parsed * 10 + static_cast<unsigned>(c - '0');
    }
    if (parsed < 1577836800000ULL || parsed >= 4102444800000ULL) return false;
    out = static_cast<int64_t>(parsed);
    return true;
  }
  bool sample_fresh_(uint32_t now, int64_t measured) const {
    const int64_t age = utc_ms_ + static_cast<uint32_t>(now - clock_at_) - measured;
    return clock_healthy_ && age >= -2000 && age < ACID_SAMPLE_FRESH_MS;
  }
  void clear_samples_() { count_ = 0; have_sequence_ = false; invalidate_token(); }
  struct Sample { uint32_t at{0}; float ph{0}; };
  std::array<Sample, 122> samples_{};
  size_t count_{0};
  std::string boot_, atlas_boot_, token_, client_session_;
  uint32_t last_tick_{0}, heartbeat_at_{0}, issued_at_{0}, clock_at_{0}, clock_generation_{0};
  int64_t last_sequence_{0}, utc_ms_{0};
  uint64_t serial_{0};
  uint32_t flow_ms_{0}, mix_ms_{ACID_MIX_MS}, mix_at_{0};
  bool heartbeat_{false}, was_flow_{false}, have_sequence_{false}, clock_healthy_{false};
};

inline AcidControl acid_control;

}  // namespace pool_doser
