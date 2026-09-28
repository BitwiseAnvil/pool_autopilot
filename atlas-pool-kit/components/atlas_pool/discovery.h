#pragma once
#include "core.h"
#include "controls.h"

namespace atlas_pool {
// A full QoS 1 outbox can reject any item, including the final retained
// availability. Resume at that item so retries do not refill it with the prefix.
class DiscoverySequence {
 public:
  static constexpr uint8_t READING_COUNT=6, DIAGNOSTIC_COUNT=20;
  static constexpr uint8_t MAINTENANCE=READING_COUNT+DIAGNOSTIC_COUNT;
  static constexpr uint8_t CONTROLS=MAINTENANCE+1;
  static constexpr uint8_t AVAILABILITY=CONTROLS+CONTROL_COUNT, COUNT=AVAILABILITY+1;
  static constexpr uint32_t RETRY_MS=1000;

  void request() { next_=0; active_=true; waiting_=false; }

  // Return true only when this sequence finishes. Sample the clock after a
  // failed, possibly blocking publish so the retry delay starts at failure.
  template<typename Publisher,typename Clock> bool poll(Publisher publish,Clock clock) {
    if (!active_ || (waiting_ && !due(clock(),retry_at_))) return false;
    waiting_=false;
    while (next_<COUNT) {
      if (!publish(next_)) {
        retry_at_=clock()+RETRY_MS; waiting_=true;
        return false;
      }
      ++next_;
    }
    active_=false;
    return true;
  }

 private:
  uint8_t next_{0};
  bool active_{false}, waiting_{false};
  uint32_t retry_at_{0};
};
} // namespace atlas_pool
