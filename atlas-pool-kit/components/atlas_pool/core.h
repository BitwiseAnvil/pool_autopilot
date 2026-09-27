#pragma once

// Platform-independent engine; exercised against a simulated EZO bus in tests.
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

namespace atlas_pool {
// Circuit revisions are version components, not decimal measurements (2.9 < 2.13).
inline bool circuit_version_at_least(const std::string &value, unsigned major, unsigned minor) {
  unsigned parts[2]{};
  size_t part=0, digits=0;
  for (char c: value) {
    if (c=='.' && part==0 && digits) { part=1; digits=0; continue; }
    if (c<'0' || c>'9' || ++digits>3) return false;
    parts[part]=parts[part]*10+unsigned(c-'0');
  }
  return part==1 && digits && (parts[0]>major || (parts[0]==major && parts[1]>=minor));
}
constexpr uint32_t FRESH_MS = 30000, PREVIEW_MS = 10000;
constexpr uint8_t ADDRESS[] = {99, 98, 102, 100};
constexpr const char *NAME[] = {"pH", "ORP", "RTD", "EC"};
constexpr const char *READING_NAME[] = {"pH", "ORP", "temperature", "EC", "salinity", "TDS"};
constexpr const char *KEY[] = {"ph", "orp", "temperature", "conductivity", "salinity", "tds"};
inline uint32_t age(uint32_t now, uint32_t then) { return now - then; }
inline bool due(uint32_t now, uint32_t when) { return int32_t(now - when) >= 0; }
inline bool numeric(const std::string &s, float &n) {
  if (s.empty() || s.find_first_not_of("0123456789+-.eE") != std::string::npos) return false;
  char *end;
  n = strtof(s.c_str(), &end);
  return end != s.c_str() && *end == 0 && std::isfinite(n);
}
inline std::string decimal(float n) {
  char s[32];
  snprintf(s, sizeof(s), "%.3f", double(n));
  return s;
}
inline std::vector<std::string> csv(const std::string &s) {
  std::vector<std::string> out;
  size_t from = 0;
  do {
    size_t end = s.find(',', from);
    out.push_back(s.substr(from, end == std::string::npos ? end : end-from));
    if (end == std::string::npos) return out;
    from = end+1;
  } while (out.size() < 10);
  return {}; // Never silently accept truncated input.
}
struct MeasurementTime { int64_t utc_ms{0}; uint32_t epoch{0}; };
struct Reading {
  float value{NAN};
  uint32_t at{0};
  bool valid{false};
  MeasurementTime measured{};
  bool fresh(uint32_t now, uint32_t limit = FRESH_MS) const { return valid && age(now, at) < limit; }
};
// HA starts expire_after when it receives a message, not when EZO sampled it.
// Publish each sample once, only within 5 s of acquisition. Reserve 2 s for the
// bounded MQTT send and 23 s for HA expiry (30 s total); never refresh that timer
// by replaying a still-valid old sample. Physical LAN timing is commissioned separately.
constexpr uint32_t PUBLISH_MAX_AGE_MS = 5000, HA_EXPIRE_SECONDS = 23;
struct PublicationCursor {
  enum class Decision { SKIP, VALUE, UNAVAILABLE };
  bool initialized{false}, valid{false};
  uint32_t sample_at{0};
  Decision decide(const Reading &reading, bool maintenance, uint32_t now) const {
    if (maintenance || !reading.fresh(now))
      return !initialized || valid ? Decision::UNAVAILABLE : Decision::SKIP;
    if (!reading.fresh(now,PUBLISH_MAX_AGE_MS)) return Decision::SKIP;
    return !initialized || !valid || sample_at!=reading.at ? Decision::VALUE : Decision::SKIP;
  }
  void sent(Decision decision,const Reading &reading) {
    initialized=true; valid=decision==Decision::VALUE; sample_at=reading.at;
  }
};
struct Layout {
  int ec{-1}, tds{-1}, salt{-1}, count{0};
  bool valid() const { return ec >= 0 && tds >= 0 && salt >= 0 && count >= 3; }
  bool parse(const std::string &s) {
    *this = {};
    auto f = csv(s);
    if (f.size() < 4 || f.size() > 5 || f[0] != "?O") return false;
    int previous = -1;
    for (size_t i=1; i<f.size(); ++i) {
      int field = f[i]=="EC" ? 0 : f[i]=="TDS" ? 1 : f[i]=="S" ? 2 : f[i]=="SG" ? 3 : -1;
      if (field <= previous) { *this = {}; return false; }
      previous = field;
      if (field == 0) ec = int(i-1);
      if (field == 1) tds = int(i-1);
      if (field == 2) salt = int(i-1);
    }
    count = int(f.size()-1);
    return valid();
  }
};
// Deliberate maintenance intent only. Procedure progress is never restored.
struct MaintenanceSetting {
  uint32_t magic{0x41544D31}, version{1}, maintenance{0};
  bool valid() const { return magic == 0x41544D31 && version == 1 && maintenance <= 1; }
};
struct IO {
  virtual ~IO() = default;
  virtual bool write(uint8_t address, const std::string &command) = 0;
  // EZO status 1=success, 2=syntax, 254=busy, 255=no data; -1=bus/frame failure.
  virtual int read(uint8_t address, std::string &response) = 0;
  virtual bool save(const MaintenanceSetting &setting) = 0;
  virtual MeasurementTime measurement_time() { return {}; }
};
struct Request {
  std::string boot, id, action, method, step, unit;
  uint32_t token{0}, session{0}, issued{0};
  float reference{NAN}, temperature{25};
  bool confirmed{false}, retained{false}, buffer_rtd{false};
};
enum class Kind { ID, CAL_STATUS, SCALE, LOGGER, K, FACTOR, LAYOUT, WRITE_SCALE, WRITE_LOGGER, WRITE_LAYOUT, READY, CONFIG_DONE,
                  COMP, READ, WRITE_CAL, CHECK_CAL, VERIFY, WRITE_K, CHECK_K,
                  WRITE_FACTOR, CHECK_FACTOR, CYCLE_END };
struct Op { int sensor; Kind kind; std::string command; uint32_t delay{300}; bool preview{false}; };

class Engine {
 public:
  explicit Engine(IO &io) : io_(io) {}
  std::array<Reading,6> readings{}; // temperature stored in Celsius
  Reading preview{};
  std::array<bool,4> online{};
  std::array<int,4> calibration{{-1,-1,-1,-1}};
  std::array<std::string,4> identity{}, errors{};
  std::string boot, result, status, method, step, request_id, last_calibration{"No calibration performed"};
  uint32_t token{1}, session{0}, completed{0};
  int selected{-1};
  bool maintenance{false}, recovery{false}, armed{false}, configuration_ok{true}, updating{false};
  float k{NAN}, tds_factor{NAN}, preview_change{NAN}, reference{NAN};
  float compensation_c{25}, buffer_c{25};
  bool compensation_assumed{true}, buffer_rtd{false};
  Layout layout;

  bool busy() const { return pending_ || returning_; }
  bool available(size_t i, uint32_t now) const { return !maintenance && readings[i].fresh(now); }
  const char *unit() const { return selected==0 ? "pH" : selected==1 ? "mV" : selected==2 ? "C" : "uS/cm"; }
  const char *sensor_name() const { return selected>=0 ? NAME[selected] : "None"; }

  void begin(uint32_t now, const std::string &boot_id, const MaintenanceSetting *saved) {
    boot = boot_id;
    next_ = now+1500; // circuits must boot after power enables
    if (saved && !saved->valid()) {
      configuration_ok=false; maintenance=recovery=true;
      result="Maintenance setting unreadable; deliberate configuration confirmation required";
      status="Configuration error";
      return;
    }
    maintenance = saved && saved->valid() && saved->maintenance;
    saved_maintenance_ = maintenance;
    recovery = maintenance;
    // Re-query the actual circuit status; never infer the outcome of an old write.
    result = maintenance ? "Interrupted maintenance: inspect actual circuit calibration; no session restored" : "Starting pool monitoring";
    status = maintenance ? "Maintenance: confirmation required" : "Starting";
  }
  void interrupt(const std::string &reason) {
    ++token;
    armed=false;
    stop_queue();
    returning_=false;
    preview={};
    if (maintenance || pending_) {
      maintenance=recovery=true;
      result = pending_ ? "Outcome unknown after "+reason+"; inspect status and reference before re-arming" : "Maintenance interrupted: "+reason;
      status="Recovery required";
      pending_=false;
    }
  }
  void ota_begin() {
    interrupt("firmware update");
    updating=true;
  }
  void ota_end_failed() { updating=false; }

  // Shared by calibration operations and the device-owned control settings.
  // Consume each fresh envelope once, including requests refused by a later gate.
  bool authorize(const Request &r, uint32_t now) {
    auto refuse=[&](const std::string &why) { request_id=r.id; result="Refused: "+why; return false; };
    if (r.retained) return refuse("retained command");
    if (r.boot!=boot || r.token!=token || age(now,r.issued)>10000) return refuse("expired authorization; refresh controls");
    if (r.id.empty() || r.id.size()>80 || r.id==last_id_) return refuse("duplicate or invalid request");
    last_id_=request_id=r.id;
    ++token;
    if (updating) return refuse("firmware update in progress");
    return true;
  }

  bool request(const Request &r, uint32_t now) {
    if (!authorize(r,now)) return false;
    auto refuse=[&](const std::string &why) { result="Refused: "+why; return false; };
    if (pending_) return refuse("wait for the current write and verification");
    if (returning_ && r.action!="cancel") return refuse("return to pool in progress; wait for monitoring or end the session first");
    if (r.action=="begin") {
      int s = r.method.rfind("ph_",0)==0 ? 0 : r.method=="orp" ? 1 : r.method=="rtd" ? 2 : r.method.rfind("ec_",0)==0 ? 3 : -1;
      if (s<0 || (s==0 && r.method!="ph_1" && r.method!="ph_2_low" && r.method!="ph_2_high" && r.method!="ph_3") ||
          (s==3 && r.method!="ec_1" && r.method!="ec_2")) return refuse("unsupported method");
      if (selected>=0) return refuse("end the current session first");
      if (!r.confirmed) return refuse("confirm entry into maintenance");
      if (!std::isfinite(r.temperature) || r.temperature<0 || r.temperature>60) return refuse("invalid buffer temperature");
      stop_queue();
      maintenance=true; recovery=false; armed=false;
      selected=s; method=r.method; ++session; completed=0;
      ready_[s]=false; // Refresh identity, stored calibration, K and output configuration.
      buffer_c=r.temperature; buffer_rtd=r.buffer_rtd;
      step=s==0 ? "mid" : s==3 ? "dry" : "single";
      preview={}; preview_change=NAN; next_=now; reference=NAN;
      result="Session started: place probe in reference, watch preview, then arm";
      status="Calibration preview";
      return save_intent(maintenance);
    }
    if (r.action=="return") {
      if (!r.confirmed) return refuse("confirm all probes are back in pool water");
      stop_queue();
      maintenance=true; recovery=false; armed=false; returning_=true;
      selected=-1; preview={}; method.clear(); step.clear();
      for (auto &v: readings) v={};
      next_=now;
      status="Restoring pool readings";
      result="Return confirmed; restoring configuration and acquiring a new cycle";
      // Maintenance intent is saved Off only after a fresh cycle proves every reading.
      return_cycles_=0;
      return true;
    }
    if (r.action=="cancel") {
      std::string summary=completed_summary();
      bool ec_incomplete=selected==3 && method=="ec_2" && step=="high" && completed==2;
      stop_queue();
      maintenance=recovery=true; armed=false; returning_=false;
      selected=-1; preview={}; method.clear(); step.clear();
      status="Maintenance";
      result="Session ended; accepted steps: "+summary+
             (ec_incomplete ? ". EC low accepted; high point and final verification incomplete." : ". Completed calibration remains stored.")+
             " Confirm return to pool";
      return save_intent(maintenance);
    }
    if (r.action=="configure_monitoring") {
      if (!r.confirmed || selected>=0 || !online[2] || !online[3] || logger_interval_<0 ||
          (reported_scale_!="?S,C" && reported_scale_!="?S,c" && reported_scale_!="?S,F" && reported_scale_!="?S,f" && reported_scale_!="?S,K" && reported_scale_!="?S,k"))
        return refuse("end calibration, query circuit settings, and confirm configuration");
      const auto fields=csv(reported_layout_);
      if (fields.empty() || fields[0]!="?O") return refuse("query EC outputs first");
      for (size_t i=1;i<fields.size();++i)
        if (fields[i]!="EC" && fields[i]!="TDS" && fields[i]!="S" && fields[i]!="SG") return refuse("invalid EC output query");
      bool missing[3]={true,true,true};
      for (const auto &f: fields) { if (f=="EC") missing[0]=false; if (f=="TDS") missing[1]=false; if (f=="S") missing[2]=false; }
      // Readiness can be cleared by unrelated RTD failures, including logging.
      const bool scale_matches=reported_scale_=="?S,C" || reported_scale_=="?S,c";
      if (scale_matches && logger_interval_==0 && !missing[0] && !missing[1] && !missing[2]) {
        result="Monitoring configuration already matches; no circuit write";
        return true;
      }
      stop_queue(); maintenance=true; armed=false; recovery=false;
      if (!save_intent(true)) return false;
      pending_=true;
      // Only this explicit action sends persistent setters, and only mismatches.
      if (!scale_matches) enqueue(2,Kind::WRITE_SCALE,"S,c");
      if (logger_interval_!=0) enqueue(2,Kind::WRITE_LOGGER,"D,0");
      const char *commands[]={"O,EC,1","O,TDS,1","O,S,1"};
      for (int i=0;i<3;++i) if (missing[i]) enqueue(3,Kind::WRITE_LAYOUT,commands[i]);
      enqueue(2,Kind::SCALE,"S,?"); enqueue(2,Kind::LOGGER,"D,?"); enqueue(3,Kind::LAYOUT,"O,?");
      enqueue(2,Kind::CONFIG_DONE,"");
      status="Checking monitoring configuration";
      return true;
    }
    if (r.action=="initialize_k1" || r.action=="tds_factor") {
      if (selected>=0 || !r.confirmed || !online[3]) return refuse("end calibration, check EC identity, and confirm configuration");
      if (r.action=="initialize_k1" && !std::isfinite(k)) return refuse("wait for EC K readback before configuring");
      if (r.action=="tds_factor" && (!std::isfinite(r.reference) || r.reference<0.01f || r.reference>1.0f)) return refuse("factor must be 0.01 to 1.00");
      if (r.action=="tds_factor" && !std::isfinite(tds_factor)) return refuse("wait for EC TDS factor readback before configuring");
      if ((r.action=="initialize_k1" && std::isfinite(k) && std::fabs(k-1)<0.001f) ||
          (r.action=="tds_factor" && std::isfinite(tds_factor) && std::fabs(tds_factor-r.reference)<0.001f)) {
        result="Configuration already matches; no circuit write";
        return true;
      }
      stop_queue(); maintenance=true; armed=false; recovery=false; pending_=true;
      reference=r.reference;
      status="Writing EC configuration";
      result=r.action=="initialize_k1" ? "K1.0 configuration pending" : "TDS factor "+decimal(reference)+" pending";
      if (!save_intent(true)) { pending_=false; return false; }
      enqueue(3,r.action=="initialize_k1" ? Kind::WRITE_K : Kind::WRITE_FACTOR,
              r.action=="initialize_k1" ? "K,1.0" : "TDS,"+decimal(reference));
      return true;
    }
    if (!maintenance || selected<0 || r.session!=session) return refuse("no matching session");
    if (r.action=="resume") {
      if (!r.confirmed) return refuse("acknowledge that an interrupted write may have completed");
      stop_queue(); recovery=false; armed=false; preview={};
      enqueue(selected,Kind::CAL_STATUS,"Cal,?");
      next_=now;
      result="Inspect circuit status and fresh reference readings; re-arm deliberately if another write is needed";
      return save_intent(maintenance);
    }
    if (recovery) return refuse("acknowledge recovery before re-arming");
    if (r.action=="clear") {
      if (!r.confirmed || r.step!=std::string("clear_")+NAME[selected]) return refuse("sensor-specific clear confirmation required");
      stop_queue(); armed=false; pending_=true; clearing_=true;
      result=std::string(NAME[selected])+" calibration clear pending";
      if (!save_intent(true)) { pending_=false; return false; }
      enqueue(selected,Kind::WRITE_CAL,"Cal,clear",900);
      return true;
    }
    if (r.action!="arm" && r.action!="apply") return refuse("unknown action");
    if (!r.confirmed || r.step!=step || step=="done" || !ready_[selected] || !preview.fresh(now,PREVIEW_MS))
      return refuse("confirm the expected step with a fresh valid preview");
    if (selected==0 && !ph_temperature_calibration_) return refuse("pH temperature-compensated calibration requires circuit firmware 2.13 or later");
    if (r.unit!=unit() || !valid_reference(r.reference)) return refuse("invalid reference, units, order, or EC K/output configuration");
    if (r.action=="arm") {
      reference=r.reference; armed=true; armed_at_=now;
      result=std::string(NAME[selected])+" "+step+" armed for "+decimal(reference)+" "+unit()+"; apply within 10 seconds";
      return true;
    }
    if (!armed || age(now,armed_at_)>=10000 || std::fabs(r.reference-reference)>0.0001f) return refuse("re-arm this exact reference");
    armed=false; pending_=true; clearing_=false; stop_queue();
    result=std::string(NAME[selected])+" "+step+" "+decimal(reference)+" "+unit()+" write pending; compensation "+decimal(compensation_c)+" C";
    last_calibration=result+"; boot "+boot.substr(0,8)+" at "+std::to_string(now/1000)+" s";
    status="Sending calibration";
    if (!save_intent(true)) { pending_=false; return false; }
    std::string command="Cal,";
    if (step=="dry") command+="dry";
    else if (step=="single") command+=decimal(reference);
    else command+=step+","+decimal(reference);
    // An apply can interrupt a preview compensation transaction. Reapply and
    // acknowledge it explicitly before the calibration write, even if its old
    // response was discarded while draining the bus.
    if (selected==0 || selected==3) enqueue(selected,Kind::COMP,"",300,true);
    enqueue(selected,Kind::WRITE_CAL,command,900);
    return true;
  }

  void tick(uint32_t now) {
    if (updating) return;
    if (armed && age(now,armed_at_)>=10000) { armed=false; result="Arm expired; inspect preview and arm again"; }
    for (auto &v: readings) if (!v.fresh(now)) v.valid=false;
    if (!preview.fresh(now,PREVIEW_MS)) preview.valid=false;
    if (active_) {
      if (!due(now,read_at_)) return;
      std::string payload;
      int code=io_.read(ADDRESS[current_.sensor],payload);
      if (code==254 && age(now,started_)<3500) { read_at_=now+100; return; }
      active_=false;
      if (discard_) { discard_=false; return; }
      if (code!=1) { fail(current_.sensor,code==254 ? "response deadline" : code==2 ? "syntax error" : code==255 ? "no data" : "I2C/frame error"); return; }
      complete(payload,now);
    }
    if (queue_.empty() && due(now,next_)) {
      if (!maintenance || returning_) normal_cycle(now);
      else if (selected>=0) preview_cycle(now);
      else {
        for (int s=0;s<4;++s) if (!ready_[s]) initialize(s);
        next_=now+5000;
      }
    }
    while (!queue_.empty() && !active_) {
      current_=queue_.front(); queue_.pop_front();
      int s=current_.sensor;
      if (current_.kind==Kind::CONFIG_DONE) {
        pending_=false; ready_={}; result="Monitoring settings verified; confirm probes are in pool water"; status="Maintenance"; continue;
      }
      if (current_.kind==Kind::READY) { ready_[s]=online[s] && init_ok_[s]; continue; }
      if (current_.kind==Kind::CYCLE_END) {
        if (returning_) {
          std::string stale;
          for (size_t i=0;i<readings.size();++i)
            if (!readings[i].fresh(now)) stale+=(stale.empty() ? "" : ", ")+std::string(READING_NAME[i]);
          if (stale.empty()) {
            returning_=false;
            // A failed save leaves maintenance and recovery set with an explanation.
            if (!save_intent(false)) continue;
            maintenance=recovery=false;
            result="Monitoring resumed after a fresh acquisition cycle";
            status="Monitoring";
          } else if (++return_cycles_>=RETURN_CYCLES) {
            returning_=false;
            result="Return failed: "+stale+" not fresh; inspect circuits and confirm return again";
            status="Maintenance";
          }
        } else if (!maintenance) status="Monitoring";
        continue;
      }
      const bool monitoring_write=current_.kind==Kind::WRITE_SCALE || current_.kind==Kind::WRITE_LOGGER || current_.kind==Kind::WRITE_LAYOUT;
      bool configuring=(pending_ && monitoring_write) || current_.kind==Kind::WRITE_K || current_.kind==Kind::CHECK_K ||
                       current_.kind==Kind::WRITE_FACTOR || current_.kind==Kind::CHECK_FACTOR;
      if (current_.kind!=Kind::ID && (!online[s] || (!init_ok_[s] && !configuring && current_.kind!=Kind::SCALE && current_.kind!=Kind::LOGGER && current_.kind!=Kind::LAYOUT && current_.kind!=Kind::CAL_STATUS && current_.kind!=Kind::K && current_.kind!=Kind::FACTOR))) {
        if (pending_) fail(s,"circuit unavailable or not initialized");
        continue;
      }
      if ((current_.kind==Kind::READ || current_.kind==Kind::VERIFY) &&
          (!ready_[s] || (s==2 && !scale_ok_) || ((s==0 || s==3) && !comp_ok_[s]))) {
        invalidate(s); if (pending_) fail(s,"verification prerequisites failed"); continue;
      }
      if (current_.kind==Kind::COMP) {
        if (s==3 && !ec_volatile_temperature_) { fail(s,"EC volatile compensation requires firmware 2.13 or later"); continue; }
        comp_ok_[s]=false;
        if (current_.preview && s==0 && buffer_rtd && !readings[2].fresh(now)) { fail(s,"buffer RTD unavailable"); continue; }
        compensation_assumed=!readings[2].fresh(now);
        compensation_c=compensation_assumed ? 25 : readings[2].value;
        if (current_.preview) {
          compensation_c=s==3 ? 25 : buffer_rtd ? readings[2].value : buffer_c;
          compensation_assumed=false;
        }
        current_.command="T,"+decimal(compensation_c);
      }
      if (monitoring_write || current_.kind==Kind::WRITE_K || current_.kind==Kind::WRITE_FACTOR) {
        // A setter may take effect even if its acknowledgment/readback is lost.
        // Clear only the affected cache and recover through read-only initialization.
        switch (current_.kind) {
          case Kind::WRITE_SCALE: reported_scale_.clear(); scale_ok_=false; break;
          case Kind::WRITE_LOGGER: logger_interval_=-1; break;
          case Kind::WRITE_LAYOUT: reported_layout_.clear(); layout={}; break;
          case Kind::WRITE_K: k=NAN; break;
          case Kind::WRITE_FACTOR: tds_factor=NAN; break;
          default: break;
        }
        ready_[s]=false;
      }
      started_=now; read_at_=now+current_.delay;
      started_time_=io_.measurement_time();
      if (!io_.write(ADDRESS[s],current_.command)) { fail(s,"I2C write failed; command outcome uncertain"); continue; }
      active_=true;
    }
  }

 private:
  IO &io_;
  std::array<bool,4> ready_{}, init_ok_{}, comp_ok_{};
  bool active_{false}, discard_{false}, pending_{false}, returning_{false}, scale_ok_{false}, clearing_{false};
  // Bounded return attempt; a transient bus error gets two more normal cycles.
  static constexpr uint32_t RETURN_CYCLES=3;
  uint32_t return_cycles_{0};
  bool ph_temperature_calibration_{false}, ec_volatile_temperature_{false};
  std::string reported_scale_, reported_layout_;
  int logger_interval_{-1};
  uint32_t next_{0}, started_{0}, read_at_{0}, armed_at_{0};
  MeasurementTime started_time_{};
  float mid_{7}, low_{0};
  std::string last_id_;
  std::deque<Op> queue_;
  Op current_{0,Kind::ID,""};
  void stop_queue() { queue_.clear(); discard_=active_; }
  bool saved_maintenance_{false};
  bool save_intent(bool intended) {
    if (configuration_ok && intended == saved_maintenance_) return true;
    MaintenanceSetting setting;
    setting.maintenance = intended;
    if (!io_.save(setting)) {
      configuration_ok=false; maintenance=recovery=true; armed=false; queue_.clear();
      status="Configuration save failed"; result="Maintenance intent was not saved; retry the deliberate action";
      return false;
    }
    configuration_ok=true;
    saved_maintenance_=intended;
    return true;
  }
  void enqueue(int s,Kind kind,const std::string &command,uint32_t delay=300,bool preview_mode=false) {
    if (queue_.size()>=48) { fail(s,"transaction queue full"); return; }
    queue_.push_back({s,kind,command,delay,preview_mode});
  }
  void initialize(int s) {
    init_ok_[s]=true; ready_[s]=false;
    enqueue(s,Kind::ID,"i"); enqueue(s,Kind::CAL_STATUS,"Cal,?");
    if (s==2) { scale_ok_=false; enqueue(s,Kind::SCALE,"S,?"); enqueue(s,Kind::LOGGER,"D,?"); }
    if (s==3) {
      layout={};
      enqueue(s,Kind::K,"K,?"); enqueue(s,Kind::FACTOR,"TDS,?");
      enqueue(s,Kind::LAYOUT,"O,?");
    }
    enqueue(s,Kind::READY,"");
  }
  void normal_cycle(uint32_t now) {
    next_=now+5000;
    for (int s=0;s<4;++s) if (!ready_[s]) initialize(s);
    enqueue(2,Kind::SCALE,"S,?"); enqueue(2,Kind::READ,"R",600);
    enqueue(0,Kind::COMP,""); enqueue(0,Kind::READ,"R",900);
    enqueue(1,Kind::READ,"R",900);
    enqueue(3,Kind::LAYOUT,"O,?"); enqueue(3,Kind::COMP,""); enqueue(3,Kind::READ,"R",600);
    enqueue(0,Kind::CYCLE_END,"");
  }
  void preview_cycle(uint32_t now) {
    next_=now+2000;
    if (!ready_[selected]) initialize(selected);
    if (selected==0 && buffer_rtd) {
      if (!ready_[2]) initialize(2);
      enqueue(2,Kind::SCALE,"S,?"); enqueue(2,Kind::READ,"R",600);
    }
    if (selected==2) enqueue(2,Kind::SCALE,"S,?");
    if (selected==3) enqueue(3,Kind::LAYOUT,"O,?");
    if (selected==0 || selected==3) enqueue(selected,Kind::COMP,"",300,true);
    enqueue(selected,Kind::READ,"R",selected<2 ? 900 : 600,true);
  }
  void invalidate(int s) {
    readings[s]={};
    if (s==3) { readings[4]={}; readings[5]={}; }
    if (selected==s) preview={};
  }
  void fail(int s,const std::string &why) {
    errors[s]=why; init_ok_[s]=ready_[s]=false; comp_ok_[s]=false;
    invalidate(s);
    if (s==2) scale_ok_=false;
    if (s==3) layout={};
    if (pending_) {
      queue_.clear(); pending_=false; maintenance=recovery=true; armed=false;
      result=std::string(NAME[s])+" outcome unknown or verification incomplete: "+why+"; inspect before re-arming";
      status="Recovery required";
    }
  }
  bool query_float(const std::string &p,const char *prefix,float &out) {
    auto fields=csv(p);
    return fields.size()==2 && fields[0]==prefix && numeric(fields[1],out);
  }
  bool valid_reference(float x) const {
    if (!std::isfinite(x)) return false;
    if (selected==0) return x>=0 && x<=14 && (step=="mid" ? x>=5.5f && x<=8.5f : step=="low" ? x<mid_ : x>mid_);
    if (selected==1) return x>=-1100 && x<=1100;
    if (selected==2) return x>=-40 && x<=125;
    return layout.valid() && std::isfinite(k) && std::fabs(k-1)<0.001f &&
           (step=="dry" ? x==0 : x>=5 && x<=200000 && (step!="high" || x>low_));
  }
  std::string completed_summary() const {
    if (selected<0 || completed==0) return "none in this session";
    std::string s=NAME[selected];
    if (selected==0) {
      s+=" midpoint";
      if (completed>=2) s+=method=="ph_2_high" ? ", high" : ", low";
      if (completed>=3) s+=", high";
    } else if (selected==3) {
      s+=" dry";
      if (completed>=2) s+=method=="ec_1" ? ", wet" : ", low";
      if (completed>=3) s+=", high";
    } else s+=" single point";
    return s;
  }
  bool measurement(const std::string &p,uint32_t now) {
    int s=current_.sensor;
    bool pv=current_.preview;
    auto fields=csv(p);
    if (s==3 && (!layout.valid() || int(fields.size())!=layout.count)) return false;
    if (s!=3 && fields.size()!=1) return false;
    auto reading=[&](int index,float lo,float hi) {
      float n;
      return index>=0 && index<int(fields.size()) && numeric(fields[index],n) && n>=lo && n<=hi ? Reading{n,started_,true,started_time_} : Reading{};
    };
    Reading v=s==0 ? reading(0,0,14) : s==1 ? reading(0,-1100,1100) : s==2 ? reading(0,-40,pv ? 125 : 80) : reading(layout.ec,0,2000000);
    if (pv) {
      preview_change=preview.fresh(now,PREVIEW_MS) && v.valid ? v.value-preview.value : NAN;
      preview=v;
    } else {
      // The buffer RTD is kept internally for compensation, never made available as pool data.
      readings[s]=v;
      if (s==3) { readings[4]=reading(layout.salt,0,42); readings[5]=reading(layout.tds,0,2000000); }
    }
    return v.valid || (s==3 && !pv); // individual EC fields may independently be unavailable
  }
  void advance() {
    ++completed;
    if (selected==0) {
      if (step=="mid") { mid_=reference; step=method=="ph_1" ? "done" : method=="ph_2_high" ? "high" : "low"; }
      else if (step=="low" && method=="ph_3") step="high";
      else step="done";
    } else if (selected==3) {
      if (step=="dry") step=method=="ec_1" ? "single" : "low";
      else if (step=="low") { low_=reference; step="high"; }
      else step="done";
    } else step="done";
  }
  void finish_point(uint32_t now) {
    last_calibration=result+"; boot "+boot.substr(0,8)+" at "+std::to_string(now/1000)+" s";
    pending_=false; advance(); status=step=="done" ? "Procedure complete; return required" : "Next reference: "+step;
    next_=now+1000;
  }
  void complete(const std::string &p,uint32_t now) {
    int s=current_.sensor;
    float n=NAN;
    bool ok=true;
    switch (current_.kind) {
      case Kind::ID: {
        auto f=csv(p);
        // Atlas documents lowercase ?i; retain compatibility with uppercase replies.
        ok=f.size()==3 && (f[0]=="?i" || f[0]=="?I") &&
           (f[1]==NAME[s] || (s==0 && f[1]=="PH")) && !f[2].empty();
        online[s]=ok;
        if (ok) identity[s]=f[1]+" "+f[2];
        if (s==3) ec_volatile_temperature_=ok && circuit_version_at_least(f[2],2,13);
        if (s==0) ph_temperature_calibration_=ok && circuit_version_at_least(f[2],2,13);
        break;
      }
      case Kind::CAL_STATUS:
      case Kind::CHECK_CAL:
        ok=(query_float(p,"?Cal",n) || query_float(p,"?CAL",n)) && n>=0 && n<=3 && std::floor(n)==n;
        if (ok) calibration[s]=int(n);
        if (ok && current_.kind==Kind::CHECK_CAL) {
          if (clearing_) {
            ok=n==0;
            if (ok) { pending_=false; recovery=true; completed=0; step="done"; result=std::string(NAME[s])+" calibration cleared and read back; end session before restarting"; last_calibration=result; status="Calibration cleared"; }
          } else if (s==3 && method=="ec_2" && step=="low") {
            // Atlas EC datasheet p. 70: Cal,low stages a reference without
            // changing readings. ACK + a valid status query permit the high
            // step; neither a point count nor agreement proves completion yet.
            ok=n<=2;
            if (ok) {
              result="EC low "+decimal(reference)+" uS/cm accepted; high point required before agreement verification; compensation "+decimal(compensation_c)+" C";
              finish_point(now);
            }
          } else {
            int minimum=s==0 ? (step=="mid" ? 1 : step=="high" && method=="ph_3" ? 3 : 2) : s==3 ? (step=="dry" ? 0 : step=="high" ? 2 : 1) : 1;
            ok=n>=minimum;
            if (ok) { status="Verifying reference reading"; enqueue(s,Kind::VERIFY,"R",s<2 ? 900 : 600,true); }
          }
        }
        break;
      case Kind::SCALE:
        reported_scale_=p;
        scale_ok_=ok=(p=="?S,C" || p=="?S,c");
        break;
      case Kind::LOGGER:
        if (query_float(p,"?D",n) && n>=0 && n<=32000 && std::floor(n)==n) logger_interval_=int(n);
        else logger_interval_=-1;
        ok=logger_interval_==0;
        break;
      case Kind::K:
      case Kind::CHECK_K:
        ok=query_float(p,"?K",n) && n>0;
        k=ok ? n : NAN;
        ok=ok && std::fabs(n-1)<0.001f;
        if (ok && current_.kind==Kind::CHECK_K) { pending_=false; ready_[3]=false; result="K1.0 verified; confirm return to pool"; status="Maintenance"; }
        break;
      case Kind::FACTOR:
      case Kind::CHECK_FACTOR:
        ok=query_float(p,"?TDS",n) && n>=0.01f && n<=1;
        if (current_.kind==Kind::CHECK_FACTOR) ok=ok && std::fabs(n-reference)<0.001f;
        tds_factor=ok ? n : NAN;
        if (ok && current_.kind==Kind::CHECK_FACTOR) { pending_=false; result="TDS factor "+decimal(n)+" verified; confirm return to pool"; status="Maintenance"; }
        break;
      case Kind::LAYOUT: reported_layout_=p; ok=layout.parse(p); break;
      case Kind::WRITE_SCALE:
      case Kind::WRITE_LOGGER:
      case Kind::WRITE_LAYOUT: ok=p.empty(); break;
      case Kind::COMP: comp_ok_[s]=ok=p.empty(); break;
      case Kind::WRITE_CAL:
        ok=p.empty();
        if (ok) { result+="; circuit accepted"; last_calibration=result+"; verification pending"; status="Checking calibration status"; enqueue(s,Kind::CHECK_CAL,"Cal,?"); }
        break;
      case Kind::WRITE_K:
      case Kind::WRITE_FACTOR:
        ok=p.empty();
        if (ok) enqueue(s,current_.kind==Kind::WRITE_K ? Kind::CHECK_K : Kind::CHECK_FACTOR,current_.kind==Kind::WRITE_K ? "K,?" : "TDS,?");
        break;
      case Kind::READ:
      case Kind::VERIFY:
        ok=measurement(p,now);
        if (ok && current_.kind==Kind::VERIFY) {
          float tolerance=s==0 ? 0.1f : s==1 ? 10.0f : s==2 ? 0.3f : std::fmax(5.0f,std::fabs(reference)*0.02f);
          ok=preview.valid && std::fabs(preview.value-reference)<=tolerance;
          if (ok) {
            result=std::string(NAME[s])+" "+step+" "+decimal(reference)+" "+unit()+" accepted and verified; compensation "+decimal(compensation_c)+" C";
            finish_point(now);
          }
        }
        break;
      default: break;
    }
    if (!ok) fail(s,"unexpected response or reference verification failed");
    else if (ready_[s] || current_.kind==Kind::ID) errors[s].clear();
  }
};
} // namespace atlas_pool
