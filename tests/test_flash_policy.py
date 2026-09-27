"""Exercise the adapted ESPHome RTC/preferences implementation and flash guard.

Hardware calls are instrumented at the same partition entry points used by the
audited IDF NVS/PHY/Arduino sources. This does not claim physical flash testing.
"""
from hashlib import sha256
import importlib.util
from pathlib import Path
import subprocess
import tempfile

import esphome

ROOT = Path(__file__).resolve().parents[1]
ESPHOME = Path(esphome.__file__).parent
spec = importlib.util.spec_from_file_location("storage_adapter", ROOT / "components/pool_storage/__init__.py")
adapter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(adapter)
upstream = (ESPHOME / "components/esp32/preferences.cpp").read_text()
adapted = adapter.adapt_preferences(upstream)
assert "nvs_flash_erase(" not in adapted and "NVS_READWRITE" not in adapted
assert "s_pending_save.push_back" not in adapted and "nvs_set_blob(" not in adapted
try:
    adapter.adapt_preferences(upstream + "\n")
except ValueError:
    pass
else:
    raise AssertionError("Upstream drift did not stop the build")

SDK = Path.home() / ".cache/esphome/idf/frameworks/5.5.5"
if not SDK.is_dir():
    raise SystemExit(f"ESP-IDF sources not found at {SDK}. Run "
                     "./tools/esphome compile atlas-pool-kit/atlas-pool-kit.yaml once "
                     "so ESPHome downloads them, then rerun the tests.")
partition = (SDK / "components/nvs_flash/src/nvs_partition.cpp").read_text()
for symbol in ("esp_partition_write(", "esp_partition_write_raw(", "esp_partition_erase_range("):
    assert symbol in partition
assert "esp_flash_write(" not in partition
wifi = (ESPHOME / "components/wifi/wifi_component_esp_idf.cpp").read_text()
assert "wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT()" in wifi
assert "esp_wifi_set_storage(WIFI_STORAGE_RAM)" in wifi

FAKE = r'''
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cinttypes>
#include <memory>
#include <map>
#include <string>
#include <vector>
#ifndef USE_ESP32
#define USE_ESP32
#endif
#define USE_ESP32_RTC_PREFERENCES_STORAGE
#define SOC_RTC_MEM_SUPPORTED 1
#define RTC_NOINIT_ATTR
#define ESP_IDF_VERSION_VAL(a,b,c) (((a)<<16)|((b)<<8)|(c))
#define ESP_IDF_VERSION ESP_IDF_VERSION_VAL(5,5,5)
using esp_err_t=int;
using nvs_handle_t=uint32_t;
using TaskHandle_t=void *;
inline TaskHandle_t current_task=reinterpret_cast<void *>(1);
inline TaskHandle_t xTaskGetCurrentTaskHandle() { return current_task; }
constexpr int ESP_OK=0, ESP_ERR_INVALID_STATE=1, NVS_READONLY=0, NVS_READWRITE=1;
constexpr int ESP_ERR_NVS_NOT_FOUND=2, ESP_ERR_NVS_NOT_INITIALIZED=4;
constexpr int ESP_ERR_NVS_NO_FREE_PAGES=5, ESP_ERR_NVS_NEW_VERSION_FOUND=6;
constexpr int ESP_PARTITION_TYPE_APP=0, ESP_PARTITION_TYPE_DATA=1;
constexpr int ESP_PARTITION_SUBTYPE_DATA_NVS=2, ESP_PARTITION_SUBTYPE_DATA_OTA=0;
struct esp_partition_t { int type, subtype; };
inline esp_partition_t nvs_partition{1,2}, app_partition{0,0};
extern "C" esp_err_t __wrap_esp_partition_write(const esp_partition_t *,size_t,const void *,size_t);
extern "C" esp_err_t __wrap_esp_partition_write_raw(const esp_partition_t *,size_t,const void *,size_t);
extern "C" esp_err_t __wrap_esp_partition_erase_range(const esp_partition_t *,size_t,size_t);
inline unsigned physical_writes=0, physical_erases=0, rw_opens=0, ro_opens=0;
inline bool fail_write=false, concurrent_vendor=false;
// BLANK needs a page-header or repair write; FULL/NEWER must preserve stored data.
enum class Nvs { READY, BLANK, FULL, NEWER };
inline Nvs nvs=Nvs::READY;
inline std::map<std::string,std::vector<uint8_t>> stored;
inline int nvs_open(const char *,int mode,nvs_handle_t *h) {
  if(nvs!=Nvs::READY) return ESP_ERR_NVS_NOT_INITIALIZED;
  *h=mode+1; if(mode) ++rw_opens; else ++ro_opens; return 0; }
inline void nvs_close(nvs_handle_t) {}
inline int nvs_flash_init() {
  if(nvs==Nvs::FULL) return ESP_ERR_NVS_NO_FREE_PAGES;
  if(nvs==Nvs::NEWER) return ESP_ERR_NVS_NEW_VERSION_FOUND;
  const uint8_t header=0;
  if(nvs==Nvs::BLANK && __wrap_esp_partition_write(&nvs_partition,0,&header,1)) return ESP_ERR_INVALID_STATE;
  nvs=Nvs::READY; return 0;
}
inline int nvs_flash_erase() {
  if(__wrap_esp_partition_erase_range(&nvs_partition,0,4096)) return ESP_ERR_INVALID_STATE;
  stored.clear(); nvs=Nvs::BLANK; return 0;
}
inline int nvs_get_blob(nvs_handle_t,const char *key,void *data,size_t *size) {
  auto it=stored.find(key); if(it==stored.end()) return 2;
  if(data) { if(*size<it->second.size()) return 3; std::memcpy(data,it->second.data(),it->second.size()); }
  *size=it->second.size(); return 0;
}
inline int nvs_set_blob(nvs_handle_t handle,const char *key,const void *data,size_t size) {
  if(handle!=2) return 3;
  if(concurrent_vendor) {
    current_task=reinterpret_cast<void *>(2);
    auto result=__wrap_esp_partition_write(&nvs_partition,0,data,size);
    if(result==0) std::abort();
    current_task=reinterpret_cast<void *>(1);
  }
  const int result=__wrap_esp_partition_write(&nvs_partition,0,data,size);
  if(result) return result;
  auto bytes=static_cast<const uint8_t *>(data); stored[key]={bytes,bytes+size}; return 0;
}
inline int nvs_commit(nvs_handle_t) { return 0; }
inline const char *esp_err_to_name(int) { return "instrumented"; }
template<class... T> void fake_log(T...) {}
#define ESP_LOGVV(...) fake_log(__VA_ARGS__)
#define ESP_LOGV(...) fake_log(__VA_ARGS__)
#define ESP_LOGD(...) fake_log(__VA_ARGS__)
#define ESP_LOGW(...) fake_log(__VA_ARGS__)
#define ESP_LOGE(...) fake_log(__VA_ARGS__)
namespace esphome {
constexpr size_t UINT32_MAX_STR_SIZE=11;
inline void uint32_to_str(char *s,uint32_t v) { std::snprintf(s,11,"%u",v); }
template<size_t N> struct SmallInlineBuffer {
  std::vector<uint8_t> bytes;
  void set(const uint8_t *p,size_t n) { bytes.assign(p,p+n); }
  size_t size() const { return bytes.size(); }
  const uint8_t *data() const { return bytes.data(); }
};
template<size_t N> struct SmallBufferWithHeapFallback {
  std::vector<uint8_t> bytes;
  explicit SmallBufferWithHeapFallback(size_t n):bytes(n) {}
  uint8_t *get() { return bytes.data(); }
};
namespace esp32 {
struct ESP32PreferenceBackend {
  uint32_t nvs_handle{0}, key{0}; bool in_flash{true}; uint16_t rtc_offset{0}; uint8_t length_words{0};
  bool save(const uint8_t *,size_t); bool load(uint8_t *,size_t);
};
}
struct ESPPreferenceObject {
  std::shared_ptr<esp32::ESP32PreferenceBackend> backend;
  ESPPreferenceObject()=default;
  explicit ESPPreferenceObject(esp32::ESP32PreferenceBackend *b):backend(b) {}
  template<class T> bool save(const T *v) { return backend && backend->save(reinterpret_cast<const uint8_t *>(v),sizeof(T)); }
  template<class T> bool load(T *v) { return backend && backend->load(reinterpret_cast<uint8_t *>(v),sizeof(T)); }
};
namespace esp32 {
struct NVSData;
struct ESP32Preferences {
  uint32_t nvs_handle{0}; uint16_t current_rtc_offset_{0};
  void open(); bool sync(); bool reset();
  ESPPreferenceObject make_preference(size_t,uint32_t,bool);
  ESPPreferenceObject make_preference(size_t,uint32_t);
  ESPPreferenceObject make_rtc_preference_(size_t,uint32_t);
  ESP32PreferenceBackend make_backend_(uint32_t) const;
  bool load_from_key(uint32_t,uint8_t *,size_t);
  bool is_changed_(uint32_t,const NVSData &,const char *);
};
}
using ESPPreferences=esp32::ESP32Preferences;
extern ESPPreferences *global_preferences;
}
'''
TEST = r'''
#include "fake_sdk.h"
#include "pool_storage.h"
#include <cassert>
#include <iostream>
extern "C" int __real_esp_partition_write(const esp_partition_t *,size_t,const void *,size_t) {
  if(fail_write) return 9; ++physical_writes; return 0;
}
extern "C" int __real_esp_partition_write_raw(const esp_partition_t *p,size_t off,const void *data,size_t size) {
  return __real_esp_partition_write(p,off,data,size);
}
extern "C" int __real_esp_partition_erase_range(const esp_partition_t *,size_t,size_t) { ++physical_erases; return 0; }
int main() {
  esphome::esp32::ESP32Preferences preferences;
  preferences.open(); assert(ro_opens==1 && rw_opens==0);
  auto counter=preferences.make_preference(sizeof(uint32_t),123,false);
  for(uint32_t boots=1;boots<=10000;++boots) { assert(counter.save(&boots)); assert(preferences.sync()); }
  esphome::esp32::ESP32Preferences restarted;
  restarted.open(); auto restored=restarted.make_preference(sizeof(uint32_t),123,false);
  uint32_t value=0; assert(restored.load(&value) && value==10000);
  assert(physical_writes==0 && physical_erases==0 && rw_opens==0);
  auto flash=preferences.make_preference(sizeof(value),555,true);
  for(int i=0;i<100;++i) { assert(!flash.save(&value)); assert(preferences.sync()); }
  assert(!preferences.reset() && physical_writes==0 && physical_erases==0);
  assert(pool_storage::read("missing",&value,sizeof(value))==pool_storage::LoadResult::MISSING);
  stored["invalid"]={1,2};
  assert(pool_storage::read("invalid",&value,sizeof(value))==pool_storage::LoadResult::ERROR);
  assert(pool_storage::save("deliberate",&value,sizeof(value)));
  assert(physical_writes==1 && rw_opens==1);
  assert(pool_storage::save("deliberate",&value,sizeof(value)) && physical_writes==1 && rw_opens==1);
  concurrent_vendor=true; ++value;
  assert(pool_storage::save("deliberate",&value,sizeof(value)));
  assert(pool_storage::counts().rejected==1 && physical_writes==2);
  concurrent_vendor=false;
  assert(__wrap_esp_partition_write_raw(&nvs_partition,0,&value,sizeof(value))!=0);
  // Boot never writes: blank, damaged or full NVS stays uninitialized and unreadable.
  auto before=physical_writes;
  nvs=Nvs::BLANK; restarted.open(); assert(nvs==Nvs::BLANK && physical_writes==before);
  assert(pool_storage::read("deliberate",&value,sizeof(value))==pool_storage::LoadResult::ERROR);
  nvs=Nvs::FULL; restarted.open(); assert(nvs==Nvs::FULL && physical_erases==0);
  // A deliberate save initializes blank or repaired NVS without erasing.
  nvs=Nvs::BLANK; stored.clear();
  assert(pool_storage::save("deliberate",&value,sizeof(value)));
  assert(nvs==Nvs::READY && physical_writes==before+2 && physical_erases==0);
  assert(pool_storage::read("deliberate",&value,sizeof(value))==pool_storage::LoadResult::FOUND);
  // A failed initialization must preserve every existing setting, including
  // values outside the record being saved and obsolete records used by rollback.
  const uint32_t calibration=900, references=712880;
  assert(pool_storage::save("doser_v2",&calibration,sizeof(calibration)));
  assert(pool_storage::save("atlas_refs_v2",&references,sizeof(references)));
  stored["old_journal"]={1,2,3,4};
  const auto preserved=stored;
  const auto saved_writes=physical_writes;
  for (auto state: {Nvs::FULL,Nvs::NEWER}) {
    nvs=state;
    restarted.open();
    assert(pool_storage::read("doser_v2",&value,sizeof(value))==pool_storage::LoadResult::ERROR);
    for (uint32_t proposed: {calibration,calibration+1}) {
      assert(!pool_storage::save("doser_v2",&proposed,sizeof(proposed)));
      assert(nvs==state && stored==preserved);
      assert(physical_writes==saved_writes && physical_erases==0);
      assert(pool_storage::counts().settings_erases==0);
    }
    for(int i=0;i<100;++i) preferences.sync();
    assert(nvs==state && stored==preserved && physical_erases==0);
  }
  // Once storage is readable again, unchanged values load without rewriting.
  nvs=Nvs::READY;
  uint32_t restored_value=0;
  assert(pool_storage::read("doser_v2",&restored_value,sizeof(restored_value))==pool_storage::LoadResult::FOUND && restored_value==calibration);
  assert(pool_storage::save("doser_v2",&calibration,sizeof(calibration)));
  assert(pool_storage::read("atlas_refs_v2",&restored_value,sizeof(restored_value))==pool_storage::LoadResult::FOUND && restored_value==references);
  assert(stored==preserved && physical_writes==saved_writes && physical_erases==0);
  // A failed initialization is not retried until the next deliberate save.
  nvs=Nvs::BLANK; fail_write=true; ++value;
  assert(!pool_storage::save("deliberate",&value,sizeof(value)) && nvs==Nvs::BLANK);
  for(int i=0;i<100;++i) preferences.sync();
  assert(nvs==Nvs::BLANK); fail_write=false; nvs=Nvs::READY;
  fail_write=true; ++value;
  assert(!pool_storage::save("deliberate",&value,sizeof(value)));
  const auto attempts=pool_storage::counts().settings_writes;
  for(int i=0;i<100;++i) preferences.sync();
  assert(pool_storage::counts().settings_writes==attempts);
  fail_write=false;
  assert(__wrap_esp_partition_write(&app_partition,0,&value,sizeof(value))==0);
  assert(pool_storage::counts().installation_writes==1);
  std::cout << "Adapted ESPHome preferences/RTC + IDF partition guards: 10000 boots, denied runtime/vendor writes, deliberate saves, non-destructive NVS initialization and calibration preservation passed\n";
}
'''

with tempfile.TemporaryDirectory(prefix="pool-flash-policy-") as directory:
    work = Path(directory)
    (work / "fake_sdk.h").write_text(FAKE)
    for name in ("preferences.h", "nvs.h", "nvs_flash.h", "esp_attr.h", "esp_partition.h", "esp_idf_version.h",
                 "soc/soc_caps.h", "freertos/FreeRTOS.h", "freertos/task.h", "esphome/core/defines.h",
                 "esphome/core/helpers.h", "esphome/core/log.h"):
        path = work / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('#include "fake_sdk.h"\n')
    (work / "esphome/core/preferences_rtc.h").write_text((ESPHOME / "core/preferences_rtc.h").read_text())
    (work / "preferences.cpp").write_text(adapted)
    (work / "test.cpp").write_text(TEST)
    executable = work / "test"
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-misleading-indentation", "-Wno-unused-parameter",
                    "-DUSE_ESP32", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                    f"-I{work}", f"-I{ROOT / 'components/pool_storage'}",
                    str(work / "test.cpp"), str(work / "preferences.cpp"),
                    str(ROOT / "components/pool_storage/pool_storage.cpp"), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
print("Audited IDF NVS partition paths and Wi-Fi RAM storage initialization: source checks passed")
