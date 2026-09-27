#pragma once

#include <cstring>
#include <cstdint>
#include <unordered_map>
#include <vector>

uint32_t millis();

namespace esphome {

inline uint32_t random_uint32() {
  static uint32_t value = 0;
  return ++value;
}

class TestPreferences;

class ESPPreferenceObject {
 public:
  ESPPreferenceObject() = default;
  ESPPreferenceObject(TestPreferences *owner, uint32_t key, size_t size)
      : owner_(owner), key_(key), size_(size) {}

  template<typename T> bool save(const T *src);
  template<typename T> bool load(T *dest);

 private:
  TestPreferences *owner_{nullptr};
  uint32_t key_{0};
  size_t size_{0};
};

class TestPreferences {
 public:
  template<typename T>
  ESPPreferenceObject make_preference(uint32_t key, bool) {
    return ESPPreferenceObject(this, key, sizeof(T));
  }

  bool sync() {
    if (fail_sync) {
      pending.clear();
      return false;
    }
    for (const auto &item : pending) storage[item.first] = item.second;
    pending.clear();
    return true;
  }

  void corrupt(uint32_t key) {
    auto it = storage.find(key);
    if (it != storage.end() && !it->second.empty()) it->second[0] ^= 0x01;
  }

  std::unordered_map<uint32_t, std::vector<uint8_t>> storage;
  std::unordered_map<uint32_t, std::vector<uint8_t>> pending;
  bool fail_save{false};
  uint32_t fail_save_key{0};
  bool fail_sync{false};

 private:
  friend class ESPPreferenceObject;
};

template<typename T> bool ESPPreferenceObject::save(const T *src) {
  if (owner_ == nullptr || owner_->fail_save || owner_->fail_save_key == key_ ||
      sizeof(T) != size_) return false;
  const auto *bytes = reinterpret_cast<const uint8_t *>(src);
  owner_->pending[key_] = std::vector<uint8_t>(bytes, bytes + sizeof(T));
  return true;
}

template<typename T> bool ESPPreferenceObject::load(T *dest) {
  if (owner_ == nullptr || sizeof(T) != size_) return false;
  const auto pending = owner_->pending.find(key_);
  const auto stored = owner_->storage.find(key_);
  const std::vector<uint8_t> *bytes = nullptr;
  if (pending != owner_->pending.end())
    bytes = &pending->second;
  else if (stored != owner_->storage.end())
    bytes = &stored->second;
  if (bytes == nullptr || bytes->size() != sizeof(T)) return false;
  std::memcpy(dest, bytes->data(), sizeof(T));
  return true;
}

inline TestPreferences *global_preferences = nullptr;

}  // namespace esphome
