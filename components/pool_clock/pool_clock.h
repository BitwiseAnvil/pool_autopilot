#pragma once
#include "clock.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/components/sntp/sntp_component.h"
#include <esp_timer.h>
#include <esp_sntp.h>
#include <sys/time.h>

namespace esphome::pool_clock {
class PoolClock : public Component {
 public:
  void set_time(sntp::SNTPComponent *source) {
    source->add_on_time_sync_callback([this]() { this->sample_(true); });
  }
  ::pool_clock_core::View sample() { return sample_(false); }
  void loop() override { sample(); }
  void resync() {
    const uint64_t now = esp_timer_get_time() / 1000;
    if (!restart_requested_ || now - restart_at_ >= 15000) {
      restart_at_ = now;
      restart_requested_ = true;
      esp_sntp_restart();
    }
  }
 protected:
  ::pool_clock_core::View sample_(bool synchronization) {
    // Serialize reads too: Atlas's MQTT task and main loop share this clock.
    // gettimeofday takes libc locks; use a task mutex, not a critical section.
    LockGuard lock(mutex_);
    timeval tv{};
    gettimeofday(&tv, nullptr);
    const auto result = clock_.observe(esp_timer_get_time() / 1000,
      static_cast<int64_t>(tv.tv_sec) * 1000 + tv.tv_usec / 1000, synchronization);
    return result;
  }
  ::pool_clock_core::Clock clock_;
  Mutex mutex_;
  uint64_t restart_at_{0};
  bool restart_requested_{false};
};
}  // namespace esphome::pool_clock
