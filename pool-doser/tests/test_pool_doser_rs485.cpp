#include "pool_doser_rs485.h"
#include <cassert>
#include <iostream>
#include <vector>
uint32_t now=100;
uint32_t millis() { return now; }
struct Uart {
  std::vector<uint8_t> rx, tx;
  size_t index{0};
  size_t available() const { return rx.size()-index; }
  bool read_byte(uint8_t *v) { if (!available()) return false; *v=rx[index++]; return true; }
  void write_array(const uint8_t *data,size_t n) { tx.insert(tx.end(),data,data+n); }
  void flush() {}
};
void request(pool_doser::Link &master,pool_doser::Link &slave,uint8_t command,
             uint32_t tx=1,uint32_t duration=1000) {
  Uart wire;
  master.queue_request(command,tx,duration); master.service_master(&wire);
  wire.rx=wire.tx; slave.poll(&wire);
  assert(slave.frame_ready); slave.frame_ready=false;
  if (command!=pool_doser::STATUS_REQUEST) slave.remember_command(slave.received);
  Uart reply;
  slave.send(&reply,pool_doser::STATUS_RESPONSE,tx,slave.received.sequence);
  reply.rx=reply.tx; master.poll(&reply);
  assert(master.response_matched);
}
void status_retries() {
  using namespace pool_doser;
  for (uint32_t first_sequence: {1U, UINT32_MAX}) {
    Link master{true}, slave;
    master.next_sequence=first_sequence;
    // Cover lost discovery replies and replies with both sessions established.
    for (int exchange=0;exchange<2;++exchange) {
      Uart wire;
      master.queue_request(STATUS_REQUEST,1,0);
      master.service_master(&wire);
      const auto original=wire.tx;
      uint64_t session=0;
      for (unsigned attempt=0;attempt<=MAX_REQUEST_RETRIES;++attempt) {
        if (attempt) {
          wire=Uart{};
          now+=RESPONSE_TIMEOUT_MS;
          master.service_master(&wire);
          assert(wire.tx==original);
        }
        wire.rx=wire.tx;
        slave.poll(&wire);
        assert(slave.frame_ready);
        slave.frame_ready=false;
        if (attempt) assert(slave.local_session==session);
        session=slave.local_session;
        Uart reply;
        slave.send(&reply,STATUS_RESPONSE,1,slave.received.sequence);
        if (attempt==MAX_REQUEST_RETRIES) {
          reply.rx=reply.tx;
          master.poll(&reply);
          assert(master.response_matched && !master.request_failed);
        }
      }
      assert(master.peer_session==slave.local_session);
      // An equal sequence must not authorize an altered request or refresh the link.
      for (size_t offset: {3U,8U,12U,16U,18U,22U,26U}) {
        Uart changed;
        changed.rx=original;
        if (offset==3) changed.rx[offset]=DOSE_B;
        else changed.rx[offset]^=1;
        put16(changed.rx.data()+43,crc16(changed.rx.data(),43));
        const auto received_at=slave.last_rx_ms;
        ++now;
        slave.poll(&changed);
        assert(!slave.frame_ready && slave.last_rx_ms==received_at);
      }
      request(master,slave,PROBE_STATUS);
      Uart old;
      old.rx=original;
      slave.poll(&old);
      assert(!slave.frame_ready);
    }
  }
}
void abort_after_lost_status() {
  using namespace pool_doser;
  Link master{true}, slave;
  request(master,slave,STATUS_REQUEST);
  request(master,slave,DOSE_B,2,5000);
  // Three status replies are lost during the dose.
  Uart lost;
  master.queue_request(STATUS_REQUEST); master.service_master(&lost);
  for (unsigned attempt=0;attempt<MAX_REQUEST_RETRIES;++attempt) {
    now+=RESPONSE_TIMEOUT_MS; master.service_master(&lost);
  }
  now+=RESPONSE_TIMEOUT_MS; master.service_master(&lost);
  assert(master.request_failed);
  // Energizing stays blocked, while the abort keeps the known Slave session.
  Uart blocked; master.queue_request(DOSE_B,3,5000); master.service_master(&blocked);
  assert(blocked.tx.empty());
  Uart wire; master.queue_request(ABORT_B,2); master.service_master(&wire);
  const auto abort=wire.tx;
  wire.rx=wire.tx; slave.poll(&wire);
  assert(slave.frame_ready && slave.received.command==ABORT_B); slave.frame_ready=false;
  slave.remember_command(slave.received);
  Uart reply; slave.send(&reply,STATUS_RESPONSE,2,slave.received.sequence);
  reply.rx=reply.tx; master.poll(&reply);
  assert(master.response_matched && master.fresh_ack(ABORT_B,2,0) && !master.request_failed);
  // An abort queued while a status request is in flight also reaches the Slave.
  Uart pending; master.queue_request(STATUS_REQUEST); master.service_master(&pending);
  master.queue_request(ABORT_B,2);
  now+=RESPONSE_TIMEOUT_MS; master.service_master(&pending);
  assert(pending.tx.size()==2*FRAME_SIZE && pending.tx[FRAME_SIZE+3]==ABORT_B);
  pending.rx.assign(pending.tx.begin()+FRAME_SIZE,pending.tx.end());
  slave.poll(&pending); assert(slave.frame_ready); slave.frame_ready=false;
  Uart confirmed; slave.send(&confirmed,STATUS_RESPONSE,2,slave.received.sequence);
  confirmed.rx=confirmed.tx; master.poll(&confirmed); assert(master.response_matched);
  // A delayed old abort cannot stop a newer dose.
  request(master,slave,STATUS_REQUEST);
  request(master,slave,DOSE_B,3,5000);
  Uart old; old.rx=abort; slave.poll(&old); assert(!slave.frame_ready);
  // A restarted Slave is found again through a status request with no Slave session.
  Link restarted;
  Uart missed; master.queue_request(STATUS_REQUEST); master.service_master(&missed);
  missed.rx=missed.tx; restarted.poll(&missed); assert(!restarted.frame_ready);
  for (unsigned attempt=0;attempt<=MAX_REQUEST_RETRIES;++attempt) {
    now+=RESPONSE_TIMEOUT_MS; master.service_master(&missed);
  }
  Uart found; master.queue_request(STATUS_REQUEST); master.service_master(&found);
  assert(get64(found.tx.data()+35)==0);
  found.rx=found.tx; restarted.poll(&found); assert(restarted.frame_ready);
  Uart answer; restarted.send(&answer,STATUS_RESPONSE,0,restarted.received.sequence);
  answer.rx=answer.tx; master.poll(&answer);
  assert(master.peer_session==restarted.local_session && master.peer_changed);
}
int main() {
  using namespace pool_doser;
  status_retries();
  abort_after_lost_status();
  Link master{true}, slave;
  request(master,slave,STATUS_REQUEST);
  assert(master.local_session && master.peer_session==slave.local_session);
  request(master,slave,TEST_CLOSE_B,1,2000);
  assert(master.fresh_ack(TEST_CLOSE_B,1,0));
  assert(slave.begin_transaction(slave.received));
  const auto original=slave.received;
  assert(!slave.begin_transaction(original));
  Frame delivery=original; delivery.command=DOSE_B;
  assert(!slave.begin_transaction(delivery));
  slave.transaction_stage=2;
  assert(slave.begin_transaction(delivery));
  assert(!slave.begin_transaction(delivery));
  request(master,slave,OPEN_B);
  Uart stale;
  master.send(&stale,DOSE_B,1,12345);
  const auto previous=stale.tx;
  stale.rx=stale.tx; slave.poll(&stale); assert(slave.frame_ready);
  slave.remember_command(slave.received); slave.frame_ready=false;
  request(master,slave,ABORT_B);
  for (int damage=0;damage<3;++damage) {
    Uart wire; wire.rx=previous;
    if (damage==1) wire.rx[10]^=1;
    if (damage==2) { wire.rx[2]=4; put16(wire.rx.data()+43,crc16(wire.rx.data(),43)); }
    slave.poll(&wire); assert(!slave.frame_ready);
  }
  Link restarted{true};
  request(restarted,slave,STATUS_REQUEST);
  assert(slave.peer_changed); slave.peer_changed=false;
  Uart old; old.rx=previous; slave.poll(&old); assert(!slave.frame_ready);
  Uart old_discovery; master.peer_session=0; master.send(&old_discovery,STATUS_REQUEST);
  old_discovery.rx=old_discovery.tx; slave.poll(&old_discovery);
  slave.frame_ready=false;
  old=Uart{}; old.rx=previous; slave.poll(&old); assert(!slave.frame_ready);
  Link isolated{true}; isolated.peer_session=123;
  Uart wire; isolated.queue_request(DOSE_B,9,5000); isolated.service_master(&wire);
  isolated.queue_request(ABORT_B,9);
  now+=RESPONSE_TIMEOUT_MS; isolated.service_master(&wire);
  assert(wire.tx.size()==2*FRAME_SIZE && wire.tx[FRAME_SIZE+3]==ABORT_B);
  Link observation;
  now=UINT32_MAX-50; observation.observe_outlet(true);
  observation.active=true; observation.deadline_ms=now+100;
  assert(observation.remaining()==100 && observation.output_proven);
  now+=100; observation.observe_outlet(false);
  assert(observation.remaining()==0 && !observation.outlet_live);
  assert(physical_fault(FAULT_POWER_DURING_A_OPEN_TEST));
  assert(!physical_fault(FAULT_LINK_LOST) && !physical_fault(FAULT_INTERRUPTED));
  assert(!delivery_start_interlocks_passed(true,true,false,FAULT_NONE,true,false,false,false,true,false,false,false,false));
  std::cout << "Protocol v5 sessions, stale commands, CRC/version refusal, transactions, abort and rollover passed\n";
}
