#include "pool_doser_rs485.h"
#include <cassert>
#include <cmath>
#include <functional>
#include <iostream>
#include <vector>

uint64_t simulation_ms = 10000;
uint32_t millis() { return uint32_t(simulation_ms); }
struct Uart {
  std::vector<uint8_t> rx, tx;
  size_t cursor{0};
  size_t available() const { return rx.size() - cursor; }
  bool read_byte(uint8_t *v) { if (!available()) return false; *v=rx[cursor++]; return true; }
  void write_array(const uint8_t *data, size_t n) { tx.insert(tx.end(), data, data+n); }
  void flush() {}
};
struct Switch {
  bool state{false}, known{true};
  unsigned on_calls{0};
  std::function<void()> off_action;
  bool has_state() const { return known; }
  void publish_state(bool value) { state=value; }
  void turn_on() { state=true; ++on_calls; }
  void turn_off() { state=false; if (off_action) off_action(); }
};
struct Number { float state{0}; void publish_state(float value) { state=value; } };
struct Text { std::string state; void publish_state(const std::string &value) { state=value; } };
struct Wifi {
  bool disabled{true};
  bool is_disabled() const { return disabled; }
  bool is_connected() const { return !disabled; }
  int32_t wifi_rssi() const { return -67; }
  void enable() { disabled=false; }
  void disable() { disabled=true; }
};
struct Clock {
  bool healthy{true};
  uint32_t generation{1};
  int64_t offset{0};
  struct Sample { int64_t utc_ms; bool healthy; uint32_t generation; };
  Sample sample() const { return {1790000000000LL + int64_t(simulation_ms) + offset, healthy, generation}; }
};
struct Script {
  bool running{false};
  int stage{0};
  uint32_t entered{0};
  uint32_t delay_ms{0};
  bool delay_loaded{false};
  virtual ~Script() = default;
  virtual void tick() = 0;
  bool is_running() const { return running; }
  void stop() { running=false; }
  void jump_to(int next) { stage=next; entered=millis(); delay_loaded=false; }
  bool delay_ready(uint32_t duration) {
    if (!delay_loaded) { delay_ms=duration; delay_loaded=true; }
    return uint32_t(millis()-entered)>=delay_ms;
  }
};
bool trace_interlocks(bool settings, bool connected, bool failed, uint8_t fault,
                      bool known, bool flow, bool maintenance, bool relay_fault,
                      bool transaction, bool a, bool active, bool b, bool live) {
  const bool ok=pool_doser::delivery_start_interlocks_passed(settings,connected,failed,fault,
    known,flow,maintenance,relay_fault,transaction,a,active,b,live);
  if (!ok) std::cerr << "start gates: " << settings << connected << failed << int(fault)
    << known << flow << maintenance << relay_fault << transaction << a << active << b << live << "\n";
  return ok;
}
#include "runtime_automation.h"

bool to_master=true, to_slave=true, master_power=true;
bool welded_a=false, welded_b=false, detector_dead=false, detector_live=false;
unsigned status_replies_to_drop=0, status_replies_dropped=0;
std::vector<uint8_t> previous_dose;
uint32_t observed_delivery_ms=0;

void transfer(Uart &from, Uart &to, bool enabled, bool requests=false) {
  for (size_t i=0;i<from.tx.size();i+=pool_doser::FRAME_SIZE) {
    if (requests && from.tx[i+3]==pool_doser::DOSE_B)
      previous_dose.assign(from.tx.begin()+i, from.tx.begin()+i+pool_doser::FRAME_SIZE);
    if (!requests && status_replies_to_drop &&
        from.tx[i+3]==pool_doser::STATUS_RESPONSE &&
        pool_doser::slave_link.received.command==pool_doser::STATUS_REQUEST) {
      --status_replies_to_drop;
      ++status_replies_dropped;
      continue;
    }
    if (enabled) to.rx.insert(to.rx.end(), from.tx.begin()+i, from.tx.begin()+i+pool_doser::FRAME_SIZE);
  }
  from.tx.clear();
  if (from.cursor==from.rx.size()) { from.rx.clear(); from.cursor=0; }
}
void advance(uint32_t ms) {
  for (uint32_t elapsed=0;elapsed<ms;elapsed+=10) {
    simulation_ms+=10;
    if (master_power) {
      if (simulation_ms%250==0) master::heartbeat();
      if (simulation_ms%50==0) master::receive();
      if (simulation_ms%1000==0) {
        pool_doser::rest_timer.tick(millis());
        master::refresh_acid_status.execute();
      }
      master::scripts_tick();
    }
    transfer(master::uart, slave::uart, to_slave && master_power, true);
    slave::outlet_live.state = detector_live || (!detector_dead &&
      (welded_a || master::relay_a.state) && (welded_b || slave::relay_b.state));
    if (simulation_ms%20==0) slave::tick();
    if (slave::outlet_live.state && pool_doser::slave_link.active_command==pool_doser::DOSE_B) observed_delivery_ms+=10;
    transfer(slave::uart, master::uart, to_master && master_power);
    assert(slave::slave_wifi.is_disabled() || !slave::relay_b.state);
  }
}
void restart_master() {
  master::relay_a.state=false;
  master::reset_ram(); master::uart={};
  pool_doser::master_link=pool_doser::Link(true);
  pool_doser::master_maintenance={};
  pool_doser::automatic_state={};
  pool_doser::settings_store={};
  master::boot();
  master::relay_b.off_action=master::relay_b_off;
  master_power=true;
}
void reset() {
  simulation_ms=10000;
  observed_delivery_ms=0;
  to_master=to_slave=master_power=true;
  welded_a=welded_b=detector_dead=detector_live=false;
  status_replies_to_drop=status_replies_dropped=0;
  master::relay_a={}; slave::relay_b={}; slave::outlet_live={}; slave::slave_wifi={};
  pool_doser::slave_link=pool_doser::Link(); pool_doser::slave_maintenance={};
  slave::uart={}; master::nist_clock={}; master::flow.state=true;
  pool_storage::data.clear(); pool_storage::saves=0; pool_storage::fail=false;
  pool_doser::Settings s; s.pump_gpd=11.25f; s.maintenance=0; s.pre_run_s=0; s.min_rest_s=0;
  pool_doser::SettingsStore store; assert(store.save(s));
  restart_master();
  advance(2000);
  assert(master::settings_ok && !master::maintenance_lockout.state);
  assert(pool_storage::saves==1);
}
void request(float ounces=0.5f) {
  master::dose.execute(ounces, "");
  assert(pool_doser::automatic_state.result==pool_doser::DoseResult::ACCEPTED);
}
void reach_delivery() {
  for (unsigned n=0;n<1000 && !pool_doser::slave_link.active;n++) advance(10);
  for (unsigned n=0;n<1000 && pool_doser::slave_link.active_command!=pool_doser::DOSE_B;n++) advance(10);
  assert(pool_doser::slave_link.active_command==pool_doser::DOSE_B);
  advance(500);
  assert(master::relay_a.state && slave::relay_b.state);
}
void assert_stopped() {
  assert(!master::relay_a.state && !slave::relay_b.state && !master::run_sequence.running);
}
void complete_pulse() {
  observed_delivery_ms=0;
  request(); advance(40000);
  const auto &s=pool_doser::automatic_state;
  if (s.result!=pool_doser::DoseResult::COMPLETED)
    std::cerr << "pulse failed: " << master::blocked_reason << " / " << s.reason << "\n";
  assert(s.result==pool_doser::DoseResult::COMPLETED);
  assert(observed_delivery_ms>=29000 && observed_delivery_ms<=31000);
  assert_stopped();
}

void delivery_and_settings() {
  reset(); complete_pulse();
  assert(pool_storage::saves==1);
  for (int n=0;n<4;++n) master::set_calibration_factor(1);
  assert(pool_storage::saves==1);
  master::set_min_rest_s(1800);
  assert(pool_storage::saves==2);
  pool_doser::rest_timer.start(millis(), 1800000);
  restart_master(); advance(2000);
  assert(pool_doser::automatic_state.accepted_sequence==0);
  assert(pool_doser::rest_timer.remaining(millis())==0 && pool_storage::saves==2);
  request(); reach_delivery();
  const auto writes=pool_storage::saves;
  const auto token=pool_doser::acid_control.issue(millis(), true);
  master::operator_stop.execute();
  assert(!master::relay_a.state && pool_storage::saves==writes);
  assert(!pool_doser::acid_control.consume(millis(), token));
  advance(2000); assert_stopped();
  master::operator_stop.execute(); advance(2000);
  assert(pool_storage::saves==writes);
  restart_master(); advance(2000);
  assert(pool_doser::automatic_state.result==pool_doser::DoseResult::NONE && pool_storage::saves==writes);
  master::maintenance_lockout_on(); advance(2000);
  const auto locked_writes=pool_storage::saves;
  master::maintenance_lockout_on(); restart_master(); advance(2000);
  assert(master::maintenance_lockout.state && pool_storage::saves==locked_writes);
}
void ordinary_interruptions() {
  for (int failure=0;failure<4;++failure) {
    reset(); request(); reach_delivery();
    const auto old_frame=previous_dose;
    if (failure==0) { master_power=false; master::relay_a.state=false; }
    if (failure==1) { to_master=to_slave=false; }
    if (failure==2) { pool_doser::slave_link=pool_doser::Link(); pool_doser::slave_maintenance={}; slave::relay_b.state=false; slave::uart={}; }
    if (failure==3) { master::flow.state=false; master::blocked_reason="aborted_flow_lost"; master::stop_dose.execute(); }
    advance(3000);
    assert(!slave::relay_b.state);
    if (failure==0) restart_master();
    to_master=to_slave=true; master::flow.state=true;
    advance(4000);
    if (master::relay_fault) std::cerr << "interruption latched " << failure << " " << master::blocked_reason << "\n";
    assert(!master::relay_fault && !pool_doser::physical_fault(pool_doser::slave_link.fault));
    assert_stopped();
    const auto relay_calls=slave::relay_b.on_calls;
    slave::uart.rx.insert(slave::uart.rx.end(), old_frame.begin(), old_frame.end());
    advance(1000);
    assert(slave::relay_b.on_calls==relay_calls && pool_storage::saves==1);
    complete_pulse();
  }
}
void lost_status_replies_during_delivery() {
  for (unsigned lost: {1U,2U}) {
    reset(); request(); reach_delivery();
    const auto deadline=pool_doser::slave_link.deadline_ms;
    const auto relay_calls=slave::relay_b.on_calls;
    status_replies_to_drop=lost;
    advance(1000);
    assert(status_replies_to_drop==0 && status_replies_dropped==lost);
    assert(master::relay_a.state && slave::relay_b.state);
    assert(!master::relay_fault && !pool_doser::master_link.request_failed);
    assert(pool_doser::slave_link.deadline_ms==deadline && slave::relay_b.on_calls==relay_calls);
    // A delayed energizing frame remains stale after the successful status retry.
    slave::uart.rx.insert(slave::uart.rx.end(), previous_dose.begin(), previous_dose.end());
    advance(1000);
    assert(pool_doser::slave_link.deadline_ms==deadline && slave::relay_b.on_calls==relay_calls);
    advance(40000);
    const auto &s=pool_doser::automatic_state;
    assert(s.result==pool_doser::DoseResult::COMPLETED && s.accepted_sequence==1);
    assert(observed_delivery_ms>=29000 && observed_delivery_ms<=31000);
    assert_stopped();
    assert(pool_storage::saves==1);
  }
}
void physical_protection() {
  for (int fault=0;fault<4;++fault) {
    reset();
    welded_a=fault==0; welded_b=fault==1; detector_dead=fault==2; detector_live=fault==3;
    master::dose.execute(0.5, "");
    advance(12000);
    assert_stopped();
    assert(master::relay_fault && pool_storage::saves==1);
    to_master=to_slave=false; advance(3000); to_master=to_slave=true; advance(3000);
    assert(master::relay_fault);  // reconnect never clears confirmed physical failure
    welded_a=welded_b=detector_dead=detector_live=false;
    master::maintenance_lockout_on(); advance(2000);
    master::clear_relay_fault.execute(); advance(3000);
    assert(!master::relay_fault && master::maintenance_lockout.state);
  }
  reset(); request(); reach_delivery();
  welded_b=true; advance(40000);
  assert_stopped();
  assert(master::relay_fault && pool_storage::saves==1);
  welded_b=false; to_master=to_slave=false; advance(3000);
  to_master=to_slave=true; advance(3000);
  assert(master::relay_fault);  // failed physical shutdown survives reconnection

  reset(); request(); reach_delivery();
  master::runtime_guard.entered = millis() - 902000U;
  advance(2000); assert_stopped();
  assert(std::string(pool_doser::automatic_state.reason)=="watchdog_timeout");
}
void save_failure_and_pre_run() {
  reset(); pool_storage::fail=true;
  master::set_calibration_factor(1.25f);
  assert(!master::settings_ok && master::blocked_reason=="configuration_save_failed");
  const auto attempted=pool_storage::saves;
  advance(1000000); assert(pool_storage::saves==attempted);
  master::dose.execute(0.5, ""); advance(1000); assert_stopped();
  pool_storage::fail=false; master::set_calibration_factor(1.25f); assert(master::settings_ok);
  master::set_min_rest_s(1800);
  master::set_pre_run_s(60); request(); advance(1000); master::stop_dose.execute(); advance(2000);
  assert(pool_doser::automatic_state.result==pool_doser::DoseResult::INTERRUPTED && observed_delivery_ms==0);
  // Nothing was delivered, so a pre-run stop starts no minimum rest.
  assert(pool_doser::rest_timer.remaining(millis())==0);
  master::set_pre_run_s(0); request(); advance(60000);
  assert(pool_doser::automatic_state.result==pool_doser::DoseResult::COMPLETED);
  assert(pool_doser::rest_timer.remaining(millis())>1700000);

  reset();
  master::import_settings(10,0.9,13,0,1800);
  assert(master::settings_import_result=="refused_busy_or_not_in_maintenance" && pool_storage::saves==1);
  master::maintenance_lockout_on(); advance(2000);
  master::import_settings(10,0.9,13,0,1800);
  assert(master::settings_import_result=="imported");
  const auto imported=pool_storage::saves;
  master::import_settings(10,0.9,13,0,1800);
  assert(pool_storage::saves==imported);
  master::import_settings(10,NAN,13,0,1800);
  assert(master::settings_import_result=="refused_invalid_settings" && pool_storage::saves==imported);
  restart_master(); advance(2000);
  assert(master::settings_ok && master::maintenance_lockout.state);
  assert(std::fabs(master::calibration_factor.state-0.9f)<0.0001f);
  assert(master::pump_gpd.state==10 && master::max_single_dose_oz.state==13);
  assert(master::pre_run_s.state==0 && master::min_rest_s.state==1800);
  assert(pool_storage::saves==imported);

  // Preserve a five-second pre-run and zero rest, too. Values must
  // survive import and restart instead of silently becoming firmware defaults.
  master::import_settings(10,0.9,13,5,0);
  assert(master::settings_import_result=="imported" && pool_storage::saves==imported+1);
  restart_master(); advance(2000);
  assert(master::settings_ok && master::maintenance_lockout.state);
  assert(master::pump_gpd.state==10 && master::max_single_dose_oz.state==13);
  assert(std::fabs(master::calibration_factor.state-0.9f)<0.0001f);
  assert(master::pre_run_s.state==5 && master::min_rest_s.state==0);
  master::import_settings(10,0.9,13,5,0);
  assert(master::settings_import_result=="imported" && pool_storage::saves==imported+1);
  assert_stopped();

  // A missing record shows defaults; single edits must not save them as real settings.
  reset(); pool_storage::data.clear(); restart_master(); advance(2000);
  assert(!master::settings_ok && master::blocked_reason=="configuration_required");
  const auto before_edit=pool_storage::saves;
  master::set_calibration_factor(1.25f);
  master::maintenance_lockout_off();
  assert(!master::settings_ok && master::blocked_reason=="configuration_required");
  assert(pool_storage::saves==before_edit && pool_storage::data.empty());
  master::dose.execute(0.5, ""); advance(1000); assert_stopped();
  master::maintenance_lockout_on(); advance(2000);
  master::import_settings(10,0.9,13,0,1800);
  assert(master::settings_import_result=="imported" && master::settings_ok);
  assert(pool_storage::saves==before_edit+1);
  master::set_calibration_factor(0.95f);
  assert(master::settings_ok && pool_storage::saves==before_edit+2);
}
void settings_import_requires_maintenance_intent() {
  // Both an explicit Off and a restored Off keep the switch On until radio-off proof.
  for (bool restored_off: {false,true}) {
    reset();
    if (restored_off) restart_master();
    else {
      master::maintenance_lockout_on(); advance(2000);
      master::maintenance_lockout_off();
    }
    assert(master::maintenance_lockout.state && !pool_doser::master_maintenance.requested());
    const auto saved=pool_doser::settings_store.state();
    const auto persisted=pool_storage::data;
    const auto writes=pool_storage::saves;
    master::import_settings(10,0.9,13,0,1800);
    assert(master::settings_import_result=="refused_busy_or_not_in_maintenance");
    assert(std::memcmp(&saved, &pool_doser::settings_store.state(), sizeof(saved))==0);
    assert(pool_storage::data==persisted && pool_storage::saves==writes);
    assert(master::pump_gpd.state==saved.pump_gpd);
    assert(master::calibration_factor.state==saved.calibration_factor());
    advance(2000);
    assert(!master::maintenance_lockout.state && !pool_doser::master_maintenance.requested());
    assert(pool_doser::settings_store.state().maintenance==0 && pool_storage::saves==writes);
  }

  reset();
  master::maintenance_lockout_on(); advance(2000);
  master::maintenance_lockout_off();
  const auto off_generation=pool_doser::master_maintenance.generation();
  // Let the Slave's radio-off reply reach the UART before the Master processes it.
  for (unsigned n=0;n<100 &&
      !(slave::slave_wifi.is_disabled() && master::uart.available()>=pool_doser::FRAME_SIZE);++n)
    advance(10);
  assert(slave::slave_wifi.is_disabled() && master::uart.available()>=pool_doser::FRAME_SIZE);
  assert(master::maintenance_lockout.state && !pool_doser::master_maintenance.requested());
  master::maintenance_lockout_on();
  const auto writes=pool_storage::saves;
  master::import_settings(10,0.9,13,0,1800);
  assert(master::settings_import_result=="imported" && pool_storage::saves==writes+1);
  assert(pool_doser::settings_store.state().maintenance==1);
  master::receive();
  assert(pool_doser::master_link.response_matched);
  assert(pool_doser::master_link.matched_request_generation==off_generation);
  assert(!(pool_doser::master_link.matched_request_flags & pool_doser::MAINTENANCE_LOCKED));
  assert(pool_doser::master_link.matched_response.flags & pool_doser::B_DOSING_READY);
  assert(master::maintenance_lockout.state && pool_doser::master_maintenance.requested());
  advance(2000);
  assert(master::maintenance_lockout.state && pool_doser::master_maintenance.locked());
  assert_stopped();
  master::dose.execute(0.5, ""); advance(2000);
  assert_stopped();
  assert(master::blocked_reason=="maintenance_lockout" && observed_delivery_ms==0);
  master::import_settings(10,0.9,13,0,1800);
  assert(master::settings_import_result=="imported" && pool_storage::saves==writes+1);
  restart_master(); advance(2000);
  assert(master::maintenance_lockout.state && pool_doser::master_maintenance.requested());
  assert(pool_storage::saves==writes+1);
}
void automatic_windows_and_rollover() {
  reset();
  pool_doser::AcidControl control;
  pool_doser::AutomaticState s;
  uint32_t now=UINT32_MAX-160000;
  uint64_t unix_ms=1790000000000ULL;
  control.begin("boot", now, s);
  auto feed=[&](uint32_t duration, float ph, bool connected=true) {
    for (uint32_t t=0;t<duration;t+=1000) {
      now+=1000; unix_ms+=1000;
      control.set_clock(now, unix_ms, true, 1);
      control.tick(now, true);
      if (connected) {
        control.query(now, "HA");
        control.observe(now, true, "Atlas", std::to_string(unix_ms), ph);
      }
    }
  };
  // Readiness is independent of HA's threshold decision or saved Auto choice.
  feed(305000, 7.80f); assert(control.reason(now,s)=="ready");
  assert(std::fabs(control.average(now)-7.80f)<0.0001f);
  feed(121000, 7.81f); assert(control.reason(now,s)=="ready");
  auto before_restart=control.issue(now, true);
  control.query(now, "restarted-HA");
  assert(control.reason(now,s)=="fresh_ph_window");
  assert(!control.consume(now,before_restart));
  feed(121000, 7.81f); assert(control.reason(now,s)=="ready");
  auto token=control.issue(now, true); assert(control.consume(now, token));
  assert(!control.consume(now, token));
  control.accept(s, token, 2, true); feed(20000, 7.81f, false);
  control.finish(now,s,true,"completed");
  feed(300000,7.81f,false); assert(control.reason(now,s)=="ha_unavailable");
  feed(121000,7.81f); assert(control.reason(now,s)=="ready");
  // Long monitoring with many threshold crossings: no recurring writes and
  // no old half-range sample watermark after 30/50-day observation outages.
  for (unsigned hour=0;hour<24*60;++hour) feed(3600000, hour%2 ? 7.80f : 7.81f);
  for (uint64_t days : {30ULL,50ULL}) {
    const uint64_t gap=days*86400000ULL;
    now+=uint32_t(gap); unix_ms+=gap;
    control.tick(now,true); control.set_clock(now,unix_ms,true,1);
    feed(121000,7.81f); assert(control.reason(now,s)=="ready");
  }
  control.set_clock(now,unix_ms+60000,true,2); assert(!std::isfinite(control.average(now)));
  const auto old=control.issue(now,true);
  control.begin("new-boot",now,s);
  assert(s.result==pool_doser::DoseResult::NONE && !control.consume(now,old));
  assert(control.mix_remaining_ms()==300000 && pool_storage::saves==1);
}
void last_delivered() {
  // Last Delivered reports pumped ounces only; rejected requests and pre-run
  // stops keep the previous value.
  reset(); master::last_delivered_oz.state=-1;
  master::dose.execute(20, ""); advance(2000);
  assert(master::blocked_reason=="exceeds_single_dose_limit" && master::last_delivered_oz.state==-1);
  master::set_pre_run_s(60); request(2.0f); advance(1000); master::stop_dose.execute(); advance(2000);
  assert(observed_delivery_ms==0 && master::last_delivered_oz.state==-1);
  master::set_pre_run_s(0);
  complete_pulse();
  assert(std::fabs(master::last_delivered_oz.state-0.5f)<0.001f);
  observed_delivery_ms=0;
  request(); reach_delivery(); advance(9500);
  master::stop_dose.execute(); advance(3000); assert_stopped();
  const float expected=0.5f*float(observed_delivery_ms)/30000.0f;
  assert(pool_doser::automatic_state.result==pool_doser::DoseResult::INTERRUPTED);
  assert(expected>0.1f && expected<0.3f);
  assert(std::fabs(master::last_delivered_oz.state-expected)<0.02f);
  master::dose.execute(20, ""); advance(2000);
  assert(std::fabs(master::last_delivered_oz.state-expected)<0.02f);
}
int main() {
  delivery_and_settings(); ordinary_interruptions(); lost_status_replies_during_delivery(); physical_protection();
  save_failure_and_pre_run(); settings_import_requires_maintenance_intent(); automatic_windows_and_rollover();
  last_delivered();
  std::cout << "Production Master/Slave scheduler: delivery, last delivered, recovery, faults, intent, 60-day operation and rollover passed\n";
}
