#pragma once
#include "control_discovery.h"

namespace atlas_pool {
inline std::string live_reading_template(bool availability) {
  const std::string guard =
    "{% set m = value_json.get('measured_at_ms') %}"
    "{% set n = as_timestamp(utcnow()) * 1000 %}"
    "{% set fresh = m is number and -2000 <= n-m < 20000 and "
    "value_json.get('clock_healthy') == true and value_json.get('time_source') == 'time.nist.gov' "
    "and value_json.get('value') is number %}";
  return guard + (availability ? "{{ 'online' if fresh else 'offline' }}" :
    "{{ value_json.value if fresh else 'None' }}");
}

inline std::string reading_discovery(uint8_t index, const std::string &device,
                                    const std::string &version, const std::string &dashboard) {
  const char *names[]={"pH","ORP","Temperature","Conductivity","Salinity","TDS"};
  const char *units[]={"pH","mV","°F","µS/cm","ppm","ppm"};
  const std::string key=KEY[index], base="pool/"+device;
  DiscoveryJSON j, d, online, reading;
  j.text("name",names[index]); j.text("unique_id",device+"_"+key);
  j.text("default_entity_id","sensor.atlas_pool_"+key);
  j.text("state_topic",base+"/live/"+key);
  j.text("value_template",live_reading_template(false));
  j.text("unit_of_measurement",units[index]); j.text("state_class","measurement");
  j.raw("suggested_display_precision",index==0 || index==2 ? "2" : "0");
  if (index==2) j.text("device_class","temperature");
  online.text("topic",base+"/availability");
  reading.text("topic",base+"/live/"+key);
  reading.text("value_template",live_reading_template(true));
  j.raw("availability","["+online.str()+","+reading.str()+"]");
  j.text("availability_mode","all"); j.raw("expire_after","20"); j.raw("qos","0");
  d.raw("identifiers","["+json_string(device)+"]"); d.text("name","Atlas Pool Kit");
  d.text("manufacturer","Atlas Scientific"); d.text("model","Wi-Fi Pool Kit ESP32-S3 TFT");
  d.text("sw_version",version);
  if (!dashboard.empty()) d.text("configuration_url","homeassistant://"+dashboard);
  j.raw("device",d.str());
  return j.str();
}
}  // namespace atlas_pool
