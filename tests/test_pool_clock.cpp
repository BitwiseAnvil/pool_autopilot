#include "../components/pool_clock/clock.h"
#include <cassert>
#include <iostream>

int main() {
  constexpr int64_t epoch=1790000000000LL;
  pool_clock_core::Clock clock;
  assert(!clock.observe(0,epoch).healthy); // a plausible date is not a successful sync
  auto view=clock.observe(1000,epoch+1000,true);
  assert(view.healthy && view.synchronized);
  const auto generation=view.generation;
  assert(clock.observe(1799999,epoch+1799999).healthy);
  assert(!clock.observe(1801000,epoch+1801000).healthy);
  assert(clock.observe(1802000,epoch+1802000,true).healthy);
  view=clock.observe(1803000,epoch-60000,true); // backward correction, new observation epoch
  assert(view.healthy && view.generation>generation);
  const auto corrected=view.generation;
  assert(!clock.observe(1804000,epoch+500000).healthy); // external, unsynchronized clock step
  assert(clock.observe(1805000,epoch+500001,true).generation>corrected);
  for (uint64_t days : {30ULL,50ULL,100ULL}) {
    const uint64_t now=days*86400000ULL;
    assert(!clock.observe(now,epoch+now).healthy);
    assert(clock.observe(now+1,epoch+now+1,true).healthy);
  }
  assert(pool_clock_core::fresh(epoch,epoch-19999));
  assert(!pool_clock_core::fresh(epoch,epoch-20000));
  assert(pool_clock_core::fresh(epoch,epoch+2000));
  assert(!pool_clock_core::fresh(epoch,epoch+2001));
  assert(!pool_clock_core::fresh(epoch,0));
  std::cout << "NIST clock health, 30/50/100-day outages, corrections and freshness passed.\n";
}
