#include "pool_storage.h"
#include "esphome/core/defines.h"
#include <esp_idf_version.h>
#include <atomic>
#include <cstring>
#include <vector>
#include <nvs.h>
#include <nvs_flash.h>
#include <esp_partition.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static_assert(ESP_IDF_VERSION == ESP_IDF_VERSION_VAL(5, 5, 5), "Re-audit vendor write paths after an SDK change");
#ifdef USE_ARDUINO
#include <esp_arduino_version.h>
static_assert(ESP_ARDUINO_VERSION == ESP_ARDUINO_VERSION_VAL(3, 3, 11), "Re-audit Arduino NVS initialization after an upgrade");
#endif

namespace pool_storage {
// Authorization belongs to the caller task, never to concurrent vendor tasks.
static std::atomic<TaskHandle_t> writer{nullptr};
static std::atomic<uint32_t> writes{0}, erases{0}, rejected{0}, ota_writes{0}, ota_erases{0};
LoadResult read(const char *key, void *data, size_t size) {
  nvs_handle_t handle;
  const auto opened = nvs_open("pool_settings", NVS_READONLY, &handle);
  if (opened == ESP_ERR_NVS_NOT_FOUND) return LoadResult::MISSING;
  if (opened != ESP_OK) return LoadResult::ERROR;
  size_t actual = size;
  const auto result = nvs_get_blob(handle, key, data, &actual);
  nvs_close(handle);
  if (result == ESP_ERR_NVS_NOT_FOUND) return LoadResult::MISSING;
  return result == ESP_OK && actual == size ? LoadResult::FOUND : LoadResult::ERROR;
}
bool load(const char *key, void *data, size_t size) {
  return read(key, data, size) == LoadResult::FOUND;
}
// NVS initialization can write page headers and power-loss repairs, so only a
// deliberate save may initialize storage while holding write permission. If
// initialization fails, preserve the partition for recovery; never erase it to
// make a settings save succeed.
static esp_err_t open_for_save(nvs_handle_t *handle) {
  auto result = nvs_open("pool_settings", NVS_READWRITE, handle);
  if (result != ESP_ERR_NVS_NOT_INITIALIZED) return result;
  result = nvs_flash_init();
  return result == ESP_OK ? nvs_open("pool_settings", NVS_READWRITE, handle) : result;
}
bool save(const char *key, const void *data, size_t size) {
  std::vector<uint8_t> previous(size);
  if (load(key, previous.data(), size) && std::memcmp(data, previous.data(), size) == 0) return true;
  TaskHandle_t empty = nullptr;
  if (!writer.compare_exchange_strong(empty, xTaskGetCurrentTaskHandle())) return false;
  nvs_handle_t handle;
  bool ok = false;
  if (open_for_save(&handle) == ESP_OK) {
    ok = nvs_set_blob(handle, key, data, size) == ESP_OK && nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
  }
  writer.store(nullptr);
  // A failed write is reported once; nothing queues or retries it later.
  return ok;
}
WriteCounts counts() { return {writes.load(), erases.load(), rejected.load(), ota_writes.load(), ota_erases.load()}; }
bool permit(const esp_partition_t *p, bool erase) {
  if (p->type == ESP_PARTITION_TYPE_DATA && p->subtype == ESP_PARTITION_SUBTYPE_DATA_NVS) {
    if (!writer.load() || writer.load() != xTaskGetCurrentTaskHandle()) { ++rejected; return false; }
    if (erase) ++erases; else ++writes;
    return true;
  }
  // Only OTA partitions are writable outside a deliberate settings operation.
  if (p->type == ESP_PARTITION_TYPE_APP ||
      (p->type == ESP_PARTITION_TYPE_DATA && p->subtype == ESP_PARTITION_SUBTYPE_DATA_OTA)) {
    if (erase) ++ota_erases; else ++ota_writes;
    return true;
  }
  ++rejected;
  return false;
}
}  // namespace pool_storage

extern "C" {
esp_err_t __real_esp_partition_write(const esp_partition_t *, size_t, const void *, size_t);
esp_err_t __real_esp_partition_write_raw(const esp_partition_t *, size_t, const void *, size_t);
esp_err_t __real_esp_partition_erase_range(const esp_partition_t *, size_t, size_t);
esp_err_t __wrap_esp_partition_write(const esp_partition_t *p, size_t off, const void *data, size_t n) {
  return pool_storage::permit(p, false) ? __real_esp_partition_write(p, off, data, n) : ESP_ERR_INVALID_STATE;
}
esp_err_t __wrap_esp_partition_write_raw(const esp_partition_t *p, size_t off, const void *data, size_t n) {
  return pool_storage::permit(p, false) ? __real_esp_partition_write_raw(p, off, data, n) : ESP_ERR_INVALID_STATE;
}
esp_err_t __wrap_esp_partition_erase_range(const esp_partition_t *p, size_t off, size_t n) {
  return pool_storage::permit(p, true) ? __real_esp_partition_erase_range(p, off, n) : ESP_ERR_INVALID_STATE;
}
}
