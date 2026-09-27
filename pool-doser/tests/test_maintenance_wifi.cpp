// Lambdas under test are extracted from both firmware YAMLs, not reimplemented.
#include "pool_doser_rs485.h"
#include <cassert>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

uint32_t fake_millis = 10000;
uint32_t millis() { return fake_millis; }

struct Uart {
  std::vector<uint8_t> rx, tx;
  size_t cursor{0};
  size_t available() const { return rx.size() - cursor; }
  bool read_byte(uint8_t *value) {
    if (!available()) return false;
    *value = rx[cursor++];
    return true;
  }
  void write_array(const uint8_t *data, size_t length) {
    tx.insert(tx.end(), data, data + length);
  }
  void flush() {}
};
struct Switch {
  bool state{false}, known{true};
  unsigned on_calls{0};
  std::function<void()> off_action;
  bool has_state() const { return known; }
  void publish_state(bool value) { state = value; }
  void turn_on() { state = true; ++on_calls; }
  void turn_off() { state = false; if (off_action) off_action(); }
};
struct Script {
  bool running{false};
  std::function<void()> action;
  bool is_running() const { return running; }
  void stop() { running = false; }
  void execute() { if (action) action(); }
};
struct Text {
  std::string state;
  void publish_state(const std::string &value) { state = value; }
};
struct Wifi {
  bool disabled{true}, connected{false};
  int32_t rssi{-67};
  unsigned starts{0}, stops{0}, rssi_reads{0};
  std::function<void()> on_enable;
  bool is_disabled() const { return disabled; }
  bool is_connected() const { return !disabled && connected; }
  int32_t wifi_rssi() { ++rssi_reads; return rssi; }
  void enable() { if (on_enable) on_enable(); disabled = false; ++starts; }
  void disable() { disabled = true; ++stops; }
};

#include "maintenance_automation.h"

bool to_master = true, to_slave = true, ota_blocked = false;
bool duplicate_requests = false;
unsigned fault_latches = 0, stop_calls = 0;

void transfer(Uart &from, Uart &to, bool enabled, bool duplicate = false) {
  if (enabled) {
    to.rx.insert(to.rx.end(), from.tx.begin(), from.tx.end());
    if (duplicate) to.rx.insert(to.rx.end(), from.tx.begin(), from.tx.end());
  }
  from.tx.clear();
}

void advance(unsigned milliseconds) {
  for (unsigned elapsed = 0; elapsed < milliseconds; elapsed += 10) {
    fake_millis += 10;
    if (fake_millis % 250 == 0) master::heartbeat();
    if (fake_millis % 50 == 0) master::receive();
    transfer(master::uart, slave::uart, to_slave, duplicate_requests);
    if (!ota_blocked && fake_millis % 20 == 0) slave::tick();
    transfer(slave::uart, master::uart, to_master);
    // This is the invariant even during retries, an upload, and a pending OFF.
    if (!master::maintenance_lockout.state) assert(slave::slave_wifi.is_disabled());
    if (!slave::slave_wifi.is_disabled()) assert(!slave::relay_b.state);
  }
}

void reset(bool locked = false) {
  fake_millis = 10000;
  pool_doser::master_link = pool_doser::Link(true); pool_doser::slave_link = pool_doser::Link();
  pool_doser::master_maintenance = {}; pool_doser::slave_maintenance = {};
  master::uart = {}; slave::uart = {};
  master::maintenance_lockout = {}; master::relay_a = {}; master::relay_b = {};
  master::run_sequence = {}; master::stop_dose = {}; master::runtime_guard = {};
  master::delivery_open = master::dosing_active = false;
  master::rs485_abort_issued = master::output_voltage_proven = false;
  master::relay_fault = false; master::relays_off_ms = 0;
  master::rs485_transaction = 1;
  slave::relay_b = {}; slave::outlet_live = {}; slave::slave_wifi = {};
  to_master = to_slave = true; ota_blocked = duplicate_requests = false;
  fault_latches = stop_calls = 0;
  master::relay_b.off_action = master::relay_b_off;
  master::force_outputs_safe.action = master::actual_force_outputs_safe;
  master::latch_relay_fault_safely.action = []() {
    ++fault_latches; master::relay_fault = true;
  };
  master::stop_dose.action = []() {
    ++stop_calls;
    master::actual_force_outputs_safe();
    master::run_sequence.stop();
    master::stop_dose.running = true;
    // Leave accounting pending, just as the actual stop awaits its final ACK.
  };
  slave::slave_wifi.on_enable = []() {
    assert(master::maintenance_lockout.state);
    assert(!master::relay_a.state && !slave::relay_b.state);
    assert(!master::delivery_open && !master::dosing_active);
  };
  if (locked) master::lock(); else master::unlock();
  assert(master::maintenance_lockout.state);  // boot always requires proof
}

void test_boot_and_transitions() {
  reset();
  assert(slave::slave_wifi.is_disabled());
  assert(pool_doser::slave_maintenance.blocks_dosing());
  advance(1000);
  assert(!master::maintenance_lockout.state);
  assert(!pool_doser::slave_maintenance.blocks_dosing());
  assert(slave::slave_wifi.starts == 0);
  master::lock(); advance(1200);
  assert(master::maintenance_lockout.state && !slave::slave_wifi.is_disabled());
  assert(pool_doser::slave_maintenance.blocks_dosing());
  assert(slave::slave_wifi.starts == 1);
  advance(3000); assert(slave::slave_wifi.starts == 1);
  master::unlock(); assert(master::maintenance_lockout.state);
  advance(1000);
  assert(!master::maintenance_lockout.state && slave::slave_wifi.is_disabled());
  assert(slave::slave_wifi.stops == 1 && fault_latches == 0);
  advance(2000); assert(slave::slave_wifi.starts == 1);
}

void test_active_stop_and_local_rejection() {
  reset(); advance(1000);
  // Real slave command execution must work in normal mode.
  pool_doser::master_link.queue_request(pool_doser::TEST_CLOSE_B, 1, 1500);
  advance(200);
  pool_doser::master_link.queue_request(pool_doser::OPEN_B, 1);
  advance(200);
  pool_doser::master_link.queue_request(pool_doser::DOSE_B, 1, 10000);
  advance(100);
  assert(slave::relay_b.state && pool_doser::slave_link.active);
  master::relay_a.state = true;
  master::run_sequence.running = master::dosing_active = true;
  master::delivery_open = true;
  master::lock();
  assert(stop_calls == 1 && !master::relay_a.state);
  advance(1000);
  assert(!slave::relay_b.state && slave::slave_wifi.is_disabled());
  assert(master::delivery_open);
  master::delivery_open = master::dosing_active = false;
  master::stop_dose.running = false;
  advance(1000);
  assert(!slave::slave_wifi.is_disabled());
  const auto on_calls = slave::relay_b.on_calls;
  for (auto command : {pool_doser::CLEAR_FAULT, pool_doser::TEST_CLOSE_B,
                       pool_doser::DOSE_B, pool_doser::ABORT_B}) {
    pool_doser::master_link.queue_request(command, 1, 1500);
    advance(500);
    assert(pool_doser::slave_maintenance.blocks_dosing());
    assert(!slave::relay_b.state && slave::relay_b.on_calls == on_calls);
  }
}

void test_loss_duplicates_and_restarts() {
  reset(true); duplicate_requests = true; advance(1500);
  assert(!slave::slave_wifi.is_disabled() && slave::slave_wifi.starts == 1);
  to_slave = false; advance(1500);
  assert(slave::slave_wifi.is_disabled());
  assert(pool_doser::slave_maintenance.blocks_dosing());
  assert(master::maintenance_lockout.state && fault_latches == 0);
  to_slave = true; advance(1500); assert(!slave::slave_wifi.is_disabled());
  // No reply: OFF stays pending, even once the slave radio really is off.
  to_master = false; master::unlock(); advance(1500);
  assert(master::maintenance_lockout.state && slave::slave_wifi.is_disabled());
  to_master = true; advance(1500); assert(!master::maintenance_lockout.state);
  master::lock(); advance(1500);
  pool_doser::slave_link = pool_doser::Link(); pool_doser::slave_maintenance = {};
  slave::slave_wifi.disabled = true; slave::uart = {};
  assert(pool_doser::slave_maintenance.blocks_dosing());
  advance(1500); assert(!slave::slave_wifi.is_disabled());
  // A master reboot restoring OFF still publishes ON while reconciling.
  pool_doser::master_link = pool_doser::Link(true); pool_doser::master_maintenance = {};
  master::uart = {}; master::unlock();
  assert(master::maintenance_lockout.state);
  advance(1500); assert(!master::maintenance_lockout.state);
}

void test_upload_success_and_error() {
  for (bool success : {false, true}) {
    reset(true); advance(1500);
    slave::ota_begin(); ota_blocked = true;
    advance(2000);
    master::unlock();
    advance(5000);  // OTA blocks the entire slave loop, including RS485.
    assert(master::maintenance_lockout.state);
    assert(!slave::slave_wifi.is_disabled() && !slave::relay_b.state);
    assert(fault_latches == 0);
    if (success) {
      pool_doser::slave_link = pool_doser::Link(); pool_doser::slave_maintenance = {};
      slave::uart = {}; slave::slave_wifi.disabled = true;
    } else {
      // Include a partial frame; the callback must discard it too.
      slave::uart.rx.push_back(pool_doser::SOF_1);
      slave::ota_error();
      assert(slave::uart.available() == 0);
      assert(pool_doser::slave_maintenance.blocks_dosing());
      assert(slave::slave_wifi.is_disabled());
    }
    ota_blocked = false; advance(1500);
    assert(!master::maintenance_lockout.state);
    assert(slave::slave_wifi.is_disabled() && !slave::relay_b.state);
  }
  reset(true); advance(1500);
  slave::ota_begin(); ota_blocked = true; advance(3000);
  slave::ota_error(); ota_blocked = false;
  assert(slave::slave_wifi.is_disabled());
  advance(1500);  // Lockout remains ON: a fresh lease permits another attempt.
  assert(master::maintenance_lockout.state && !slave::slave_wifi.is_disabled());
}

void test_stale_ack_and_rapid_toggles() {
  reset(); advance(1000);
  master::lock(); advance(1500);
  // Get a normal-mode reply but hold it across ON/OFF toggles.
  master::unlock();
  master::receive(); master::heartbeat(); master::receive();
  transfer(master::uart, slave::uart, true); slave::tick();
  master::lock(); master::unlock();
  transfer(slave::uart, master::uart, true); master::receive();
  assert(master::maintenance_lockout.state);
  advance(1500); assert(!master::maintenance_lockout.state);
  master::lock(); master::unlock(); master::lock();
  advance(1500);
  assert(master::maintenance_lockout.state && !slave::slave_wifi.is_disabled());

  // Check the actual Link correlation independently of scheduler timing.
  pool_doser::MasterMaintenance control;
  pool_doser::Link link{true}, encoder;
  Uart requests;
  control.request(false);
  link.request_flags = control.flags(true);
  link.request_generation = control.generation();
  link.queue_request(pool_doser::STATUS_REQUEST);
  link.service_master(&requests);
  const uint32_t old_sequence = pool_doser::get32(requests.tx.data() + 4);
  control.request(true); control.request(false);
  Uart replies;
  encoder.peer_session = link.local_session;
  encoder.send(&replies, pool_doser::STATUS_RESPONSE, 0, old_sequence,
               pool_doser::B_DOSING_READY);
  replies.rx = replies.tx;
  link.poll(&replies);
  assert(link.response_matched);
  assert(link.matched_request_generation != control.generation());
  assert(!control.release(link, true));
  for (uint16_t flags : {uint16_t{0}, uint16_t(pool_doser::B_DOSING_READY |
                         pool_doser::B_WIFI_ENABLED),
                         uint16_t(pool_doser::B_DOSING_READY |
                         pool_doser::MAINTENANCE_LOCKED),
                         uint16_t(pool_doser::B_DOSING_READY |
                         pool_doser::RELAY_B_CLOSED),
                         uint16_t(pool_doser::B_DOSING_READY |
                         pool_doser::OUTLET_LIVE),
                         uint16_t(pool_doser::B_DOSING_READY)}) {
    requests.tx.clear(); replies = {};
    link.request_generation = control.generation();
    link.queue_request(pool_doser::STATUS_REQUEST);
    link.service_master(&requests);
    const auto sequence = pool_doser::get32(requests.tx.data() + 4);
    encoder.send(&replies, pool_doser::STATUS_RESPONSE, 0, sequence, flags);
    replies.rx = replies.tx;
    link.poll(&replies);
    assert(link.response_matched);
    assert(!control.release(link, false)); // local shutdown must also finish
    const bool safe = flags == pool_doser::B_DOSING_READY;
    assert(control.release(link, true) == safe);
    assert(control.locked() == !safe);
  }
  // Retried command identity includes its authorization flags.
  pool_doser::Frame command;
  command.command = pool_doser::DOSE_B;
  command.sequence = 17; command.transaction = 10; command.duration_ms = 5000;
  link.remember_command(command);
  assert(link.is_duplicate_command(command));
  command.flags = pool_doser::MAINTENANCE_LOCKED;
  assert(!link.is_duplicate_command(command));
}

void test_detector_and_bad_frames() {
  reset(true);
  slave::outlet_live.known = false; advance(1500);
  assert(slave::slave_wifi.is_disabled());
  slave::outlet_live.known = true; slave::outlet_live.state = true;
  advance(1500); assert(slave::slave_wifi.is_disabled());
  slave::outlet_live.state = false; advance(1500);
  assert(!slave::slave_wifi.is_disabled());
  // Old protocol and CRC damage must neither authorize Wi-Fi nor energize B.
  for (bool old_version : {false, true}) {
    reset(); to_slave = false;
    Uart wire; pool_doser::Link sender;
    sender.send(&wire, pool_doser::DOSE_B, 1, 5000,
                pool_doser::MAINTENANCE_LOCKED | pool_doser::ALLOW_MAINTENANCE_WIFI);
    if (old_version) {
      wire.tx[2]--;
      pool_doser::put16(wire.tx.data() + 43, pool_doser::crc16(wire.tx.data(), 43));
    } else wire.tx[14] ^= 1;
    slave::uart.rx = wire.tx;
    slave::tick();
    assert(slave::slave_wifi.is_disabled() && !slave::relay_b.state);
    assert(pool_doser::slave_maintenance.blocks_dosing());
    advance(1500); assert(master::maintenance_lockout.state);
  }
  // Lease expiry uses unsigned elapsed time across millis() rollover.
  pool_doser::SlaveMaintenance gate;
  fake_millis = UINT32_MAX - 500;
  pool_doser::Frame frame;
  frame.command = pool_doser::STATUS_REQUEST;
  frame.flags = pool_doser::MAINTENANCE_LOCKED | pool_doser::ALLOW_MAINTENANCE_WIFI;
  gate.observe(frame); assert(gate.wants_wifi(true));
  fake_millis += 1001; gate.expire(); assert(!gate.wants_wifi(true));
}

void test_wifi_signal_reporting() {
  reset();
  assert(std::isnan(master::slave_signal()));
  advance(1500);
  slave::slave_wifi.connected = true;  // Even a stale driver state cannot wake Wi-Fi.
  assert(std::isnan(master::slave_signal()));
  advance(1500);
  assert(slave::slave_wifi.starts == 0 && slave::slave_wifi.rssi_reads == 0);

  master::lock();
  slave::slave_wifi.connected = false;
  advance(1500);
  assert(!slave::slave_wifi.is_disabled());
  assert(std::isnan(master::slave_signal()));  // Enabled but not associated.
  slave::slave_wifi.connected = true;
  for (int32_t rssi : {-1, -30, -67, -82, -127}) {
    slave::slave_wifi.rssi = rssi;
    advance(500);
    assert(master::slave_signal() == static_cast<float>(rssi));
    const auto &frame = pool_doser::master_link.received;
    assert((frame.flags & ~pool_doser::WIFI_RSSI_MASK) ==
           (pool_doser::MAINTENANCE_LOCKED | pool_doser::B_WIFI_ENABLED));
    assert(!master::relay_a.state && !slave::relay_b.state);
    assert(frame.live_ms == 0 && frame.remaining_ms == 0);
    assert(frame.fault == pool_doser::FAULT_NONE);
  }
  for (int32_t invalid : {0, 1, -128, INT32_MIN, INT32_MAX}) {
    slave::slave_wifi.rssi = invalid;
    advance(500);
    assert(std::isnan(master::slave_signal()));
  }
  slave::slave_wifi.rssi = -72;
  advance(500); assert(master::slave_signal() == -72);
  slave::slave_wifi.connected = false;
  advance(500); assert(std::isnan(master::slave_signal()));
  slave::slave_wifi.connected = true;
  advance(500); assert(master::slave_signal() == -72);

  // Dropped replies and an OTA-blocked loop cannot leave a live-looking value.
  to_master = false;
  advance(1500); assert(std::isnan(master::slave_signal()));
  to_master = true;
  advance(1500); assert(master::slave_signal() == -72);
  slave::ota_begin(); ota_blocked = true;
  advance(1500); assert(std::isnan(master::slave_signal()));
  slave::ota_error(); ota_blocked = false;
  advance(1500); assert(master::slave_signal() == -72);

  master::unlock(); advance(1500);
  assert(!master::maintenance_lockout.state && slave::slave_wifi.is_disabled());
  assert(std::isnan(master::slave_signal()));
  const auto reads = slave::slave_wifi.rssi_reads;
  advance(1500); assert(slave::slave_wifi.rssi_reads == reads);

  // Older v4 firmware sends zero in the optional bits, not a fictitious 0 dBm.
  pool_doser::Frame frame;
  frame.command = pool_doser::STATUS_RESPONSE;
  frame.flags = pool_doser::MAINTENANCE_LOCKED | pool_doser::B_WIFI_ENABLED;
  assert(std::isnan(pool_doser::slave_wifi_rssi(true, frame)));
  frame.flags |= pool_doser::encode_wifi_rssi(-82);
  assert(pool_doser::slave_wifi_rssi(true, frame) == -82);
  frame.flags &= ~pool_doser::B_WIFI_ENABLED;
  assert(std::isnan(pool_doser::slave_wifi_rssi(true, frame)));
  frame.flags |= pool_doser::B_WIFI_ENABLED;
  frame.flags &= ~pool_doser::MAINTENANCE_LOCKED;
  assert(std::isnan(pool_doser::slave_wifi_rssi(true, frame)));
  frame.flags |= pool_doser::MAINTENANCE_LOCKED;
  frame.command = pool_doser::STATUS_REQUEST;
  assert(std::isnan(pool_doser::slave_wifi_rssi(true, frame)));
}

int main() {
  test_boot_and_transitions();
  test_active_stop_and_local_rejection();
  test_loss_duplicates_and_restarts();
  test_upload_success_and_error();
  test_stale_ack_and_rapid_toggles();
  test_detector_and_bad_frames();
  test_wifi_signal_reporting();
  std::cout << "Maintenance Wi-Fi: 7 scenario groups passed.\n";
}
