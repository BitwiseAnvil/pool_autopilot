#pragma once
#include <cstdint>

namespace pool_clock_core {
constexpr int64_t MIN_UTC_MS = 1577836800000LL;
constexpr int64_t MAX_UTC_MS = 4102444800000LL;
constexpr uint64_t SYNC_MAX_AGE_MS = 1800000;
constexpr int64_t CLOCK_TOLERANCE_MS = 2000;
constexpr int64_t SAMPLE_MAX_AGE_MS = 20000;

inline bool valid_utc(int64_t value) { return value >= MIN_UTC_MS && value < MAX_UTC_MS; }
inline bool fresh(int64_t now, int64_t measured) {
  return valid_utc(now) && valid_utc(measured) &&
    measured <= now + CLOCK_TOLERANCE_MS && now - measured < SAMPLE_MAX_AGE_MS;
}

struct View {
  int64_t utc_ms{0};
  uint64_t sync_age_ms{0};
  uint32_t generation{0};
  bool synchronized{false};
  bool healthy{false};
};

// Calendar time is never used to measure relay runtimes or allowance expiry.
// The 64-bit monotonic clock also prevents sync age from wrapping after 49 days.
class Clock {
 public:
  View observe(uint64_t monotonic_ms, int64_t utc_ms, bool synchronization = false) {
    const bool jumped = observed_ &&
      (monotonic_ms < last_mono_ ||
       utc_ms - last_utc_ - static_cast<int64_t>(monotonic_ms - last_mono_) > CLOCK_TOLERANCE_MS ||
       utc_ms - last_utc_ - static_cast<int64_t>(monotonic_ms - last_mono_) < -CLOCK_TOLERANCE_MS);
    if (jumped || !valid_utc(utc_ms)) trusted_ = false;
    if (synchronization && valid_utc(utc_ms)) {
      synchronized_ = trusted_ = true;
      sync_at_ = monotonic_ms;
    }
    const uint64_t age = synchronized_ ? monotonic_ms - sync_at_ : 0;
    const bool healthy = trusted_ && age < SYNC_MAX_AGE_MS;
    if (jumped || healthy != healthy_) ++generation_;
    observed_ = true;
    last_mono_ = monotonic_ms;
    last_utc_ = utc_ms;
    healthy_ = healthy;
    return {utc_ms, age, generation_, synchronized_, healthy};
  }
 private:
  uint64_t last_mono_{0}, sync_at_{0};
  int64_t last_utc_{0};
  uint32_t generation_{0};
  bool observed_{false}, synchronized_{false}, trusted_{false}, healthy_{false};
};
}  // namespace pool_clock_core
