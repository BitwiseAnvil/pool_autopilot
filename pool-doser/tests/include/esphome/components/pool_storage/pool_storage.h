#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>
namespace pool_storage {
inline std::map<std::string, std::vector<uint8_t>> data;
inline unsigned saves{0};
inline bool fail{false};
inline bool load(const char *key, void *value, size_t size) {
  const auto it = data.find(key);
  if (it == data.end() || it->second.size() != size) return false;
  std::memcpy(value, it->second.data(), size);
  return true;
}
inline bool save(const char *key, const void *value, size_t size) {
  ++saves;
  if (fail) return false;
  const auto *bytes = static_cast<const uint8_t *>(value);
  data[key] = {bytes, bytes + size};
  return true;
}
}
