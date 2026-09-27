#pragma once
#include <cstddef>
#include <cstdint>

namespace pool_storage {
// No queue, checkpoint, or retry. Only an explicit configuration action calls save.
enum class LoadResult { FOUND, MISSING, ERROR };
LoadResult read(const char *key, void *data, size_t size);
bool load(const char *key, void *data, size_t size);
bool save(const char *key, const void *data, size_t size);
struct WriteCounts {
  uint32_t settings_writes{0}, settings_erases{0}, rejected{0};
  uint32_t installation_writes{0}, installation_erases{0};
};
WriteCounts counts();
}  // namespace pool_storage
