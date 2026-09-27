#include "../components/atlas_pool/core.h"
#include "../components/atlas_pool/discovery.h"
#include "../components/atlas_pool/utc.h"
#include "../components/atlas_pool/control_discovery.h"
#include <cassert>
#include <functional>
#include <iostream>
#include <map>
#include <tuple>

using namespace atlas_pool;
struct Bus : IO {
  MeasurementTime time{1790000000000LL,1};
  MeasurementTime measurement_time() override { return time; }
  MaintenanceSetting saved{};
  bool storage=true, has_saved=false;
  std::array<bool,4> disconnected{}, busy{}, was_busy{};
  std::array<float,4> values{{7.4f,-12.0f,25.0f,5200.0f}}, compensation{};
  std::array<int,4> counts{};
  std::array<std::string,4> forced_read{};
  std::map<uint8_t,std::string> outstanding;
  std::map<std::pair<int,std::string>,int> response_status;
  std::map<std::pair<int,std::string>,std::string> response_payload;
  std::vector<std::pair<int,std::string>> commands;
  std::string forced_layout, forced_scale;
  float probe_k=1, factor=0.54;
  float ec_low_reference=NAN;
  int ec_low_status=1;
  int writes=0, settings_writes=0, busy_responses=0, saves=0, logger_interval=0;
  int index(uint8_t a) { for (int s=0;s<4;++s) if (ADDRESS[s]==a) return s; assert(false); return -1; }
  bool save(const MaintenanceSetting &j) override { if (!storage) return false; saved=j; has_saved=true; ++saves; return true; }
  bool write(uint8_t address,const std::string &c) override {
    int s=index(address);
    // A response must be consumed (or reach its bounded deadline) before another write.
    assert(outstanding.count(address)==0 || busy[s] || was_busy[s]);
    was_busy[s]=false;
    outstanding[address]=c; commands.emplace_back(s,c);
    if (c.rfind("Cal,",0)==0 && c!="Cal,?") {
      assert(has_saved && saved.maintenance);
      ++writes;
    }
    // A circuit can apply a write before its reply is read or the link fails.
    auto fields=csv(c);
    if ((fields[0]=="K" || fields[0]=="TDS" || fields[0]=="S" || fields[0]=="D" || fields[0]=="O") && fields[1]!="?") {
      assert(has_saved && saved.maintenance);
      ++settings_writes;
    }
    auto error=response_status.find({s,c});
    if (error!=response_status.end() && error->second==2) return true;
    if (fields[0]=="T") compensation[s]=std::stof(fields[1]);
    else if (fields[0]=="K" && fields[1]!="?") probe_k=std::stof(fields[1]);
    else if (fields[0]=="TDS" && fields[1]!="?") factor=std::stof(fields[1]);
    else if (c=="S,c") forced_scale="?S,C";
    else if (c=="D,0") logger_interval=0;
    else if (fields[0]=="O" && fields[1]!="?") {
      auto outputs=csv(forced_layout);
      outputs.push_back(fields[1]);
      forced_layout="?O";
      for (const char *key: {"EC","TDS","S","SG"})
        if (std::find(outputs.begin(),outputs.end(),key)!=outputs.end()) forced_layout+=","+std::string(key);
    }
    else if (fields[0]=="Cal" && fields[1]!="?") {
      if (fields[1]=="clear") { counts[s]=0; if (s==3) ec_low_reference=NAN; }
      else if (fields[1]=="dry") { counts[s]=0; values[s]=0; ec_low_reference=NAN; }
      else if (s==3 && fields[1]=="low") {
        // Atlas EC datasheet p. 70: a low point is staged. Measured values
        // stay unchanged until the high point completes the calibration.
        ec_low_reference=std::stof(fields.back()); counts[s]=ec_low_status;
      }
      else if (s==3 && fields[1]=="high") {
        assert(std::isfinite(ec_low_reference));
        values[s]=std::stof(fields.back()); counts[s]=2; ec_low_reference=NAN;
      }
      else {
        values[s]=std::stof(fields.back());
        if (fields[1]=="mid" || fields.size()==2) counts[s]=1;
        else ++counts[s];
      }
    }
    return true;
  }
  int read(uint8_t address,std::string &reply) override {
    int s=index(address);
    assert(outstanding.count(address));
    if (busy[s]) { ++busy_responses; was_busy[s]=true; return 254; }
    std::string c=outstanding[address];
    auto error=response_status.find({s,c});
    if (error!=response_status.end() && error->second==254) { was_busy[s]=true; ++busy_responses; return 254; }
    outstanding.erase(address);
    if (disconnected[s]) return -1;
    if (error!=response_status.end()) return error->second;
    auto payload=response_payload.find({s,c});
    if (payload!=response_payload.end()) { reply=payload->second; return 1; }
    reply="";
    if (c=="i") reply=std::string("?i,")+NAME[s]+",2.17";
    else if (c=="Cal,?") reply="?Cal,"+std::to_string(counts[s]);
    else if (c=="D,?") reply="?D,"+std::to_string(logger_interval);
    else if (c=="S,?") reply=forced_scale.empty() ? "?S,C" : forced_scale;
    else if (c=="K,?") reply="?K,"+decimal(probe_k);
    else if (c=="TDS,?") reply="?TDS,"+decimal(factor);
    else if (c=="O,?") reply=forced_layout.empty() ? "?O,EC,TDS,S,SG" : forced_layout;
    else if (c=="R") {
      reply=forced_read[s].empty() ? decimal(values[s]) : forced_read[s];
      if (s==3 && forced_read[s].empty()) {
        reply.clear();
        const auto outputs=csv(forced_layout.empty() ? "?O,EC,TDS,S,SG" : forced_layout);
        for (size_t i=1;i<outputs.size();++i) {
          if (i>1) reply+=",";
          reply+=decimal(outputs[i]=="EC" ? values[3] : outputs[i]=="TDS" ? values[3]*factor : outputs[i]=="S" ? 3.1f : 1.002f);
        }
      }
    }
    return 1;
  }
};
struct Rig {
  Bus bus;
  Engine e{bus};
  uint32_t now=0, id=0;
  explicit Rig(bool commissioned=true,uint32_t start=0) : now(start) {
    MaintenanceSetting j; j.maintenance=0;
    e.begin(now,"test-boot",commissioned ? &j : nullptr);
  }
  void run(uint32_t ms) { for (uint32_t n=0;n<ms;n+=20) { e.tick(now); now+=20; } }
  void until(const std::function<bool()> &predicate,uint32_t timeout=40000) {
    for (uint32_t elapsed=0;elapsed<timeout;elapsed+=20) { e.tick(now); now+=20; if (predicate()) return; }
    std::cerr<<"Timed out: "<<e.result<<"; status "<<e.status<<"\n";
    assert(false);
  }
  Request req(const std::string &action) {
    Request r; r.boot=e.boot; r.token=e.token; r.session=e.session; r.issued=now;
    r.id=std::to_string(++id); r.action=action; r.confirmed=true; r.step=e.step; r.unit=e.unit();
    return r;
  }
  bool send(const std::string &action) { return e.request(req(action),now); }
  void session(const std::string &method) {
    auto r=req("begin"); r.method=method; assert(e.request(r,now));
    until([&]{return e.preview.fresh(now,PREVIEW_MS);});
  }
  void point(float reference) {
    until([&]{return e.preview.fresh(now,PREVIEW_MS);});
    auto arm=req("arm"); arm.reference=reference; assert(e.request(arm,now));
    auto apply=req("apply"); apply.reference=reference; assert(e.request(apply,now));
    until([&]{return !e.busy();});
    if (e.recovery) std::cerr<<e.result<<"\n";
    assert(!e.recovery);
  }
};
void parsing() {
  float n;
  for (auto s: {"", "nan", "inf", "7x", "7 8", "?Cal,1", "1e999", ".", "--2"}) assert(!numeric(s,n));
  assert(numeric("0",n) && n==0); assert(numeric("-225.5",n) && n==-225.5f);
  Layout l;
  assert(l.parse("?O,EC,TDS,S") && l.count==3 && l.tds==1 && l.salt==2);
  assert(l.parse("?O,EC,TDS,S,SG") && l.count==4);
  for (auto s: {"?O,TDS,S", "?O,EC,S,TDS", "?O,EC,TDS,TDS,S", "?O,EC,TDS,S,X", "1,2,3"}) assert(!l.parse(s));
}
void identity_responses() {
  for (const char *prefix: {"?i", "?I"}) {
    Rig r;
    const std::array<std::string,4> identities{{"pH,2.17", "ORP,2.17", "RTD,2.17", "EC,2.17"}};
    for (int s=0;s<4;++s) r.bus.response_payload[{s,"i"}]=std::string(prefix)+","+identities[s];
    r.run(25000);
    for (int s=0;s<4;++s) assert(r.e.online[s]);
    assert(r.e.identity[0]=="pH 2.17" && r.e.identity[1]=="ORP 2.17");
    assert(r.e.identity[2]=="RTD 2.17" && r.e.identity[3]=="EC 2.17");
    for (int i=0;i<6;++i) assert(r.e.available(i,r.now));
    r.session("ph_1"); r.point(7);
    assert(r.e.step=="done" && r.bus.writes==1);
  }
  for (const char *reply: {"", "?x,pH,2.17", "?i,EC,2.17", "?i,pH", "?i,pH,", "?i,pH,2.17,extra"}) {
    Rig r; r.bus.response_payload[{0,"i"}]=reply; r.run(25000);
    assert(!r.e.online[0] && !r.e.available(0,r.now));
    for (int i=1;i<6;++i) assert(r.e.available(i,r.now));
    assert(r.bus.writes==0);
  }
}
void normal_and_rollover() {
  for (uint32_t start: {0u,UINT32_MAX-10000}) {
    Rig r(true,start); r.run(30000);
    for (int i=0;i<6;++i) assert(r.e.available(i,r.now));
    assert(r.e.readings[1].value==-12 && r.e.readings[2].value==25);
    assert(r.e.readings[3].at==r.e.readings[4].at && r.e.readings[4].at==r.e.readings[5].at);
    assert(r.bus.compensation[0]==25 && r.bus.compensation[3]==25 && !r.e.compensation_assumed);
    assert(r.e.readings[5].value==2808);
    uint32_t sample=r.e.readings[0].at;
    assert(!r.e.available(0,sample+30000));
    assert(r.bus.writes==0);
  }
}
void missing_temperature() {
  Rig r; r.bus.disconnected[2]=true; r.run(30000);
  assert(!r.e.available(2,r.now));
  assert(r.e.available(0,r.now) && r.e.available(3,r.now));
  assert(r.e.compensation_assumed && r.bus.compensation[0]==25 && r.bus.compensation[3]==25);
  r.bus.disconnected[2]=false; r.bus.values[2]=28.5; r.run(20000);
  assert(r.e.available(2,r.now) && r.bus.compensation[3]==28.5 && !r.e.compensation_assumed);
  r.bus.forced_scale="?S,F"; r.run(12000); assert(!r.e.available(2,r.now));
}
void ec_layout_and_fields() {
  Rig r; r.run(25000);
  r.bus.forced_read[3]="5200,broken,3.1,1.002"; r.run(10000);
  assert(r.e.available(3,r.now) && r.e.available(4,r.now) && !r.e.available(5,r.now));
  r.bus.forced_read[3]="5200,2808"; r.run(10000);
  assert(!r.e.available(3,r.now) && !r.e.available(4,r.now) && !r.e.available(5,r.now));
  assert(r.e.available(0,r.now));
  r.bus.forced_read[3].clear(); r.bus.forced_layout="?O,EC,S,TDS"; r.run(10000);
  assert(!r.e.available(3,r.now));
}
void deadline_and_compensation() {
  Rig r; r.bus.busy[3]=true; r.run(30000);
  assert(r.bus.busy_responses>0 && !r.e.available(3,r.now));
  assert(r.e.available(0,r.now) && r.e.available(1,r.now) && r.e.available(2,r.now));
  r.bus.busy[3]=false; r.run(20000); assert(r.e.available(3,r.now));
}
void ph_workflow() {
  Rig r; r.run(25000); r.session("ph_3");
  for (int i=0;i<6;++i) assert(!r.e.available(i,r.now));
  auto wrong=r.req("arm"); wrong.step="high"; wrong.reference=10; assert(!r.e.request(wrong,r.now));
  r.point(7); assert(r.e.step=="low" && r.e.completed==1);
  r.run(3000); r.point(4); assert(r.e.step=="high");
  r.run(3000); r.point(10); assert(r.e.step=="done" && r.e.completed==3 && r.bus.counts[0]==3);
  assert(r.e.maintenance && r.bus.saved.maintenance);
  auto no=r.req("return"); no.confirmed=false; assert(!r.e.request(no,r.now));
  assert(r.send("return")); r.until([&]{return !r.e.maintenance;});
  assert(!r.bus.saved.maintenance && r.e.available(0,r.now));
}
void other_procedures() {
  Rig ec; ec.run(25000); ec.session("ec_2");
  auto wet=ec.req("arm"); wet.step="low"; wet.reference=12880; assert(!ec.e.request(wet,ec.now));
  ec.point(0); ec.bus.values[3]=13756; ec.run(3000); ec.point(12880);
  ec.bus.values[3]=56493; ec.run(3000); ec.point(80000);
  assert(ec.e.step=="done" && ec.e.completed==3 && ec.bus.counts[3]==2);
  Rig orp; orp.run(25000); orp.session("orp"); orp.point(225); assert(orp.e.step=="done");
  Rig rtd; rtd.run(25000); rtd.session("rtd");
  auto wrong=rtd.req("arm"); wrong.reference=32; wrong.unit="F"; assert(!rtd.e.request(wrong,rtd.now));
  rtd.point(0); assert(rtd.e.step=="done");
}
void replay_and_expiry() {
  Rig r; r.run(25000); r.session("orp");
  auto request=r.req("arm"); request.reference=225;
  auto retained=request; retained.retained=true; assert(!r.e.request(retained,r.now));
  auto old=request; old.boot="previous-boot"; assert(!r.e.request(old,r.now));
  old=request; old.issued=r.now-10001; assert(!r.e.request(old,r.now));
  old=request; old.session++; assert(!r.e.request(old,r.now));
  request=r.req("arm"); request.reference=225; assert(r.e.request(request,r.now));
  assert(!r.e.request(request,r.now));
  r.run(10020); auto apply=r.req("apply"); apply.reference=225; assert(!r.e.request(apply,r.now));
  assert(r.bus.writes==0);
}
void interrupted_ram_session() {
  Rig r; r.run(25000); r.session("ph_3"); r.point(7);
  assert(r.send("cancel")); r.e.interrupt("disconnect");
  assert(r.e.maintenance && r.e.recovery && r.bus.counts[0]==1);
  Engine rebooted(r.bus); rebooted.begin(r.now,"new-boot",&r.bus.saved);
  assert(rebooted.maintenance && rebooted.recovery && rebooted.completed==0 && rebooted.selected==-1);
  Rig pending; pending.run(25000); pending.session("orp");
  auto arm=pending.req("arm"); arm.reference=225; assert(pending.e.request(arm,pending.now));
  auto apply=pending.req("apply"); apply.reference=225; assert(pending.e.request(apply,pending.now));
  pending.e.tick(pending.now); assert(pending.bus.saved.maintenance && pending.bus.saves==1);
  pending.e.interrupt("Wi-Fi loss"); pending.run(5000);
  assert(pending.e.recovery && pending.e.maintenance && pending.bus.writes==1);
  assert(pending.send("resume")); pending.run(5000); assert(pending.bus.writes==1 && !pending.e.armed);
}
void configuration_and_storage() {
  Rig r; r.bus.probe_k=10; r.run(25000); assert(r.e.k==10 && !r.e.available(3,r.now));
  auto k=r.req("initialize_k1"); assert(r.e.request(k,r.now)); r.until([&]{return !r.e.busy();});
  assert(r.bus.probe_k==1 && r.e.maintenance);
  auto factor=r.req("tds_factor"); factor.reference=1.1; assert(!r.e.request(factor,r.now));
  factor=r.req("tds_factor"); factor.reference=0.65; assert(r.e.request(factor,r.now)); r.until([&]{return !r.e.busy();});
  assert(std::fabs(r.e.tds_factor-0.65)<0.0001 && r.bus.writes==0);
  Rig bad; bad.run(25000); bad.bus.storage=false;
  auto begin=bad.req("begin"); begin.method="orp"; assert(!bad.e.request(begin,bad.now));
  assert(!bad.e.configuration_ok && bad.e.maintenance && bad.bus.writes==0);
}
void factor_configuration_recovery() {
  for (const char *interrupted_command: {"TDS,0.650","TDS,?"}) {
    for (float requested_factor: {0.54f,0.65f}) {
      Rig r; r.run(25000);
      assert(r.e.tds_factor==0.54f && r.bus.settings_writes==0);
      auto request=r.req("tds_factor"); request.reference=0.65f;
      assert(r.e.request(request,r.now));
      // Interrupt either the setter acknowledgment or its verification query.
      r.until([&] {
        auto found=r.bus.outstanding.find(ADDRESS[3]);
        return found!=r.bus.outstanding.end() && found->second==interrupted_command;
      });
      assert(r.bus.factor==0.65f && r.bus.settings_writes==1);
      r.e.interrupt("MQTT disconnect");
      request=r.req("tds_factor"); request.reference=requested_factor;
      assert(!r.e.request(request,r.now));  // Neither a stale match nor another setter is safe yet.
      const auto recovery_start=r.bus.commands.size();
      r.run(25000);
      assert(r.e.maintenance && r.e.recovery && !r.e.busy());
      assert(r.e.tds_factor==0.65f && r.bus.settings_writes==1 && r.bus.saves==1);
      assert(std::find(r.bus.commands.begin()+recovery_start,r.bus.commands.end(),
                       std::make_pair(3,std::string("TDS,?")))!=r.bus.commands.end());
      request=r.req("tds_factor"); request.reference=requested_factor;
      assert(r.e.request(request,r.now));
      r.until([&]{return !r.e.busy();}); r.run(25000);
      assert(r.bus.factor==requested_factor && r.e.tds_factor==requested_factor);
      assert(r.bus.settings_writes==(requested_factor==0.65f ? 1 : 2));
      assert(r.bus.writes==0 && r.bus.saves==1);
    }
  }

  for (const char *failed_command: {"TDS,0.650","TDS,?"}) {
    for (int code: {-1,254}) {
      Rig r; r.run(25000);
      r.bus.response_status[{3,failed_command}]=code;
      auto request=r.req("tds_factor"); request.reference=0.65f;
      assert(r.e.request(request,r.now));
      r.until([&]{return !r.e.busy();});
      assert(r.e.maintenance && r.e.recovery && r.bus.factor==0.65f);
      // Keep recovery readback unavailable, then malformed, before allowing it to succeed.
      r.bus.response_status.clear(); r.bus.response_status[{3,"TDS,?"}]=-1;
      r.run(15000);
      for (float requested_factor: {0.54f,0.65f}) {
        request=r.req("tds_factor"); request.reference=requested_factor;
        assert(!r.e.request(request,r.now));
      }
      r.bus.response_status.clear(); r.bus.response_payload[{3,"TDS,?"}]="?TDS,invalid";
      r.run(15000);
      request=r.req("tds_factor"); request.reference=0.65f;
      assert(!r.e.request(request,r.now));
      assert(r.bus.settings_writes==1 && r.bus.saves==1);
      r.bus.response_payload.clear(); r.run(25000);
      assert(r.e.tds_factor==0.65f && r.bus.settings_writes==1);
      request=r.req("tds_factor"); request.reference=0.65f;
      assert(r.e.request(request,r.now)); r.run(25000);
      assert(r.bus.settings_writes==1 && r.bus.saves==1 && r.bus.writes==0);
      assert(r.send("return")); r.until([&]{return !r.e.maintenance;});
      assert(r.e.available(3,r.now) && r.e.available(5,r.now));
      assert(r.bus.settings_writes==1 && r.bus.saves==2);
    }
  }

  Rig starting;
  starting.until([&]{return starting.e.online[3];});
  auto request=starting.req("tds_factor"); request.reference=0.54f;
  assert(!starting.e.request(request,starting.now));
  starting.run(25000);
  request=starting.req("tds_factor"); request.reference=0.54f;
  assert(starting.e.request(request,starting.now)); starting.run(25000);
  assert(starting.bus.settings_writes==0 && starting.bus.saves==0);
}
void configuration_retry_readback() {
  struct Setting {
    int sensor;
    const char *action, *setter, *query;
    std::function<void(Bus &)> mismatch;
  };
  const Setting settings[]={
    {2,"configure_monitoring","S,c","S,?",[](Bus &b){b.forced_scale="?S,F";}},
    {2,"configure_monitoring","D,0","D,?",[](Bus &b){b.logger_interval=60;}},
    {3,"configure_monitoring","O,EC,1","O,?",[](Bus &b){b.forced_layout="?O,TDS,S,SG";}},
    {3,"configure_monitoring","O,TDS,1","O,?",[](Bus &b){b.forced_layout="?O,EC,S,SG";}},
    {3,"configure_monitoring","O,S,1","O,?",[](Bus &b){b.forced_layout="?O,EC,TDS,SG";}},
    {3,"initialize_k1","K,1.0","K,?",[](Bus &b){b.probe_k=10;}}
  };
  for (const auto &setting: settings) {
    for (const char *command: {setting.setter,setting.query}) {
      for (int failure: {0,-1,254}) {
        Rig r;
        setting.mismatch(r.bus);
        assert(r.send("cancel")); r.run(25000);
        assert(r.bus.settings_writes==0 && r.bus.saves==1);
        if (failure) r.bus.response_status[{setting.sensor,command}]=failure;
        assert(r.send(setting.action));
        if (failure) r.until([&]{return !r.e.busy();});
        else {
          r.until([&] {
            auto found=r.bus.outstanding.find(ADDRESS[setting.sensor]);
            return r.bus.settings_writes==1 && found!=r.bus.outstanding.end() && found->second==command;
          });
          r.e.interrupt("MQTT disconnect");
        }
        assert(r.bus.settings_writes==1);
        assert(!r.send(setting.action));  // Wait for readback before deciding whether another write is needed.
        r.bus.response_status.clear();
        r.bus.response_status[{setting.sensor,setting.query}]=-1;
        r.run(25000);
        assert(!r.send(setting.action));
        r.bus.response_status.clear();
        r.bus.response_payload[{setting.sensor,setting.query}]="invalid";
        r.run(25000);
        assert(!r.send(setting.action));
        assert(r.bus.settings_writes==1 && r.bus.saves==1);
        r.bus.response_payload.clear();
        const auto recovery_start=r.bus.commands.size();
        r.run(25000);
        assert(std::find(r.bus.commands.begin()+recovery_start,r.bus.commands.end(),
                         std::make_pair(setting.sensor,std::string(setting.query)))!=r.bus.commands.end());
        assert(r.send(setting.action)); r.run(25000);
        assert(r.bus.settings_writes==1 && r.bus.saves==1 && r.bus.writes==0);
      }
    }
  }
  // If a multi-setting operation stops halfway, retry only the remaining mismatches.
  for (int applied=1;applied<=5;++applied) {
    Rig r;
    r.bus.forced_scale="?S,F"; r.bus.logger_interval=60; r.bus.forced_layout="?O,SG";
    assert(r.send("cancel")); r.run(25000);
    assert(r.send("configure_monitoring"));
    r.until([&]{return r.bus.settings_writes==applied;});
    r.e.interrupt("MQTT disconnect");
    assert(!r.send("configure_monitoring"));
    r.run(25000);
    assert(r.bus.settings_writes==applied);
    assert(r.send("configure_monitoring")); r.run(25000);
    assert(r.bus.settings_writes==5 && r.bus.saves==1);
  }
  Rig starting;
  starting.until([&]{return starting.e.online[3];});
  assert(!starting.send("initialize_k1"));
  starting.run(25000);
  assert(starting.send("initialize_k1"));
  assert(starting.bus.settings_writes==0 && starting.bus.saves==0);
}
void circuit_write_policy() {
  for (const char *version: {"1.0","1.96","2.9","2.12","2.13suffix","nan","2.13.0"})
    assert(!circuit_version_at_least(version,2,13));
  for (const char *version: {"2.13","2.17","3.0"}) assert(circuit_version_at_least(version,2,13));
  Rig normal; normal.run(25000);
  assert(normal.send("configure_monitoring"));
  assert(normal.bus.settings_writes==0 && normal.bus.saves==0);
  normal.e.interrupt("network lost"); normal.run(25000);
  normal.e.interrupt("network restored"); normal.run(25000);
  assert(normal.bus.settings_writes==0 && normal.bus.saves==0);

  for (const char *scale: {"?S,C","?S,c"}) {
    Rig logger;
    logger.bus.forced_scale=scale;
    logger.bus.logger_interval=60;
    assert(logger.send("cancel"));  // Deliberate maintenance before querying settings.
    logger.run(25000);
    assert(!logger.e.errors[2].empty() && logger.e.maintenance);
    assert(logger.bus.settings_writes==0 && logger.bus.saves==1);
    assert(logger.send("configure_monitoring"));
    logger.until([&]{return !logger.e.busy();});
    assert(logger.bus.settings_writes==1 && logger.bus.logger_interval==0);
    for (const auto &command: logger.bus.commands) assert(command.second!="S,c");
    assert(logger.send("configure_monitoring"));
    logger.run(25000);
    assert(logger.bus.settings_writes==1 && logger.bus.saves==1);
  }

  Rig changed;
  changed.bus.forced_scale="?S,F";
  changed.bus.logger_interval=60;
  changed.bus.forced_layout="?O,EC,SG";
  changed.run(25000);
  assert(changed.bus.settings_writes==0 && changed.bus.saves==0);
  assert(!changed.e.available(2,changed.now) && !changed.e.available(3,changed.now));
  assert(changed.send("configure_monitoring"));
  changed.until([&]{return !changed.e.busy();});
  assert(changed.bus.settings_writes==4 && changed.bus.saves==1);
  assert(changed.bus.logger_interval==0 && changed.e.maintenance);
  assert(changed.send("configure_monitoring"));
  changed.run(25000);
  assert(changed.bus.settings_writes==4 && changed.bus.saves==1);
  auto back=changed.req("return"); assert(changed.e.request(back,changed.now));
  changed.run(25000);
  if (changed.e.maintenance) std::cerr << "configuration return: " << changed.e.status << "; " << changed.e.result << "; " << changed.e.errors[2] << "; " << changed.e.errors[3] << "; saves " << changed.bus.saves << "\n";
  assert(!changed.e.maintenance && changed.bus.saves==2);

  Rig old;
  old.bus.response_payload[{3,"i"}]="?i,EC,2.12";
  old.run(60000);
  assert(!old.e.available(3,old.now));
  for (const auto &command: old.bus.commands)
    assert(command.first!=3 || command.second.rfind("T,",0)!=0);
  assert(old.bus.settings_writes==0 && old.bus.saves==0);

  // A configuration setter with a missing acknowledgement is never replayed.
  Rig interrupted;
  interrupted.bus.logger_interval=60; interrupted.run(25000);
  interrupted.bus.response_status[{2,"D,0"}]=-1;
  assert(interrupted.send("configure_monitoring")); interrupted.run(25000);
  assert(interrupted.e.maintenance && interrupted.e.recovery);
  assert(interrupted.bus.settings_writes==1 && interrupted.bus.logger_interval==0);
  interrupted.bus.response_status.clear();
  interrupted.e.interrupt("reconnect"); interrupted.run(25000);
  assert(interrupted.bus.settings_writes==1);

  Rig long_run(true,0xFFFF0000u);
  for (unsigned hours=0;hours<60*24;++hours) {
    long_run.run(20000); long_run.now+=3600000u;
    if (hours%24==0) long_run.e.interrupt("daily reconnect");
    long_run.bus.commands.clear();
  }
  long_run.run(25000);
  assert(long_run.bus.saves==0 && long_run.bus.writes==0 && long_run.bus.settings_writes==0);
  assert(!long_run.e.maintenance);
}
void automatic_monitoring_startup() {
  Rig fresh(false);
  assert(!fresh.e.maintenance && !fresh.e.recovery && !fresh.e.armed);
  assert(!fresh.bus.has_saved && fresh.bus.saves==0);
  fresh.run(25000);
  for (int i=0;i<6;++i) assert(fresh.e.available(i,fresh.now));
  for (int s=0;s<4;++s) assert(fresh.e.calibration[s]==0);
  assert(fresh.bus.writes==0 && fresh.e.status=="Monitoring");

  // Ordinary network transitions and OTA must not create a maintenance session.
  for (const char *reason: {"MQTT disconnect", "MQTT reconnection"}) {
    fresh.e.interrupt(reason); fresh.run(10000);
    assert(!fresh.e.maintenance && !fresh.e.recovery);
    for (int i=0;i<6;++i) assert(fresh.e.available(i,fresh.now));
  }
  fresh.e.ota_begin();
  assert(fresh.e.updating && !fresh.e.maintenance && !fresh.bus.saved.maintenance);
  fresh.e.ota_end_failed(); fresh.run(10000);
  assert(!fresh.e.maintenance && fresh.bus.writes==0);
  Bus restarted_bus;
  Engine restarted(restarted_bus);
  restarted.begin(0,"normal-restart",&fresh.bus.saved);
  for (uint32_t now=0;now<25000;now+=20) restarted.tick(now);
  assert(!restarted.maintenance && !restarted.recovery);
  for (int i=0;i<6;++i) assert(restarted.available(i,25000));
  assert(restarted_bus.writes==0);

  // Preserve explicitly entered maintenance even before any calibration write.
  fresh.session("orp");
  Engine calibration_restart(restarted_bus);
  calibration_restart.begin(0,"calibration-restart",&fresh.bus.saved);
  assert(calibration_restart.maintenance && calibration_restart.recovery);
  assert(calibration_restart.selected==-1 && fresh.bus.writes==0);
  assert(fresh.send("cancel"));
  Engine ended_restart(restarted_bus);
  ended_restart.begin(0,"ended-restart",&fresh.bus.saved);
  assert(ended_restart.maintenance && ended_restart.recovery && ended_restart.selected==-1);

  // Damaged configuration cannot silently release deliberate maintenance.
  for (int invalid=0;invalid<3;++invalid) {
    MaintenanceSetting saved; saved.maintenance=1;
    if (invalid==0) saved.magic=0;
    if (invalid==1) saved.version=0;
    if (invalid==2) saved.maintenance=2;
    Bus bus; Engine e(bus); e.begin(0,"invalid-setting",&saved);
    for (uint32_t now=0;now<25000;now+=20) e.tick(now);
    assert(e.maintenance && e.recovery && !e.configuration_ok);
    for (int i=0;i<6;++i) assert(!e.available(i,25000));
    assert(bus.writes==0 && bus.saves==0);
  }
}
void boot_clear_and_ota() {
  Rig fresh(false); fresh.run(25000); assert(!fresh.e.maintenance && fresh.bus.writes==0);
  fresh.session("orp"); fresh.point(225);
  auto wrong=fresh.req("clear"); wrong.step="clear_pH"; assert(!fresh.e.request(wrong,fresh.now));
  auto clear=fresh.req("clear"); clear.step="clear_ORP"; assert(fresh.e.request(clear,fresh.now));
  fresh.until([&]{return !fresh.e.busy();}); assert(fresh.bus.counts[1]==0 && fresh.e.recovery && fresh.e.step=="done");
  fresh.e.ota_begin(); assert(fresh.e.updating && fresh.e.maintenance);
  assert(!fresh.send("return")); fresh.e.ota_end_failed();
  assert(fresh.e.maintenance && fresh.e.recovery);
}
void errors_and_uncertain_writes() {
  for (int code: {2,255,-1,254}) {
    Rig r; r.run(25000);
    r.bus.response_status[{3,"T,25.000"}]=code;
    r.run(18000);
    for (int i=3;i<6;++i) assert(!r.e.available(i,r.now));
    for (int i=0;i<3;++i) assert(r.e.available(i,r.now));
    assert(r.bus.writes==0);
  }
  Rig r; r.run(25000); r.session("orp");
  auto arm=r.req("arm"); arm.reference=225; assert(r.e.request(arm,r.now));
  r.bus.response_status[{1,"Cal,225.000"}]=255;
  auto apply=r.req("apply"); apply.reference=225; assert(r.e.request(apply,r.now));
  r.until([&]{return !r.e.busy();});
  assert(r.bus.counts[1]==1 && r.e.completed==0 && r.e.recovery && r.e.maintenance);
  assert(r.e.result.find("outcome unknown")!=std::string::npos);
  r.run(15000); assert(r.bus.writes==1); // Never retry an uncertain write.
  assert(r.send("resume")); r.run(5000); assert(r.bus.writes==1 && r.e.calibration[1]==1);
}
void reference_and_compensation_guards() {
  Rig r; r.run(25000); r.session("ph_1");
  auto arm=r.req("arm"); arm.reference=7; assert(r.e.request(arm,r.now));
  auto changed=r.req("apply"); changed.reference=7.01; assert(!r.e.request(changed,r.now));
  assert(r.bus.writes==0);
  r.bus.response_status[{0,"T,25.000"}]=2;
  auto apply=r.req("apply"); apply.reference=7; assert(r.e.request(apply,r.now));
  r.until([&]{return !r.e.busy();});
  assert(r.bus.writes==0 && r.e.recovery); // No Cal after a failed compensation acknowledgement.
  Rig old; old.bus.response_payload[{0,"i"}]="?i,pH,2.12"; old.run(25000); old.session("ph_1");
  auto unsupported=old.req("arm"); unsupported.reference=7; assert(!old.e.request(unsupported,old.now));
  assert(old.bus.writes==0);
  Rig buffer; buffer.run(25000); buffer.bus.disconnected[2]=true;
  auto begin=buffer.req("begin"); begin.method="ph_3"; begin.buffer_rtd=true;
  assert(buffer.e.request(begin,buffer.now)); buffer.run(18000);
  assert(!buffer.e.preview.valid && buffer.e.maintenance && buffer.bus.writes==0);
}
void verification_and_reboot_guards() {
  Rig r; r.run(25000); r.session("orp");
  auto arm=r.req("arm"); arm.reference=225; assert(r.e.request(arm,r.now));
  auto apply=r.req("apply"); apply.reference=225; assert(r.e.request(apply,r.now));
  r.until([&]{return r.bus.writes==1;});
  assert(r.bus.counts[1]==1 && r.bus.saved.maintenance && r.e.completed==0);
  Engine rebooted(r.bus); rebooted.begin(r.now,"another-boot",&r.bus.saved);
  assert(rebooted.maintenance && rebooted.recovery && !rebooted.armed && rebooted.completed==0);
  assert(rebooted.result.find("inspect actual circuit calibration")!=std::string::npos);
  assert(!rebooted.request(apply,r.now));
  r.bus.forced_read[1]="190";
  r.until([&]{return !r.e.busy();});
  assert(r.e.recovery && r.e.completed==0 && r.bus.writes==1 && r.bus.counts[1]==1);
  assert(r.send("cancel")); assert(r.e.maintenance && r.bus.counts[1]==1);
}
void mqtt_sample_expiry() {
  using D=PublicationCursor::Decision;
  for (uint32_t sampled: {1000u,UINT32_MAX-1000}) {
    PublicationCursor cursor;
    Reading reading{7.2f,sampled,true};
    assert(cursor.decide(reading,false,sampled+1000)==D::VALUE);
    cursor.sent(D::VALUE,reading);
    assert(cursor.decide(reading,false,sampled+2000)==D::SKIP);
    assert(cursor.decide(reading,false,sampled+29000)==D::SKIP);
    assert(cursor.decide(reading,false,sampled+30000)==D::UNAVAILABLE);
    assert(cursor.decide(reading,true,sampled+2000)==D::UNAVAILABLE);
    cursor.sent(D::UNAVAILABLE,reading);
    assert(cursor.decide(reading,true,sampled+2200)==D::SKIP);
    assert(cursor.decide(reading,false,sampled+5000)==D::SKIP);
    reading.at=sampled+6000;
    assert(cursor.decide(reading,false,sampled+7000)==D::VALUE);
    PublicationCursor reconnect;
    assert(reconnect.decide(reading,false,reading.at+6000)==D::SKIP);
    assert(reconnect.decide(reading,false,reading.at+4999)==D::VALUE);
  }
  static_assert(PUBLISH_MAX_AGE_MS+2000+HA_EXPIRE_SECONDS*1000<=FRESH_MS);
}
void utc_timestamps() {
  Rig r; r.run(25000);
  const auto captured=r.e.readings[0].measured;
  assert(captured.utc_ms==r.bus.time.utc_ms && captured.epoch==1);
  r.bus.time={1790000600000LL,2};
  assert(r.e.readings[0].measured.utc_ms==captured.utc_ms);
  r.run(25000);
  assert(r.e.readings[0].measured.utc_ms==r.bus.time.utc_ms && r.e.readings[0].measured.epoch==2);
  constexpr int64_t epoch=1577836800123LL;
  assert(iso8601(epoch)=="2020-01-01T00:00:00.123Z");
  assert(iso8601(0).empty());
  assert(iso8601(1709164800000LL)=="2024-02-29T00:00:00.000Z");

}
void mqtt_discovery_retry() {
  using Sequence=DiscoverySequence;
  for (uint8_t failure: {uint8_t(0),Sequence::READING_COUNT,Sequence::MAINTENANCE,Sequence::AVAILABILITY}) {
    for (uint32_t start: {100u,UINT32_MAX-2500}) {
      Sequence sequence;
      uint32_t now=start;
      int failures=2;
      bool online=false;
      std::array<int,Sequence::COUNT> attempts{}, accepted{};
      auto publish=[&](uint8_t index) {
        ++attempts[index];
        if (index==failure && failures>0) {
          --failures; now+=2000; // A publish may block before reporting failure.
          return false;
        }
        ++accepted[index];
        if (index==Sequence::AVAILABILITY) online=true;
        return true;
      };
      auto clock=[&] { return now; };
      assert(!sequence.poll(publish,clock));
      assert(attempts[0]==0);
      sequence.request();
      assert(!sequence.poll(publish,clock));
      assert(!online && attempts[failure]==1);
      for (int i=0;i<Sequence::COUNT;++i) assert(accepted[i]==(i<failure ? 1 : 0));
      for (int retry=0;retry<2;++retry) {
        auto before=attempts;
        now+=999;
        assert(!sequence.poll(publish,clock) && attempts==before);
        ++now;
        assert(sequence.poll(publish,clock)==(retry==1));
      }
      assert(online && attempts[failure]==3);
      for (int i=0;i<Sequence::COUNT;++i) {
        assert(accepted[i]==1); // No prefix replay that could refill the outbox.
        assert(attempts[i]==(i==failure ? 3 : 1));
      }
      auto before=attempts;
      assert(!sequence.poll(publish,clock) && attempts==before);
      sequence.request(); // Reconnect / HA birth republishes all retained configs.
      assert(sequence.poll(publish,clock));
      for (int count: accepted) assert(count==2);
    }
  }
  Sequence reconnect;
  uint32_t now=1000;
  std::vector<uint8_t> sent;
  auto blocked=[&](uint8_t index) { sent.push_back(index); return index!=Sequence::AVAILABILITY; };
  auto clock=[&] { return now; };
  reconnect.request(); assert(!reconnect.poll(blocked,clock));
  sent.clear(); reconnect.request(); // A fresh connection replaces a pending retry.
  assert(reconnect.poll([&](uint8_t index) { sent.push_back(index); return true; },clock));
  assert(sent.size()==Sequence::COUNT && sent.front()==0 && sent.back()==Sequence::AVAILABILITY);
}
void staged_ec_low_point() {
  // Intermediate status is not proof of a completed two-point calibration.
  for (int low_status: {1,0,2}) {
    Rig r; r.run(25000); r.session("ec_2"); r.point(0);
    r.bus.ec_low_status=low_status; r.bus.values[3]=13756; r.run(3000);
    r.point(12880);
    assert(r.e.step=="high" && r.e.completed==2 && !r.e.recovery && r.e.maintenance);
    assert(r.bus.values[3]==13756 && r.bus.ec_low_reference==12880);
    assert(r.e.result.find("accepted")!=std::string::npos);
    assert(r.e.result.find("accepted and verified")==std::string::npos);
    assert(r.e.last_calibration.find("high point required")!=std::string::npos);
    r.bus.values[3]=56493; r.run(3000); r.point(80000);
    assert(r.e.step=="done" && r.e.completed==3 && r.e.calibration[3]==2);
    assert(r.e.preview.value==80000 && r.e.result.find("accepted and verified")!=std::string::npos);
  }
  Rig rejected; rejected.run(25000); rejected.session("ec_2"); rejected.point(0);
  rejected.bus.values[3]=13756; rejected.run(3000);
  rejected.bus.response_status[{3,"Cal,low,12880.000"}]=2;
  auto arm=rejected.req("arm"); arm.reference=12880; assert(rejected.e.request(arm,rejected.now));
  auto apply=rejected.req("apply"); apply.reference=12880; assert(rejected.e.request(apply,rejected.now));
  rejected.until([&]{return !rejected.e.busy();});
  assert(rejected.e.recovery && rejected.e.step=="low" && rejected.e.completed==1);

  Rig bad_high; bad_high.run(25000); bad_high.session("ec_2"); bad_high.point(0);
  bad_high.bus.values[3]=13756; bad_high.run(3000); bad_high.point(12880);
  bad_high.bus.forced_read[3]="56493,30506,35,1.02"; bad_high.run(3000);
  arm=bad_high.req("arm"); arm.reference=80000; assert(bad_high.e.request(arm,bad_high.now));
  apply=bad_high.req("apply"); apply.reference=80000; assert(bad_high.e.request(apply,bad_high.now));
  bad_high.until([&]{return !bad_high.e.busy();});
  assert(bad_high.e.recovery && bad_high.e.maintenance && bad_high.e.step=="high" && bad_high.e.completed==2);
}
void return_cycle_isolation() {
  for (uint32_t delay: {0u,1000u,3000u}) {
    Rig r; r.run(25000); assert(r.send("return")); r.run(delay);
    auto begin=r.req("begin"); begin.method="orp";
    assert(!r.e.request(begin,r.now)); // Reproduces return -> 1 s -> begin(orp).
    assert(r.e.maintenance && r.e.selected==-1 && r.e.busy());
    for (int i=0;i<6;++i) assert(!r.e.available(i,r.now));
    for (const char *action: {"initialize_k1","tds_factor"}) {
      auto config=r.req(action); config.reference=0.65;
      assert(!r.e.request(config,r.now));
    }
    r.until([&]{return !r.e.maintenance;});
    r.session("orp"); r.bus.values[1]=225;
    for (int elapsed=0;elapsed<12000;elapsed+=20) {
      r.run(20);
      assert(r.e.maintenance && r.e.selected==1);
      for (int i=0;i<6;++i) assert(!r.e.available(i,r.now));
    }
    assert(r.e.preview.valid && r.e.preview.value==225);
    r.point(225);
    assert(r.send("return")); r.run(1000); assert(r.send("cancel"));
    r.session("ph_1"); r.run(12000);
    assert(r.e.maintenance && r.e.selected==0); // Cancel cannot leave a latent return behind.
  }
}
void return_saves_only_after_fresh_cycle() {
  // A stale circuit ends the return attempt in maintenance; intent stays On.
  Rig r; r.run(25000); r.session("orp"); assert(r.send("cancel"));
  assert(r.bus.saved.maintenance==1);
  r.bus.disconnected[1]=true;
  const int saves=r.bus.saves;
  assert(r.send("return")); r.run(100);
  assert(r.bus.saved.maintenance==1 && r.bus.saves==saves);
  Engine early(r.bus); early.begin(r.now,"reboot-during-return",&r.bus.saved);
  assert(early.maintenance); // A restart before fresh readings cannot bypass the check.
  r.until([&]{return !r.e.busy();},60000);
  assert(r.e.maintenance && r.e.status=="Maintenance" && r.bus.saves==saves);
  assert(r.e.result.find("Return failed: ORP")==0);
  for (int i=0;i<6;++i) assert(!r.e.available(i,r.now));
  // Once the circuit is back, a new confirmation resumes and saves Off once.
  r.bus.disconnected[1]=false;
  assert(r.send("return")); r.until([&]{return !r.e.maintenance;});
  assert(r.bus.saved.maintenance==0 && r.bus.saves==saves+1 && r.e.status=="Monitoring");
  Engine rebooted(r.bus); rebooted.begin(r.now,"after-return",&r.bus.saved);
  assert(!rebooted.maintenance);
  // A failed final save keeps maintenance and reports the configuration error.
  Rig bad; bad.run(25000); bad.session("orp"); assert(bad.send("cancel"));
  bad.bus.storage=false;
  assert(bad.send("return")); bad.until([&]{return !bad.e.busy();},60000);
  assert(bad.e.maintenance && bad.e.recovery && !bad.e.configuration_ok);
  assert(bad.bus.saved.maintenance==1);
}
struct ControlRig : Rig, ControlStorage {
  ControlSettings settings_saved;
  bool settings_ok=true;
  int reference_saves=0;
  Controls controls{*this};
  ControlRig() { run(30000); controls.begin(e,nullptr); }
  bool save_controls(const ControlSettings &s) override {
    ++reference_saves;
    if (!settings_ok) return false;
    settings_saved=s; return true;
  }
  bool set(const std::string &key,const std::string &value) {
    bool ok=controls.set(e,req("set"),key,value,now); controls.sync(e); return ok;
  }
  bool press(const std::string &key) {
    bool ok=controls.press(e,req(key),now); controls.sync(e); return ok;
  }
  void advance(uint32_t ms) { run(ms); controls.sync(e); }
  void preview() { until([&]{ return e.preview.fresh(now,PREVIEW_MS); }); controls.sync(e); }
};
void control_settings_and_persistence() {
  ControlRig r;
  assert(r.set("ph_mid","7.01")); assert(r.controls.settings.numbers[0]==7.01f);
  assert(r.set("orp_reference","-25")); assert(r.set("rtd_reference","0"));
  assert(!r.set("ph_mid","9")); assert(!r.set("ph_mid","nan"));
  assert(!r.set("ph_mid","7junk")); assert(!r.set("ph_mid","1e99"));
  assert(!r.set("unknown","1")); assert(!r.set("clear_sensor","bad"));
  r.settings_ok=false;
  assert(!r.set("ph_mid","7.2")); assert(r.controls.settings.numbers[0]==7.01f);
  Controls restored(r); restored.begin(r.e,&r.settings_saved);
  assert(restored.settings.numbers==r.controls.settings.numbers);
  for (bool flag:restored.switches) assert(!flag);
  ControlSettings corrupt=r.settings_saved; corrupt.numbers[0]=NAN;
  Controls defaults(r); defaults.begin(r.e,&corrupt);
  assert(defaults.settings.numbers[0]==7);
  assert(!r.e.maintenance && r.bus.writes==0);

  // Every reference must survive an import and reload. Restoring reference
  // inputs must never replay a calibration or persistent EZO configuration.
  ControlRig imported;
  const std::array<float,NUMBER_COUNT> changed{{7.01f,4.01f,10.01f,225.1f,14130,90000,0.25f,25.1f,0.65f}};
  const std::array<float,NUMBER_COUNT> captured{{7,4,10,225,12880,80000,0,25,0.54f}};
  for (const auto &values: {changed,captured}) {
    const auto before=imported.reference_saves;
    for (size_t i=0;i<NUMBER_COUNT;++i)
      assert(imported.set(NUMBER_CONTROLS[i].key,std::to_string(values[i])));
    assert(imported.reference_saves==before+NUMBER_COUNT);
    assert(imported.settings_saved.valid());
    Controls reloaded(imported); reloaded.begin(imported.e,&imported.settings_saved);
    assert(reloaded.settings.numbers==values && imported.controls.settings.numbers==values);
    for (size_t i=0;i<NUMBER_COUNT;++i)
      assert(imported.set(NUMBER_CONTROLS[i].key,std::to_string(values[i])));
    assert(imported.reference_saves==before+NUMBER_COUNT);
    assert(!imported.e.maintenance && imported.bus.writes==0);
  }
}
void control_authorization() {
  ControlRig r;
  for (int scenario=0;scenario<5;++scenario) {
    auto request=r.req("set");
    if (scenario==0) request.retained=true;
    if (scenario==1) request.boot="previous-boot";
    if (scenario==2) request.issued=r.now-10001;
    if (scenario==3) ++request.session;
    if (scenario==4) --request.token;
    assert(!r.controls.set(r.e,request,"ph_mid","7.2",r.now));
    assert(r.controls.settings.numbers[0]==7);
  }
  auto duplicate=r.req("set");
  assert(r.controls.set(r.e,duplicate,"ph_mid","7.01",r.now));
  duplicate.token=r.e.token;
  assert(!r.controls.set(r.e,duplicate,"ph_mid","7.2",r.now));
  auto old=r.req("begin");
  assert(r.set("ph_low","4.01"));
  assert(!r.controls.press(r.e,old,r.now));
  assert(!r.e.maintenance && r.bus.writes==0);
}
void control_procedure_mappings() {
  for (int i=0;i<8;++i) {
    ControlRig r;
    assert(r.set("calibration_method",METHOD_LABELS[i]));
    assert(r.press("begin")); assert(r.e.method==METHOD_VALUES[i]);
    r.preview();
    assert(!r.set("calibration_method",METHOD_LABELS[(i+1)%8]));
    assert(!r.set("buffer_temperature","26"));
    assert(!r.set("rtd_in_buffer","ON"));
    assert(r.set("reference_ready","ON")); assert(r.press("arm"));
    float expected=i<4 ? 7 : i==4 ? 225 : 0;
    assert(r.e.reference==expected && r.e.armed);
    r.advance(10020); assert(!r.e.armed);
    assert(!r.press("apply")); assert(r.bus.writes==0);
  }
}
void control_apply_and_confirmations() {
  ControlRig r;
  assert(!r.set("reference_ready","ON"));
  assert(r.press("begin")); r.preview();
  assert(!r.press("apply"));
  assert(r.set("reference_ready","ON")); assert(r.press("arm"));
  assert(r.set("ph_mid","7.01")); assert(!r.e.armed && !r.controls.switches[0]);
  assert(!r.press("apply"));
  assert(r.set("reference_ready","ON")); assert(r.press("arm"));
  auto stale=r.req("apply");
  assert(r.press("apply"));
  assert(!r.controls.switches[0]);
  assert(!r.set("ph_mid","7.2")); // A pending write freezes settings.
  r.until([&]{return !r.e.busy();}); r.controls.sync(r.e);
  assert(r.bus.writes==1 && r.e.completed==1 && r.e.step=="low");
  assert(!r.controls.press(r.e,stale,r.now));
  assert(!r.press("apply")); assert(r.bus.writes==1);
  assert(!r.press("return"));
  assert(r.set("probes_returned","ON")); assert(r.press("return"));
  r.until([&]{return !r.e.maintenance;}); r.controls.sync(r.e);
  assert(r.e.status=="Monitoring" && !r.controls.switches[1]);
}
void control_recovery_and_clear() {
  ControlRig r;
  assert(r.press("begin")); r.preview();
  assert(r.set("reference_ready","ON"));
  r.e.interrupt("MQTT disconnect"); r.controls.reset_confirmations(); r.controls.sync(r.e);
  assert(!r.controls.switches[0] && !r.press("resume"));
  assert(r.set("recovery_acknowledged","ON"));
  r.advance(1000); assert(r.controls.switches[3]); // Recovery confirmation must survive idle loops.
  assert(r.press("resume")); assert(!r.e.recovery && !r.controls.switches[3]);
  r.preview();
  assert(!r.press("clear"));
  assert(r.set("clear_sensor","EC")); assert(r.set("clear_confirmed","ON"));
  assert(!r.press("clear")); assert(r.bus.writes==0);
  assert(r.set("clear_sensor","pH")); assert(r.set("clear_confirmed","ON"));
  assert(r.press("clear")); assert(!r.controls.switches[5]);
  r.until([&]{return !r.e.busy();});
  assert(r.bus.writes==1);
}
void control_advanced_configuration() {
  ControlRig r;
  assert(r.set("tds_factor","0.6"));
  assert(!r.press("set_tds_factor")); assert(r.bus.factor==0.54f);
  assert(r.set("ec_config_confirmed","ON")); assert(r.press("set_tds_factor"));
  r.until([&]{return !r.e.busy();}); r.controls.sync(r.e);
  assert(r.bus.factor==0.6f && !r.controls.switches[4]);
  assert(r.set("probes_returned","ON"));
  r.e.interrupt("disconnect"); r.controls.reset_confirmations(); r.controls.sync(r.e);
  assert(!r.press("return"));
  assert(r.set("probes_returned","ON")); r.advance(1000);
  assert(r.press("return")); r.until([&]{return !r.e.maintenance;});
}
void native_controls() {
  control_settings_and_persistence(); control_authorization(); control_procedure_mappings();
  control_apply_and_confirmations(); control_recovery_and_clear(); control_advanced_configuration();
  ControlRig recovery;
  recovery.e.configuration_ok=false;
  recovery.e.maintenance=recovery.e.recovery=true;
  recovery.controls.sync(recovery.e);
  assert(recovery.set("probes_returned","ON"));
  for (int i=0;i<10;++i) recovery.controls.sync(recovery.e);
  assert(recovery.press("return"));
  recovery.until([&]{return !recovery.e.maintenance;});
  assert(recovery.e.configuration_ok && recovery.bus.saves==1);
  assert(recovery.set("ph_mid","7.1"));
  assert(recovery.set("ph_mid","7.1"));
  assert(recovery.set("calibration_method","ORP - 225 mV"));
  assert(recovery.reference_saves==1);
}
int main(int argc,char **argv) {
  if (argc==2) {
    if (std::string(argv[1])=="ec_low") staged_ec_low_point();
    else if (std::string(argv[1])=="return") return_cycle_isolation();
    else if (std::string(argv[1])=="discovery") mqtt_discovery_retry();
    else if (std::string(argv[1])=="identity") identity_responses();
    else if (std::string(argv[1])=="startup") automatic_monitoring_startup();
    else if (std::string(argv[1])=="factor_recovery") factor_configuration_recovery();
    else if (std::string(argv[1])=="configuration_retry") configuration_retry_readback();
    else if (std::string(argv[1])=="controls") native_controls();
    else assert(false);
    return 0;
  }
  parsing(); identity_responses(); normal_and_rollover(); missing_temperature(); ec_layout_and_fields(); deadline_and_compensation();
  ph_workflow(); other_procedures(); replay_and_expiry(); interrupted_ram_session(); configuration_and_storage(); boot_clear_and_ota();
  errors_and_uncertain_writes(); reference_and_compensation_guards(); verification_and_reboot_guards();
  mqtt_sample_expiry(); utc_timestamps(); mqtt_discovery_retry();
  staged_ec_low_point(); return_cycle_isolation(); return_saves_only_after_fresh_cycle(); automatic_monitoring_startup(); circuit_write_policy();
  factor_configuration_recovery(); configuration_retry_readback();
  native_controls();
  std::cout<<"Atlas engine: scenario groups passed\n";
}
