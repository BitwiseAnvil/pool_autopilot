#pragma once
#include "core.h"

namespace atlas_pool {
struct NumberControl {
  const char *key, *name, *unit;
  float minimum, maximum, step, initial;
};
inline constexpr NumberControl NUMBER_CONTROLS[]={
  {"ph_mid","pH midpoint buffer","pH",5.5f,8.5f,0.01f,7},
  {"ph_low","pH low buffer","pH",0,7,0.01f,4},
  {"ph_high","pH high buffer","pH",7,14,0.01f,10},
  {"orp_reference","ORP reference","mV",-1100,1100,0.1f,225},
  {"ec_low","EC low or single wet reference","µS/cm",5,200000,1,12880},
  {"ec_high","EC high reference","µS/cm",5,200000,1,80000},
  {"rtd_reference","RTD reference temperature","°C",-40,125,0.01f,0},
  {"buffer_temperature","Measured pH buffer temperature","°C",0,60,0.1f,25},
  {"tds_factor","Proposed TDS factor","",0.01f,1,0.01f,0.54f},
};
struct NamedControl { const char *key, *name; };
inline constexpr NamedControl SWITCH_CONTROLS[]={
  {"reference_ready","Probe is in the reference and readings are stable"},
  {"probes_returned","All probes are back in pool water"},
  {"rtd_in_buffer","RTD is in the same pH buffer"},
  {"recovery_acknowledged","I have inspected the interrupted operation"},
  {"ec_config_confirmed","Confirm deliberate circuit configuration change"},
  {"clear_confirmed","Confirm clearing selected sensor calibration"},
};
inline constexpr NamedControl SELECT_CONTROLS[]={
  {"calibration_method","Calibration procedure"},
  {"clear_sensor","Sensor to clear"},
};
inline constexpr NamedControl BUTTON_CONTROLS[]={
  {"begin","Begin calibration"}, {"arm","Arm selected reference"},
  {"apply","Apply armed reference"}, {"cancel","End session and stay in maintenance"},
  {"return","Resume pool monitoring"}, {"resume","Recover interrupted session"},
  {"initialize_k1","Configure installed K1.0 probe"},
  {"configure_monitoring","Configure monitoring settings"},
  {"set_tds_factor","Write and verify TDS factor"}, {"clear","Clear selected sensor calibration"},
};
inline constexpr const char *METHOD_LABELS[]={"pH - 3 points","pH - midpoint only",
  "pH - midpoint and low","pH - midpoint and high","ORP - 225 mV","EC - dry, low, high",
  "EC - dry and one wet point","RTD - optional temperature calibration"};
inline constexpr const char *METHOD_VALUES[]={"ph_3","ph_1","ph_2_low","ph_2_high","orp","ec_2","ec_1","rtd"};
inline constexpr uint8_t NUMBER_COUNT=9, SWITCH_COUNT=6, SELECT_COUNT=2, BUTTON_COUNT=10;
inline constexpr uint8_t CONTROL_COUNT=NUMBER_COUNT+SWITCH_COUNT+SELECT_COUNT+BUTTON_COUNT;
struct ControlSettings {
  uint32_t magic{0x41544332}, version{2};
  std::array<float,NUMBER_COUNT> numbers{{7,4,10,225,12880,80000,0,25,0.54f}};
  bool valid() const {
    if (magic!=0x41544332 || version!=2) return false;
    for (size_t i=0;i<numbers.size();++i)
      if (!std::isfinite(numbers[i]) || numbers[i]<NUMBER_CONTROLS[i].minimum || numbers[i]>NUMBER_CONTROLS[i].maximum) return false;
    return true;
  }
};
struct ControlStorage {
  virtual ~ControlStorage() = default;
  virtual bool save_controls(const ControlSettings &) = 0;
};
class Controls {
 public:
  explicit Controls(ControlStorage &storage) : storage_(storage) {}
  ControlSettings settings;
  uint32_t method{0}, clear_sensor{0};
  std::array<bool,SWITCH_COUNT> switches{};

  void begin(Engine &e,const ControlSettings *saved) {
    if (saved && saved->valid()) settings=*saved;
    if (e.selected>=0) {
      for (uint32_t i=0;i<8;++i) if (e.method==METHOD_VALUES[i]) method=i;
      settings.numbers[7]=e.buffer_c;
      switches[2]=e.buffer_rtd;
    }
    sync(e);
  }
  void reset_confirmations() {
    for (size_t i=0;i<switches.size();++i) if (i!=2) switches[i]=false;
  }
  void sync(const Engine &e) {
    if (session_!=e.session || step_!=e.step || selected_!=e.selected ||
        (e.recovery && !recovery_) || (e.busy() && !busy_) || (!e.configuration_ok && configuration_ok_))
      reset_confirmations();
    session_=e.session; step_=e.step; selected_=e.selected;
    recovery_=e.recovery; busy_=e.busy();
    configuration_ok_=e.configuration_ok;
  }

  // Settings use the same boot/token/age/replay protection as calibration writes.
  // They only change local reference inputs; they never write an EZO circuit.
  bool set(Engine &e,const Request &r,const std::string &key,const std::string &value,uint32_t now) {
    if (!e.authorize(r,now)) return false;
    auto refuse=[&](const char *why) { e.result=std::string("Refused: ")+why; return false; };
    if (e.busy() || r.session!=e.session) return refuse("wait for the current operation and refresh controls");
    auto candidate=settings;
    bool persistent=false;
    for (size_t i=0;i<NUMBER_COUNT;++i) if (key==NUMBER_CONTROLS[i].key) {
      float number;
      if (!numeric(value,number) || number<NUMBER_CONTROLS[i].minimum || number>NUMBER_CONTROLS[i].maximum)
        return refuse("reference is outside the permitted range");
      if (i==7 && e.selected>=0) return refuse("end this session before changing buffer temperature");
      candidate.numbers[i]=number; persistent=true;
    }
    if (key=="calibration_method" || key=="clear_sensor") {
      if (key=="calibration_method" && e.selected>=0) return refuse("end this session before changing procedure");
      bool found=false;
      for (uint32_t i=0;i<(key=="calibration_method" ? 8u : 4u);++i) {
        if (value!=(key=="calibration_method" ? METHOD_LABELS[i] : NAME[i])) continue;
        (key=="calibration_method" ? method : clear_sensor)=i;
        found=true;
      }
      if (!found) return refuse("unknown selection");
      reset_confirmations(); e.armed=false; e.result="Procedure selection updated";
      return true;
    }
    if (persistent) {
      if (candidate.numbers == settings.numbers) return true;
      if (!storage_.save_controls(candidate)) return refuse("could not save reference settings; previous values kept");
      settings=candidate;
      e.armed=false; reset_confirmations();
      e.result="Reference setting saved; confirm the reference again before arming";
      return true;
    }
    for (size_t i=0;i<SWITCH_COUNT;++i) if (key==SWITCH_CONTROLS[i].key) {
      if (value!="ON" && value!="OFF") return refuse("confirmation must be ON or OFF");
      bool on=value=="ON";
      if (i==2 && e.selected>=0) return refuse("end this session before changing buffer compensation");
      if (on && i==0 && (e.selected<0 || e.recovery || !e.preview.fresh(now,PREVIEW_MS)))
        return refuse("wait for a fresh calibration preview before confirming the reference");
      if (on && (i==1 || i==3 || i==5) && !e.maintenance) return refuse("no maintenance session");
      switches[i]=on;
      if (i==0 && !on) e.armed=false;
      e.result="Confirmation updated";
      return true;
    }
    return refuse("unknown control");
  }

  bool press(Engine &e,Request r,uint32_t now) {
    std::string action=r.action;
    r.method=METHOD_VALUES[method];
    r.temperature=settings.numbers[7]; r.buffer_rtd=switches[2];
    r.unit=e.unit(); r.reference=0;
    if (e.selected==0) r.reference=settings.numbers[e.step=="mid" ? 0 : e.step=="low" ? 1 : 2];
    else if (e.selected==1) r.reference=settings.numbers[3];
    else if (e.selected==2) r.reference=settings.numbers[6];
    else if (e.selected==3 && e.step!="dry") r.reference=settings.numbers[e.step=="high" ? 5 : 4];
    r.confirmed=action=="begin" || action=="cancel";
    if (action=="arm" || action=="apply") r.confirmed=switches[0];
    if (action=="return") r.confirmed=switches[1];
    if (action=="resume") r.confirmed=switches[3];
    if (action=="initialize_k1" || action=="set_tds_factor" || action=="configure_monitoring") r.confirmed=switches[4];
    if (action=="set_tds_factor") { r.action="tds_factor"; r.reference=settings.numbers[8]; }
    if (action=="clear") { r.confirmed=switches[5]; r.step=std::string("clear_")+NAME[clear_sensor]; }
    bool accepted=e.request(r,now);
    if (accepted && action!="arm") reset_confirmations();
    return accepted;
  }
 private:
  ControlStorage &storage_;
  uint32_t session_{0};
  int selected_{-1};
  bool recovery_{false}, busy_{false}, configuration_ok_{true};
  std::string step_;
};
} // namespace atlas_pool
