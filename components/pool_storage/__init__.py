"""ESPHome 2026.9.0 adaptation; only generated, project-local sources are edited."""
from hashlib import sha256
from pathlib import Path

from esphome import writer
from esphome.const import __version__
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components.esp32 import add_idf_sdkconfig_option
from esphome.core import CORE

DEPENDENCIES = ["esp32"]
CONFIG_SCHEMA = cv.Schema({})
PREFERENCES_SHA256 = "c9e26028ac205ad0ea9fe62fa3d89789b6ccfc01ac098d2270fa1032c544f9b3"


def replace_function(source, signature, body):
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[:brace + 1] + "\n" + body + "\n}" + source[end:]


def adapt_preferences(source):
    if sha256(source.encode()).hexdigest() != PREFERENCES_SHA256:
        raise ValueError("Pool storage: upstream preferences changed; audit before building")
    source = replace_function(source, "void ESP32Preferences::open()", """  // Initialization is guarded at the partition boundary, including NVS repair.
  s_open_err = nvs_flash_init();
  this->nvs_handle = 0;
  if (s_open_err == ESP_OK)
    s_open_err = nvs_open("esphome", NVS_READONLY, &this->nvs_handle);""")
    source = replace_function(source, "bool ESP32PreferenceBackend::save(", """#ifdef USE_ESP32_RTC_PREFERENCES_STORAGE
  if (!this->in_flash)
    return save_to_rtc(this->rtc_offset, this->key, this->length_words, data, len);
#endif
  // Flash settings use pool_storage::save synchronously from deliberate actions.
  // Framework runtime callers cannot enqueue work for a later preferences flush.
  ESP_LOGE(TAG, "Unapproved flash preference save rejected");
  return false;""")
    source = replace_function(source, "bool ESP32Preferences::sync()", "  return true;")
    source = replace_function(source, "bool ESP32Preferences::reset()", "  return false;")
    return source.replace(" - erased NVS", " - read-only NVS")


async def to_code(config):
    if __version__ != "2026.9.0":
        raise cv.Invalid("Pool storage requires the audited ESPHome 2026.9.0")
    add_idf_sdkconfig_option("CONFIG_ESP_WIFI_NVS_ENABLED", False)
    add_idf_sdkconfig_option("CONFIG_ESP_PHY_CALIBRATION_AND_DATA_STORAGE", False)
    add_idf_sdkconfig_option("CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH", False)
    add_idf_sdkconfig_option("CONFIG_ESP_COREDUMP_ENABLE_TO_NONE", True)
    for symbol in ("esp_partition_write", "esp_partition_write_raw", "esp_partition_erase_range"):
        cg.add_build_flag(f"-Wl,--wrap={symbol}")
    # copy_src_tree always copies the unmodified installed source first. The
    # hash rejects drift; the shared Python installation and SDK stay untouched.
    if not getattr(writer.copy_src_tree, "pool_storage_adapter", False):
        original = writer.copy_src_tree

        def copy_with_storage_policy():
            original()
            target = Path(CORE.relative_src_path("esphome/components/esp32/preferences.cpp"))
            target.write_text(adapt_preferences(target.read_text()))

        copy_with_storage_policy.pool_storage_adapter = True
        writer.copy_src_tree = copy_with_storage_policy
