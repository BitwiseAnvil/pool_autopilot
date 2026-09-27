#pragma once
#include "core.h"
#include "controls.h"
#include "esphome/core/component.h"
#include "esphome/components/pool_storage/pool_storage.h"
#include "esphome/components/i2c/i2c_bus.h"
#include "esphome/components/json/json_util.h"
#include "esphome/components/pool_clock/pool_clock.h"
#include <mqtt_client.h>
#include <mqtt5_client.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <atomic>

namespace esphome::atlas_pool_device {
class AtlasPool : public Component, public ::atlas_pool::IO, public ::atlas_pool::ControlStorage {
 public:
  AtlasPool() : engine(*this), controls_(*this) {}
  ::atlas_pool::Engine engine;
  void set_bus(i2c::I2CBus *bus) { bus_=bus; }
  void set_clock(pool_clock::PoolClock *clock) { clock_=clock; }
  void set_dashboard_path(std::string path) { dashboard_path_=std::move(path); }
  void set_mqtt(std::string host, uint16_t port, std::string user, std::string password, std::string device) {
    host_=std::move(host); port_=port; user_=std::move(user); password_=std::move(password); device_=std::move(device);
    base_="pool/"+device_;
  }
  void setup() override;
  void loop() override;
  void dump_config() override;
  // setup() starts ESP-MQTT: lwIP and the Wi-Fi interface must exist first.
  // This orders initialization without waiting for a network connection.
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }
  void on_shutdown() override;
  void ota_begin();
  void ota_error() { engine.ota_end_failed(); }
  bool write(uint8_t address,const std::string &command) override;
  int read(uint8_t address,std::string &response) override;
  bool save(const ::atlas_pool::MaintenanceSetting &setting) override;
  bool save_controls(const ::atlas_pool::ControlSettings &settings) override;
  ::atlas_pool::MeasurementTime measurement_time() override;

 protected:
  struct Incoming { char payload[768]; uint16_t size; bool retained; uint32_t epoch; };
  struct Snapshot {
    std::array<::atlas_pool::Reading,6> readings;
    bool maintenance;
    uint32_t at, epoch;
    char diagnostics[4096];
  };
  // Only the main loop produces snapshots; xQueueOverwrite copies this buffer.
  // Keep it off the 8 KiB loop stack, including nested encrypted OTA callbacks.
  Snapshot snapshot_buffer_{};
  ::atlas_pool::Controls controls_;
  i2c::I2CBus *bus_{nullptr};
  pool_clock::PoolClock *clock_{nullptr};
  esp_mqtt_client_handle_t client_{nullptr};
  QueueHandle_t incoming_{nullptr}, snapshots_{nullptr};
  SemaphoreHandle_t publish_mutex_{nullptr};
  std::string host_, user_, password_, device_, base_, dashboard_path_;
  uint16_t port_{1883};
  std::atomic<bool> connected_{false}, discovery_{false}, drop_{false}, shutting_down_{false};
  std::atomic<uint32_t> epoch_{0};
  std::atomic<uint32_t> publish_epoch_{0};
  std::string boot_id_;
  uint32_t handled_epoch_{0}, next_publish_{0};
  bool handled_connected_{false};
  Incoming assembling_{};
  size_t assembled_{0}, total_{0};
  bool assembling_command_{false};
  static void mqtt_event(void *,esp_event_base_t,int32_t,void *);
  static void network_task(void *);
  void handle_event(int32_t,esp_mqtt_event_handle_t);
  bool publish_discovery_(uint8_t index);
  bool publish_(const std::string &topic,const std::string &payload,bool retained=false,int qos=0);
  void snapshot_(uint32_t now);
  void request_(const Incoming &message,uint32_t now);
  int64_t utc_ms_() const;
  void timestamp_(JsonObject object) const;
};
} // namespace esphome::atlas_pool_device
