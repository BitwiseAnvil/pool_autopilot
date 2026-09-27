#pragma once
#include "controls.h"

namespace atlas_pool {
// The same portable builder is exercised against HA's installed MQTT schemas.
inline std::string json_string(const std::string &value) {
  std::string out="\"";
  for (unsigned char c:value) {
    if (c=='"' || c=='\\') { out+='\\'; out+=char(c); }
    else if (c<32) { char escape[7]; snprintf(escape,sizeof(escape),"\\u%04x",unsigned(c)); out+=escape; }
    else out+=char(c);
  }
  return out+'"';
}
struct DiscoveryJSON {
  std::string body;
  void raw(const std::string &key,const std::string &value) {
    if (!body.empty()) body+=',';
    body+=json_string(key)+':'+value;
  }
  void text(const std::string &key,const std::string &value) { raw(key,json_string(value)); }
  std::string str() const { return '{'+body+'}'; }
};
struct ControlDescriptor { std::string domain, key, name; int index; };
inline ControlDescriptor control_descriptor(uint8_t index) {
  if (index<NUMBER_COUNT) return {"number",NUMBER_CONTROLS[index].key,NUMBER_CONTROLS[index].name,index};
  index-=NUMBER_COUNT;
  if (index<SWITCH_COUNT) return {"switch",SWITCH_CONTROLS[index].key,SWITCH_CONTROLS[index].name,index};
  index-=SWITCH_COUNT;
  if (index<SELECT_COUNT) return {"select",SELECT_CONTROLS[index].key,SELECT_CONTROLS[index].name,index};
  index-=SELECT_COUNT;
  return {"button",BUTTON_CONTROLS[index].key,BUTTON_CONTROLS[index].name,index};
}
inline std::string control_command_template(const ControlDescriptor &c,const std::string &device) {
  // Locate the diagnostic entity by a device marker, not an editable entity ID.
  // This also works when HA assigns a suffix, or the user renames that entity.
  std::string prefix="{% set d = states.sensor | selectattr('attributes.atlas_device','defined') | "
    "selectattr('attributes.atlas_device','eq',"+json_string(device)+") | map(attribute='attributes') | first | default({}) %}";
  std::string fields="control=true,timestamp=utcnow().isoformat(),boot=d.get('boot',''),token=d.get('token',0),session=d.get('session',0),"
    "issued=d.get('uptime_ms',0),step=d.get('step',''),id=now().strftime('%Y%m%d%H%M%S%f') ~ '-' ~ (range(10000,99999)|random),";
  if (c.domain=="button") fields+="action="+json_string(c.key);
  else fields+="action='set',key="+json_string(c.key)+",value=value|string";
  return prefix+"{{ dict("+fields+") | to_json }}";
}
inline std::string control_discovery(uint8_t index,const std::string &device,const std::string &version,
                                     const std::string &dashboard_path) {
  const auto c=control_descriptor(index);
  const auto base="pool/"+device;
  DiscoveryJSON j,d;
  j.text("name",c.name); j.text("unique_id",device+"_control_"+c.key);
  j.text("default_entity_id",c.domain+".atlas_pool_"+c.key);
  j.text("entity_category","config"); j.text("availability_topic",base+"/availability");
  j.text("command_topic",base+"/command"); j.text("command_template",control_command_template(c,device));
  j.raw("qos","0"); j.raw("retain","false");
  if (c.domain!="button") {
    j.text("state_topic",base+"/diagnostics");
    j.text("value_template",c.domain=="switch" ? "{{ 'ON' if value_json.controls."+c.key+" else 'OFF' }}" :
      "{{ value_json.controls."+c.key+" }}");
    j.raw("optimistic","false");
  }
  if (c.domain=="number") {
    const auto &n=NUMBER_CONTROLS[c.index];
    j.raw("min",decimal(n.minimum)); j.raw("max",decimal(n.maximum)); j.raw("step",decimal(n.step));
    j.text("mode","box");
    if (*n.unit) j.text("unit_of_measurement",n.unit);
  } else if (c.domain=="select") {
    std::string options="[";
    for (int i=0;i<(c.index==0 ? 8 : 4);++i) {
      if (i) options+=',';
      options+=json_string(c.index==0 ? METHOD_LABELS[i] : NAME[i]);
    }
    j.raw("options",options+']');
  }
  d.raw("identifiers",'['+json_string(device)+']');
  d.text("name","Atlas Pool Kit"); d.text("manufacturer","Atlas Scientific");
  d.text("model","Wi-Fi Pool Kit ESP32-S3 TFT"); d.text("sw_version",version);
  if (!dashboard_path.empty()) d.text("configuration_url","homeassistant://"+dashboard_path);
  j.raw("device",d.str());
  return j.str();
}
} // namespace atlas_pool
