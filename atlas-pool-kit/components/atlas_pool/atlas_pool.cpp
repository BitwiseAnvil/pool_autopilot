#include "atlas_pool.h"
#include "discovery.h"
#include "control_discovery.h"
#include "reading_discovery.h"
#include "utc.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/core/version.h"
#include "esphome/components/wifi/wifi_component.h"
#include <esp_netif.h>
#include <esp_random.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <ctime>

namespace esphome::atlas_pool_device {
static const char *const TAG="atlas_pool";
using ::atlas_pool::iso8601;
int64_t AtlasPool::utc_ms_() const {
  const auto clock=clock_->sample();
  return clock.healthy ? clock.utc_ms : 0;
}
::atlas_pool::MeasurementTime AtlasPool::measurement_time() {
  const auto clock=clock_->sample();
  return {clock.healthy ? clock.utc_ms : 0, clock.generation};
}
void AtlasPool::timestamp_(JsonObject object) const {
  const auto clock=clock_->sample();
  auto timestamp=iso8601(clock.healthy ? clock.utc_ms : 0);
  if (timestamp.empty()) object["timestamp"]=nullptr;
  else object["timestamp"]=timestamp;
  if (clock.healthy) object["timestamp_ms"]=clock.utc_ms;
  else object["timestamp_ms"]=nullptr;
  object["time_source"]="time.nist.gov";
  object["clock_healthy"]=clock.healthy;
  object["clock_epoch"]=clock.generation;
  if (clock.synchronized) object["clock_sync_age_s"]=clock.sync_age_ms/1000;
  else object["clock_sync_age_s"]=nullptr;
  object["boot"]=boot_id_;
}
bool AtlasPool::save(const ::atlas_pool::MaintenanceSetting &setting) {
  return pool_storage::save("atlas_intent_v1", &setting, sizeof(setting));
}
bool AtlasPool::save_controls(const ::atlas_pool::ControlSettings &settings) {
  return pool_storage::save("atlas_refs_v2", &settings, sizeof(settings));
}
bool AtlasPool::write(uint8_t address,const std::string &command) {
  return bus_->write(address,reinterpret_cast<const uint8_t *>(command.data()),command.size())==i2c::NO_ERROR;
}
int AtlasPool::read(uint8_t address,std::string &response) {
  uint8_t data[64]{};
  if (bus_->read(address,data,sizeof(data))!=i2c::NO_ERROR) return -1;
  if (data[0]!=1) return data[0];
  const auto *end=static_cast<const uint8_t *>(memchr(data+1,0,sizeof(data)-1));
  if (!end) return -1;
  response.assign(reinterpret_cast<char *>(data+1),end-data-1);
  return 1;
}
void AtlasPool::setup() {
  ::atlas_pool::MaintenanceSetting saved;
  const auto loaded=pool_storage::read("atlas_intent_v1", &saved, sizeof(saved));
  if (loaded==pool_storage::LoadResult::ERROR) saved.magic=0;
  char boot[33];
  snprintf(boot,sizeof(boot),"%08lx%08lx%08lx%08lx",(unsigned long)esp_random(),(unsigned long)esp_random(),
           (unsigned long)esp_random(),(unsigned long)esp_random());
  engine.begin(millis(),boot,loaded==pool_storage::LoadResult::MISSING ? nullptr : &saved);
  ::atlas_pool::ControlSettings settings;
  bool controls_valid=pool_storage::load("atlas_refs_v2", &settings, sizeof(settings)) && settings.valid();
  controls_.begin(engine,controls_valid ? &settings : nullptr);
  boot_id_=boot;
  incoming_=xQueueCreate(8,sizeof(Incoming));
  snapshots_=xQueueCreate(1,sizeof(Snapshot));
  publish_mutex_=xSemaphoreCreateMutex();
  if (!incoming_ || !snapshots_ || !publish_mutex_) { ESP_LOGE(TAG,"MQTT queues unavailable; local engine continues"); return; }
  esp_mqtt_client_config_t config{};
  config.broker.address.hostname=host_.c_str();
  config.broker.address.port=port_;
  config.broker.address.transport=MQTT_TRANSPORT_OVER_TCP;
  config.credentials.client_id=device_.c_str();
  config.credentials.username=user_.c_str();
  config.credentials.authentication.password=password_.c_str();
  std::string will=base_+"/availability";
  config.session.last_will.topic=will.c_str();
  config.session.last_will.msg="offline";
  config.session.last_will.qos=1;
  config.session.last_will.retain=true;
  config.session.keepalive=15;
  config.session.protocol_ver=MQTT_PROTOCOL_V_5;
  config.network.timeout_ms=2000;
  config.network.reconnect_timeout_ms=5000;
  config.buffer.size=4096;
  config.outbox.limit=8192;
  client_=esp_mqtt_client_init(&config); // IDF copies configuration strings.
  if (!client_ || esp_mqtt_client_register_event(client_,MQTT_EVENT_ANY,mqtt_event,this)!=ESP_OK ||
      xTaskCreate(network_task,"atlas_mqtt",16384,this,1,nullptr)!=pdPASS) {
    ESP_LOGE(TAG,"MQTT initialization failed; local engine continues");
    return;
  }
  if (esp_mqtt_client_start(client_)!=ESP_OK) ESP_LOGE(TAG,"MQTT start failed; local engine continues");
}
void AtlasPool::dump_config() {
  ESP_LOGCONFIG(TAG,"Atlas Pool Kit: four EZO circuits, six readings, serialized calibration");
  ESP_LOGCONFIG(TAG,"MQTT device: %s, broker: %s:%u",device_.c_str(),host_.c_str(),port_);
  ESP_LOGCONFIG(TAG,"OTA and local sampling are independent of MQTT");
}
void AtlasPool::mqtt_event(void *self,esp_event_base_t,int32_t id,void *event) {
  static_cast<AtlasPool *>(self)->handle_event(id,static_cast<esp_mqtt_event_handle_t>(event));
}
void AtlasPool::handle_event(int32_t id,esp_mqtt_event_handle_t event) {
  if (id==MQTT_EVENT_CONNECTED) {
    ++epoch_; connected_=true; discovery_=true;
    // Preserve RETAIN even on live publications, not only broker replay.
    esp_mqtt5_subscribe_property_config_t subscription{};
    subscription.retain_as_published_flag=true;
    esp_mqtt5_client_set_subscribe_property(client_,&subscription);
    esp_mqtt_client_subscribe(client_,(base_+"/command").c_str(),0);
    esp_mqtt_client_subscribe(client_,"homeassistant/status",0);
  } else if (id==MQTT_EVENT_DISCONNECTED) {
    connected_=false; ++epoch_; assembled_=total_=0; assembling_command_=false;
  } else if (id==MQTT_EVENT_DATA) {
    if (event->current_data_offset==0) {
      std::string topic(event->topic,event->topic_len);
      if (topic=="homeassistant/status") { discovery_=true; return; }
      assembling_command_=topic==base_+"/command";
      assembled_=0; total_=event->total_data_len;
      assembling_={}; assembling_.retained=event->retain; assembling_.epoch=epoch_.load();
      if (total_==0 || total_>=sizeof(assembling_.payload)) { assembling_command_=false; drop_=true; return; }
    }
    if (!assembling_command_) return;
    if (size_t(event->current_data_offset)!=assembled_ || event->data_len<0 || assembled_+event->data_len>total_) {
      assembling_command_=false; drop_=true; return;
    }
    memcpy(assembling_.payload+assembled_,event->data,event->data_len);
    assembled_+=event->data_len;
    if (assembled_==total_) {
      assembling_.size=total_; assembling_.payload[total_]=0;
      if (xQueueSend(incoming_,&assembling_,0)!=pdTRUE) drop_=true;
      assembling_command_=false;
    }
  }
}
void AtlasPool::request_(const Incoming &message,uint32_t now) {
  if (message.epoch!=epoch_.load() || !connected_) return;
  ::atlas_pool::Request r;
  bool native_control=false;
  std::string control_key, control_value;
  r.retained=message.retained;
  bool parsed=json::parse_json(reinterpret_cast<const uint8_t *>(message.payload),message.size,[&](JsonObject root) {
    for (const char *key: {"boot","id","action"}) if (!root[key].is<const char *>()) return false;
    for (const char *key: {"token","session","issued"}) if (!root[key].is<uint32_t>()) return false;
    r.boot=root["boot"].as<std::string>(); r.id=root["id"].as<std::string>(); r.action=root["action"].as<std::string>();
    r.token=root["token"].as<uint32_t>(); r.session=root["session"].as<uint32_t>(); r.issued=root["issued"].as<uint32_t>();
    r.method=root["method"] | ""; r.step=root["step"] | ""; r.unit=root["unit"] | "";
    r.reference=root["reference"].is<float>() ? root["reference"].as<float>() : NAN;
    r.temperature=root["temperature"].is<float>() ? root["temperature"].as<float>() : 25;
    r.confirmed=root["confirmed"].is<bool>() && root["confirmed"].as<bool>();
    r.buffer_rtd=root["buffer_rtd"].is<bool>() && root["buffer_rtd"].as<bool>();
    native_control=root["control"].is<bool>() && root["control"].as<bool>();
    if (native_control && r.action=="set") {
      if (!root["key"].is<const char *>() || !root["value"].is<const char *>()) return false;
      control_key=root["key"].as<std::string>(); control_value=root["value"].as<std::string>();
      if (control_key.size()>32 || control_value.size()>80) return false;
    }
    return r.boot.size()<=32 && r.action.size()<=24 && r.method.size()<=16 && r.step.size()<=16 && r.unit.size()<=8;
  });
  ++publish_epoch_;
  if (!parsed) { engine.result="Refused: malformed command envelope"; return; }
  bool accepted=native_control ? (r.action=="set" ? controls_.set(engine,r,control_key,control_value,now) :
    controls_.press(engine,r,now)) : engine.request(r,now);
  if (!native_control && accepted && r.action!="arm") controls_.reset_confirmations();
  ESP_LOGI(TAG,"Control %s: %s",r.action.c_str(),accepted ? "accepted" : "refused");
}
void AtlasPool::loop() {
  uint32_t now=millis();
  uint32_t epoch=epoch_.load();
  bool connected=connected_.load();
  if (epoch!=handled_epoch_ || connected!=handled_connected_) {
    handled_epoch_=epoch; handled_connected_=connected;
    ++publish_epoch_;
    engine.interrupt(connected ? "MQTT reconnection" : "MQTT disconnect");
    controls_.reset_confirmations();
    next_publish_=now;
  }
  if (drop_.exchange(false)) { ++publish_epoch_; engine.interrupt("command queue overflow or oversized message"); }
  Incoming incoming;
  if (incoming_ && xQueueReceive(incoming_,&incoming,0)==pdTRUE) {
    request_(incoming,now);
    next_publish_=now;
  }
  engine.tick(now);
  controls_.sync(engine);
  if (::atlas_pool::due(now,next_publish_)) { snapshot_(now); next_publish_=now+1000; }
}
void AtlasPool::snapshot_(uint32_t now) {
  if (!snapshots_) return;
  Snapshot &s=snapshot_buffer_;
  s.readings=engine.readings; s.maintenance=engine.maintenance; s.at=now; s.epoch=publish_epoch_.load();
  auto payload=json::build_json([&](JsonObject j) {
    timestamp_(j);
    j["atlas_device"]=device_;
    j["boot"]=engine.boot; j["token"]=engine.token; j["session"]=engine.session; j["uptime_ms"]=now;
    j["maintenance"]=engine.maintenance; j["recovery"]=engine.recovery; j["armed"]=engine.armed;
    j["diagnostic_version"]=2; j["busy"]=engine.busy(); j["configuration_ok"]=engine.configuration_ok;
    j["sensor"]=engine.sensor_name(); j["method"]=engine.method; j["step"]=engine.step;
    j["status"]=engine.status; j["result"]=engine.result; j["request_id"]=engine.request_id;
    j["last_calibration"]=engine.last_calibration;
    j["completed"]=engine.completed; j["reference"]=engine.reference;
    j["buffer_c"]=engine.buffer_c; j["buffer_rtd"]=engine.buffer_rtd;
    j["preview"]=engine.preview.valid ? engine.preview.value : NAN;
    j["preview_age_s"]=engine.preview.valid ? float(::atlas_pool::age(now,engine.preview.at))/1000 : NAN;
    j["preview_change"]=engine.preview_change; j["preview_unit"]=engine.unit();
    j["compensation_c"]=engine.compensation_c;
    j["compensation_source"]=engine.maintenance && engine.selected>=0 ? "calibration reference" : engine.compensation_assumed ? "assumed 25 C" : "local RTD";
    j["rtd_age_s"]=engine.readings[2].valid ? float(::atlas_pool::age(now,engine.readings[2].at))/1000 : NAN;
    j["k"]=engine.k; j["tds_factor"]=engine.tds_factor;
    auto slope=j["ph_slope"].to<JsonObject>();
    slope["acid"]=engine.ph_slope[0]; slope["base"]=engine.ph_slope[1]; slope["offset_mv"]=engine.ph_slope[2];
    j["firmware"]="atlas-pool-kit/1.2.0"; j["esphome"]=ESPHOME_VERSION;
    j["reset_reason"]=int(esp_reset_reason());
    char ip[network::IP_ADDRESS_BUFFER_SIZE];
    wifi::global_wifi_component->get_ip_addresses()[0].str_to(ip);
    j["ip"]=ip;
    // Read the actual DHCP interface identity, not just the configured mDNS name.
    const char *hostname=nullptr;
    auto *netif=wifi::global_wifi_component->get_esp_netif_sta();
    if (netif && esp_netif_get_hostname(netif,&hostname)==ESP_OK)
      j["hostname"]=hostname;
    else j["hostname"]=nullptr;
#ifdef USE_MDNS
    j["mdns_enabled"]=true;
#else
    j["mdns_enabled"]=false;
#endif
    j["rssi"]=wifi::global_wifi_component->wifi_rssi();
    j["uptime_s"]=now/1000;
    auto controls=j["controls"].to<JsonObject>();
    for (size_t i=0;i<::atlas_pool::NUMBER_COUNT;++i)
      controls[::atlas_pool::NUMBER_CONTROLS[i].key]=controls_.settings.numbers[i];
    for (size_t i=0;i<::atlas_pool::SWITCH_COUNT;++i)
      controls[::atlas_pool::SWITCH_CONTROLS[i].key]=controls_.switches[i];
    controls["calibration_method"]=::atlas_pool::METHOD_LABELS[controls_.method];
    controls["clear_sensor"]=::atlas_pool::NAME[controls_.clear_sensor];
    auto circuits=j["circuits"].to<JsonObject>();
    for (int i=0;i<4;++i) {
      auto c=circuits[::atlas_pool::NAME[i]].to<JsonObject>();
      c["identity"]=engine.identity[i]; c["calibration"]=engine.calibration[i]; c["error"]=engine.errors[i];
      c["valid"]=engine.available(i,now);
    }
  });
  if (payload.size()>=sizeof(s.diagnostics)) { ESP_LOGE(TAG,"Diagnostic payload too large"); return; }
  memcpy(s.diagnostics,payload.c_str(),payload.size()+1);
  xQueueOverwrite(snapshots_,&s);
}
bool AtlasPool::publish_(const std::string &topic,const std::string &payload,bool retained,int qos) {
  if (!connected_ || xSemaphoreTake(publish_mutex_,pdMS_TO_TICKS(2100))!=pdTRUE) return false;
  // Properties carry date/time on discovery and availability too, without
  // adding unsupported keys to Home Assistant's discovery JSON schema.
  std::string timestamp=iso8601(utc_ms_());
  std::string uptime=std::to_string(esp_timer_get_time()/1000);
  esp_mqtt5_user_property_item_t items[]={{"timestamp",timestamp.c_str()},
    {"time_source","time.nist.gov"},
    {"boot",boot_id_.c_str()},{"uptime_ms",uptime.c_str()}};
  mqtt5_user_property_handle_t properties=nullptr;
  esp_mqtt5_publish_property_config_t options{};
  bool ok=esp_mqtt5_client_set_user_property(&properties,items,4)==ESP_OK;
  if (ok) {
    options.user_property=properties;
    // Expire live telemetry at the broker too; receiver checks remain decisive.
    if (!retained) options.message_expiry_interval=10;
    ok=esp_mqtt5_client_set_publish_property(client_,&options)==ESP_OK &&
       esp_mqtt_client_publish(client_,topic.c_str(),payload.c_str(),payload.size(),qos,retained)>=0;
  }
  esp_mqtt5_client_delete_user_property(properties);
  xSemaphoreGive(publish_mutex_);
  return ok;
}
bool AtlasPool::publish_discovery_(uint8_t index) {
  using Sequence=::atlas_pool::DiscoverySequence;
  auto device=[&](JsonObject j) {
    auto d=j["device"].to<JsonObject>();
    d["identifiers"].to<JsonArray>().add(device_);
    d["name"]="Atlas Pool Kit"; d["manufacturer"]="Atlas Scientific";
    d["model"]="Wi-Fi Pool Kit ESP32-S3 TFT"; d["sw_version"]="ESPHome " ESPHOME_VERSION " / Atlas 1.2.0";
    if (!dashboard_path_.empty()) d["configuration_url"]="homeassistant://"+dashboard_path_;
  };
  if (index<Sequence::READING_COUNT) {
    int i=index;
    std::string key=::atlas_pool::KEY[i];
    auto data=::atlas_pool::reading_discovery(index,device_,"ESPHome " ESPHOME_VERSION " / Atlas 1.2.0",dashboard_path_);
    return publish_("homeassistant/sensor/"+device_+"/"+key+"/config",data.c_str(),true,1);
  }
  // Discovery command templates read the session context from this diagnostic entity.
  const char *keys[]={"calibration","preview","preview_age","compensation","tds_factor","k","firmware","ip","rssi","uptime","reset_reason","ph_calibration","orp_calibration","rtd_calibration","ec_calibration","last_calibration","control_result","ph_acid_slope","ph_base_slope","ph_zero_offset"};
  const char *titles[]={"Calibration","Calibration Preview","Preview Age","Compensation Temperature","TDS Factor","EC Probe K","Firmware","IP Address","Wi-Fi Signal","Uptime","Restart Reason","pH Calibration Points","ORP Calibration Points","RTD Calibration Points","EC Wet Calibration Points","Last Calibration Result","Calibration Control Result","pH Acid Slope","pH Base Slope","pH Zero Offset"};
  const char *slope_fields[]={"acid","base","offset_mv"};
  const char *fields[]={"status","preview","preview_age_s","compensation_c","tds_factor","k","firmware","ip","rssi","uptime_s","reset_reason",nullptr,nullptr,nullptr,nullptr};
  if (index<Sequence::MAINTENANCE) {
    int i=index-Sequence::READING_COUNT;
    std::string key=keys[i];
    auto data=json::build_json([&](JsonObject j) {
      j["name"]=titles[i]; j["unique_id"]=device_+"_"+key; j["default_entity_id"]="sensor.atlas_pool_"+key;
      if (i!=16) j["entity_category"]="diagnostic";
      j["state_topic"]=base_+"/diagnostics";
      j["value_template"]=i>=17 ? std::string("{{ value_json.ph_slope.")+slope_fields[i-17]+" }}" : i==16 ? std::string("{{ value_json.result }}") : i==15 ? std::string("{{ value_json.last_calibration }}") : i<11 ? std::string("{{ value_json.")+fields[i]+" }}" : std::string("{{ value_json.circuits['")+::atlas_pool::NAME[i-11]+"'].calibration }}";
      if (i==0) j["json_attributes_topic"]=base_+"/diagnostics";
      if (i==2 || i==9) j["unit_of_measurement"]="s";
      if (i==3) j["unit_of_measurement"]="°C";
      if (i==8) { j["unit_of_measurement"]="dBm"; j["device_class"]="signal_strength"; }
      if (i>=17) { j["unit_of_measurement"]=i==19 ? "mV" : "%"; j["suggested_display_precision"]=i==19 ? 2 : 1; }
      j["availability_topic"]=base_+"/availability"; j["expire_after"]=30;
      device(j);
    });
    return publish_("homeassistant/sensor/"+device_+"/"+key+"/config",data.c_str(),true,1);
  }
  if (index==Sequence::MAINTENANCE) {
    auto maintenance=json::build_json([&](JsonObject j) {
      j["name"]="Maintenance"; j["unique_id"]=device_+"_maintenance"; j["default_entity_id"]="binary_sensor.atlas_pool_maintenance";
      j["state_topic"]=base_+"/diagnostics"; j["value_template"]="{{ 'ON' if value_json.maintenance else 'OFF' }}";
      j["availability_topic"]=base_+"/availability";
      j["expire_after"]=30; device(j);
    });
    return publish_("homeassistant/binary_sensor/"+device_+"/maintenance/config",maintenance.c_str(),true,1);
  }
  if (index>=Sequence::CONTROLS && index<Sequence::AVAILABILITY) {
    uint8_t control=index-Sequence::CONTROLS;
    auto descriptor=::atlas_pool::control_descriptor(control);
    auto payload=::atlas_pool::control_discovery(control,device_,"ESPHome " ESPHOME_VERSION " / Atlas 1.2.0",dashboard_path_);
    return publish_("homeassistant/"+descriptor.domain+"/"+device_+"/"+descriptor.key+"/config",payload,true,1);
  }
  return index==Sequence::AVAILABILITY && publish_(base_+"/availability","online",true,1);
}
void AtlasPool::network_task(void *arg) {
  auto *self=static_cast<AtlasPool *>(arg);
  Snapshot s;
  std::array<::atlas_pool::PublicationCursor,6> published{};
  ::atlas_pool::DiscoverySequence discovery;
  for (;;) {
    if (self->connected_ && !self->shutting_down_) {
      if (self->discovery_.exchange(false)) discovery.request();
      if (discovery.poll([&](uint8_t index) {
            return self->connected_ && !self->shutting_down_ && self->publish_discovery_(index);
          },[] { return millis(); }))
        published={}; // A restarted HA needs a current sample after discovery succeeds.
    }
    if (xQueueReceive(self->snapshots_,&s,pdMS_TO_TICKS(100))!=pdTRUE) continue;
    if (!self->connected_ || self->shutting_down_ || s.epoch!=self->publish_epoch_.load()) continue;
    // QoS 0, no enqueue/outbox: replace a snapshot while disconnected or publishing discovery.
    uint32_t now=millis();
    if (::atlas_pool::age(now,s.at)>2000) continue;
    auto state=json::build_json([&](JsonObject j) {
      self->timestamp_(j);
      auto measured=j["measured_at_ms"].to<JsonObject>();
      const auto clock=self->clock_->sample();
      for (int i=0;i<6;++i) {
        float n=s.readings[i].value;
        if (i==2) n=n*1.8f+32;
        if (i==4) n*=1000;
        const auto &r=s.readings[i];
        const bool valid=!s.maintenance && r.fresh(now) && clock.healthy &&
          r.measured.epoch==clock.generation && ::pool_clock_core::fresh(clock.utc_ms,r.measured.utc_ms);
        j[::atlas_pool::KEY[i]]=valid ? n : NAN;
        if (valid) measured[::atlas_pool::KEY[i]]=r.measured.utc_ms;
        else measured[::atlas_pool::KEY[i]]=nullptr;
      }
    });
    if (s.epoch!=self->publish_epoch_.load()) continue;
    self->publish_(self->base_+"/state",state.c_str());
    for (int i=0;i<6;++i) {
      now=millis();
      if (::atlas_pool::age(now,s.at)>2000 || s.epoch!=self->publish_epoch_.load()) break;
      auto decision=published[i].decide(s.readings[i],s.maintenance,now);
      const auto clock=self->clock_->sample();
      const auto &r=s.readings[i];
      if (!clock.healthy || r.measured.epoch!=clock.generation ||
          !::pool_clock_core::fresh(clock.utc_ms,r.measured.utc_ms))
        decision=!published[i].initialized || published[i].valid ?
          ::atlas_pool::PublicationCursor::Decision::UNAVAILABLE : ::atlas_pool::PublicationCursor::Decision::SKIP;
      if (decision==::atlas_pool::PublicationCursor::Decision::SKIP) continue;
      auto reading=json::build_json([&](JsonObject j) {
        self->timestamp_(j);
        float n=s.readings[i].value;
        if (i==2) n=n*1.8f+32;
        if (i==4) { n*=1000; j["unit"]="ppm"; }
        j["value"]=decision==::atlas_pool::PublicationCursor::Decision::VALUE ? n : NAN;
        if (decision==::atlas_pool::PublicationCursor::Decision::VALUE) j["measured_at_ms"]=r.measured.utc_ms;
        else j["measured_at_ms"]=nullptr;
        j["clock_epoch"]=r.measured.epoch;
      });
      if (self->publish_(self->base_+"/reading/"+::atlas_pool::KEY[i],reading.c_str()))
        published[i].sent(decision,s.readings[i]);
    }
    if (::atlas_pool::age(millis(),s.at)<=2000 && s.epoch==self->publish_epoch_.load())
      self->publish_(self->base_+"/diagnostics",s.diagnostics);
  }
}
void AtlasPool::ota_begin() {
  ++publish_epoch_;
  engine.ota_begin();
  snapshot_(millis());
}
void AtlasPool::on_shutdown() {
  shutting_down_=true;
  ++publish_epoch_;
  engine.interrupt("shutdown");
  // The broker's LWT covers power loss. Orderly disconnect must publish offline.
  if (client_ && connected_) publish_(base_+"/availability","offline",true,1);
}
} // namespace esphome::atlas_pool_device
