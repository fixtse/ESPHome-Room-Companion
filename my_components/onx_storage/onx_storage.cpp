#include "onx_storage.h"

#include <array>
#include <algorithm>
#include <cstring>
#include <memory>
#include <new>

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#ifdef USE_ESP32
#include "esp_err.h"
#include "nvs.h"
#endif

#if defined(ENABLE_AUTHCODE_EFUSE) && defined(USE_ESP32)
#include "esp_efuse.h"
#include "esp_efuse_table.h"
#endif

namespace esphome::onx_storage {

static const char *const TAG = "onx_storage";

namespace {


#if defined(ENABLE_AUTHCODE_EFUSE) && defined(USE_ESP32)
static const esp_efuse_desc_t EFUSE_AUTH_CODE_DESC[] = {
    {EFUSE_BLK9, 0, 128}, // Custom secure version
};
static const esp_efuse_desc_t* ESP_EFUSE_AUTH_CODE[] = {
    &EFUSE_AUTH_CODE_DESC[0],
    NULL
};

/*
 * 比较两个内存区域
 * 返回值: 0=相等, >0=第一个不匹配的字节位置(1-based)
 */
static size_t find_first_difference(const void *a, const void *b, size_t size) {
    const unsigned char *p1 = (const unsigned char *)a;
    const unsigned char *p2 = (const unsigned char *)b;
    for (size_t i = 0; i < size; i++) {
        if (p1[i] != p2[i]) {
            return i + 1;  // 返回1-based位置
        }
    }
    return 0;  // 没有差异
}

static esp_err_t read_auth_code(unsigned char *auth_code) {
    esp_err_t err = esp_efuse_read_field_blob(ESP_EFUSE_AUTH_CODE, auth_code, 128);
    if (err != ESP_OK) return err;
    return ESP_OK;
}

static esp_err_t write_auth_code(unsigned char *auth_code, const esp_efuse_desc_t* field[], uint16_t num) {
    const esp_efuse_coding_scheme_t coding_scheme_for_batch_mode = EFUSE_CODING_SCHEME_RS;
    esp_efuse_coding_scheme_t coding_scheme = esp_efuse_get_coding_scheme(EFUSE_BLK9);

    if (coding_scheme == coding_scheme_for_batch_mode) {
        esp_efuse_batch_write_begin();
    }
    esp_err_t ret = esp_efuse_write_field_blob(field, auth_code, num);

    if (coding_scheme == coding_scheme_for_batch_mode) {
        esp_efuse_batch_write_commit();
    }

    return ret;
}

static bool write_auth_code_check(unsigned char *write_auth_code_ptr) {
    unsigned char read_auth_code_pt[16] = {0};
    char zero_array[16] = {0};
    if (read_auth_code(read_auth_code_pt) != ESP_OK) {
        return false;
    }
    if (memcmp(read_auth_code_pt, zero_array, 16) == 0) {
        if (write_auth_code(write_auth_code_ptr, ESP_EFUSE_AUTH_CODE, 128) != ESP_OK) return false;
        read_auth_code(read_auth_code_pt);
        if (memcmp(read_auth_code_pt, write_auth_code_ptr, 16) != 0)
            return false;
        else
            return true;
    }
    uint8_t ret = 0;
    ret = find_first_difference(read_auth_code_pt, write_auth_code_ptr, 16);
    if (ret != 0) {
        if (read_auth_code_pt[ret - 1] != 0) {
            return false; // efuse内已为1，auth_code有冲突
        } else {
            esp_efuse_desc_t temp_AUTH_CODE[] = {
                {EFUSE_BLK9, static_cast<int>((ret - 1) * 8), static_cast<int>((16 - ret + 1) * 8)}, // Custom secure version
            };
            const esp_efuse_desc_t* ESP_EFUSE_TEMP_AUTH_CODE[] = {
                &temp_AUTH_CODE[0], // 保持原样
                NULL
            };
            if (write_auth_code((write_auth_code_ptr + ret - 1), ESP_EFUSE_TEMP_AUTH_CODE, ((16 - ret + 1) * 8)) != ESP_OK) {
                return false;
            }
            read_auth_code(read_auth_code_pt);
            if (memcmp(read_auth_code_pt, write_auth_code_ptr, 16) != 0)
                return false;
            else
                return true;
        }
    }
    return true; // Already same
}
#endif

static bool hex_to_bytes(const std::string &hex, uint8_t *bytes, size_t len) {
  if (hex.length() != len * 2) return false;
  for (size_t i = 0; i < len; i++) {
    std::string byteString = hex.substr(i * 2, 2);
    char *endptr;
    bytes[i] = (uint8_t) strtol(byteString.c_str(), &endptr, 16);
    if (*endptr != '\0') return false;
  }
  return true;
}

static std::string bytes_to_hex(const uint8_t *bytes, size_t len) {
  std::string hex;
  char buf[3];
  for (size_t i = 0; i < len; i++) {
    snprintf(buf, sizeof(buf), "%02X", bytes[i]);
    hex += buf;
  }
  return hex;
}

static const char *const DEFAULT_FACTORY_SSID = "8DB0839D";
static const char *const DEFAULT_FACTORY_PASSWORD = "094FAFE8";
static constexpr size_t STORED_FACTORY_SSID_MAX_LENGTH = 64;
static constexpr size_t STORED_FACTORY_PASSWORD_MAX_LENGTH = 64;
static constexpr size_t STORED_AUTH_CODE_MAX_LENGTH = 32;
static constexpr size_t STORED_AUTH_SHA256_MAX_LENGTH = 64;

static const uint32_t FACTORY_WIFI_PREF_KEY = fnv1_hash("onx-storage-factory-wifi");
static const uint32_t AUTH_RECORD_PREF_KEY = fnv1_hash("onx-storage-auth-record");
static const uint32_t POWER_POLICY_PREF_KEY = fnv1_hash("onx-storage-power-policy-v1");
static const char *const NORMAL_UI_CFG_V4_KEY = "onx_ui_cfg_v4";
static const char *const NORMAL_UI_ST_V5_KEY = "onx_ui_st_v5";
static constexpr uint32_t NORMAL_UI_CFG_V4_MAGIC = 0x4F4E5844;  // ONXD
static constexpr uint32_t NORMAL_UI_ST_V5_MAGIC = 0x4F4E5853;   // ONXS
static constexpr uint32_t POWER_POLICY_MAGIC = 0x4F4E5850;  // ONXP
static constexpr size_t NORMAL_UI_DEVICE_MAX_COUNT = 250;
static constexpr size_t NORMAL_UI_TITLE_MAX_LENGTH = 128;
static constexpr size_t NORMAL_UI_ICON_MAX_LENGTH = 16;
static constexpr size_t NORMAL_UI_ENTITY_ID_MAX_LENGTH = 128;

struct StoredFactoryWifi {
  char ssid[STORED_FACTORY_SSID_MAX_LENGTH + 1];
  char password[STORED_FACTORY_PASSWORD_MAX_LENGTH + 1];
};

struct StoredAuthRecord {
  char auth_code[STORED_AUTH_CODE_MAX_LENGTH + 1];
  char auth_sha256[STORED_AUTH_SHA256_MAX_LENGTH + 1];
};

struct StoredPowerPolicy {
  uint32_t magic;
  uint8_t sleep_mode_enabled;
  uint8_t reserved0;
  uint16_t reserved1;
  uint16_t sleep_window_start_minute;
  uint16_t sleep_window_end_minute;
  uint32_t sleep_timeout_ms;
  uint32_t standby_timeout_ms;
};

#ifdef USE_ESP32
static bool erase_nvs_named_key(const char *key_str, const char *label) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open("esphome", NVS_READWRITE, &handle);
  if (err != ESP_OK) return false;
  err = nvs_erase_key(handle, key_str);
  bool ok = (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND);
  if (!ok) ESP_LOGW(TAG, "normal mode storage reset: failed to erase %s NVS key '%s': %s", label, key_str, esp_err_to_name(err));
  if (nvs_commit(handle) != ESP_OK) ok = false;
  nvs_close(handle);
  return ok;
}
static bool read_nvs_blob(const char *key_str, PsramVector<uint8_t> &out) {
  nvs_handle_t handle;
  if (nvs_open("esphome", NVS_READONLY, &handle) != ESP_OK) return false;
  size_t len = 0;
  bool ok = nvs_get_blob(handle, key_str, nullptr, &len) == ESP_OK && len > 0;
  if (ok) {
    out.resize(len);
    ok = out.size() == len && nvs_get_blob(handle, key_str, out.data(), &len) == ESP_OK;
  }
  nvs_close(handle);
  return ok;
}
static bool write_nvs_blob(const char *key_str, const uint8_t *data, size_t len) {
  nvs_handle_t handle;
  if (nvs_open("esphome", NVS_READWRITE, &handle) != ESP_OK) return false;
  esp_err_t err = nvs_set_blob(handle, key_str, data, len);
  if (err == ESP_OK) err = nvs_commit(handle);
  nvs_close(handle);
  return err == ESP_OK;
}
static bool erase_preference_key(uint32_t key, const char *label) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open("esphome", NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "normal mode storage reset: failed to open NVS namespace: %s", esp_err_to_name(err));
    return false;
  }

  char key_str[16];
  snprintf(key_str, sizeof(key_str), "%u", static_cast<unsigned>(key));
  err = nvs_erase_key(handle, key_str);
  bool ok = true;
  if (err == ESP_OK) {
    ESP_LOGI(TAG, "normal mode storage reset: erased %s NVS key '%s'", label, key_str);
  } else if (err == ESP_ERR_NVS_NOT_FOUND) {
    ESP_LOGI(TAG, "normal mode storage reset: %s NVS key '%s' already absent", label, key_str);
  } else {
    ESP_LOGW(TAG, "normal mode storage reset: failed to erase %s NVS key '%s': %s", label, key_str,
             esp_err_to_name(err));
    ok = false;
  }

  err = nvs_commit(handle);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "normal mode storage reset: failed to commit NVS erase: %s", esp_err_to_name(err));
    ok = false;
  }
  nvs_close(handle);
  return ok;
}
#endif

template<size_t N> void copy_string_to_buffer(const std::string &value, char (&target)[N]) {
  memset(target, 0, sizeof(target));
  if (value.empty()) {
    return;
  }
  const size_t copy_length = std::min(value.size(), N - 1);
  memcpy(target, value.data(), copy_length);
}

template<size_t N> std::string load_string_from_buffer(const char (&source)[N]) {
  return std::string(source, strnlen(source, N));
}

static std::string truncate_string(const std::string &value, size_t max_length) {
  if (value.size() <= max_length) {
    return value;
  }
  return value.substr(0, max_length);
}

template<size_t ROWS, size_t COLS>
void copy_string_rows(const std::array<NormalUiDeviceConfig, ROWS> &values, char (&target)[ROWS][COLS], uint8_t field) {
  for (size_t i = 0; i < ROWS; i++) {
    const std::string &value = field == 0 ? values[i].title : (field == 1 ? values[i].type : values[i].entity_id);
    copy_string_to_buffer(value, target[i]);
  }
}

template<size_t ROWS, size_t COLS>
void clear_string_rows(char (&target)[ROWS][COLS]) {
  for (size_t i = 0; i < ROWS; i++) {
    memset(target[i], 0, COLS);
  }
}

static void clear_device_state(NormalUiDeviceState &state) {
  memset(&state, 0, sizeof(state));
}

static void set_device_power(NormalUiDeviceState &state, bool power) {
  state.flags |= NORMAL_UI_DEVICE_STATE_FLAG_POWER;
  state.power = power ? 1 : 0;
}

static bool get_device_power(const NormalUiDeviceState &state) {
  return (state.flags & NORMAL_UI_DEVICE_STATE_FLAG_POWER) != 0 && state.power != 0;
}

static uint32_t normalize_light_capability_mask(uint32_t mask) {
  mask &= 0xF;
  if (mask == 0 || (mask & 0x1) == 0) {
    return 1;
  }
  return mask;
}

static void normalize_kelvin_range(uint16_t *min_kelvin, uint16_t *max_kelvin) {
  if (min_kelvin == nullptr || max_kelvin == nullptr) {
    return;
  }
  if (*min_kelvin > 0 && *max_kelvin > 0 && *min_kelvin > *max_kelvin) {
    const uint16_t tmp = *min_kelvin;
    *min_kelvin = *max_kelvin;
    *max_kelvin = tmp;
  }
}

static std::string normalize_device_type(const std::string &value) {
  std::string s = value;
  for (char &c : s) {
    if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
  }
  if (s == "switch_on" || s == "switch_off" || s == "switch") {
    return "switch";
  }
  if (s == "plug_on" || s == "plug_off" || s == "chazuo_on" || s == "chazuo_off" || s == "plug") {
    return "plug";
  }
  if (s == "light_on" || s == "light_off" || s == "liang_on" || s == "liang_off" || s == "light" ||
      s == "liang" || s == "dimming_light" || s == "dimmable_light" || s == "dimming") {
    return "light";
  }
  if (s == "curtain" || s == "cover") {
    return "curtain";
  }
  return s;
}

}  // namespace

bool NormalLightState::supports_brightness() const { return (this->capability_mask & 0x2) != 0; }

bool NormalLightState::supports_color_temperature() const { return (this->capability_mask & 0x4) != 0; }

bool NormalLightState::supports_rgb() const { return (this->capability_mask & 0x8) != 0; }

// Home Assistant capabilities remain authoritative even for old on/off-only
// presentation selections. A saved display type must not disable native controls.
bool NormalLightState::custom_supports_brightness() const {return this->supports_brightness();}
bool NormalLightState::custom_supports_color_temperature() const {return this->supports_color_temperature();}
bool NormalLightState::custom_supports_rgb() const {return this->supports_rgb();}

uint8_t NormalLightState::color_temperature_step() const {
  uint16_t min_kelvin = this->min_color_temperature_kelvin;
  uint16_t max_kelvin = this->max_color_temperature_kelvin;
  normalize_kelvin_range(&min_kelvin, &max_kelvin);
  if (min_kelvin == 0 || max_kelvin == 0) {
    return 50;
  }
  const int span = std::max<int>(1, max_kelvin - min_kelvin);
  int kelvin = this->color_temperature_kelvin;
  if (kelvin >= 153 && kelvin <= 500) {
    kelvin = static_cast<int>((1000000 + kelvin / 2) / kelvin);
  } else if (kelvin <= 100) {
    return static_cast<uint8_t>(std::clamp(kelvin, 0, 10) * 10);
  }
  if (kelvin < min_kelvin || kelvin > max_kelvin) {
    return 50;
  }
  return static_cast<uint8_t>(std::clamp((int) ((max_kelvin - kelvin) * 100 + span / 2) / span, 0, 100));
}

uint16_t NormalLightState::kelvin_from_color_temperature_step(uint8_t step) const {
  uint16_t min_kelvin = this->min_color_temperature_kelvin;
  uint16_t max_kelvin = this->max_color_temperature_kelvin;
  normalize_kelvin_range(&min_kelvin, &max_kelvin);
  if (min_kelvin == 0 || max_kelvin == 0) {
    return 0;
  }
  const int span = std::max<int>(1, max_kelvin - min_kelvin);
  const int normalized_step = std::clamp<int>(step, 0, 100);
  return static_cast<uint16_t>(
      std::clamp(max_kelvin - (normalized_step * span + 50) / 100, (int) min_kelvin, (int) max_kelvin));
}

uint16_t NormalLightState::hue_degrees() const { return static_cast<uint16_t>((this->rgb_tick % RGB_TICKS) * RGB_TICK_DEGREES); }

NormalDeviceSlot::NormalDeviceSlot(OnxStorage *storage, size_t index) : storage_(storage), index_(index) {}

bool NormalDeviceSlot::valid() const {
  return this->storage_ != nullptr && this->index_ < NORMAL_UI_DEVICE_MAX_COUNT;
}

size_t NormalDeviceSlot::index() const { return this->index_; }

const std::string &NormalDeviceSlot::title() const {
  static const std::string empty;
  return this->valid() ? this->storage_->get_normal_ui_device_title(this->index_) : empty;
}

const std::string &NormalDeviceSlot::type() const {
  static const std::string empty;
  return this->valid() ? this->storage_->get_normal_ui_device_type(this->index_) : empty;
}

const std::string &NormalDeviceSlot::entity_id() const {
  static const std::string empty;
  return this->valid() ? this->storage_->get_normal_ui_device_entity_id(this->index_) : empty;
}

bool NormalDeviceSlot::is_on() const {
  return this->valid() && this->storage_->get_normal_ui_device_state(this->index_);
}

bool NormalDeviceSlot::set_power(bool state) const {
  return this->valid() && this->storage_->set_normal_ui_device_state(this->index_, state);
}

NormalLightState NormalDeviceSlot::light() const {
  return this->valid() ? this->storage_->get_normal_light_state(this->index_) : NormalLightState{};
}

bool NormalDeviceSlot::update_light(const NormalLightStatePatch &patch) const {
  return this->valid() && this->storage_->update_normal_light_state(this->index_, patch);
}

void OnxStorage::setup() {
  this->load_defaults_();

  if (global_preferences == nullptr) {
    ESP_LOGW(TAG, "Preferences backend is unavailable; factory data will stay in RAM only");
    return;
  }

  this->factory_wifi_pref_ = global_preferences->make_preference<StoredFactoryWifi>(FACTORY_WIFI_PREF_KEY, true);
  this->auth_record_pref_ = global_preferences->make_preference<StoredAuthRecord>(AUTH_RECORD_PREF_KEY, true);
  this->power_policy_pref_ = global_preferences->make_preference<StoredPowerPolicy>(POWER_POLICY_PREF_KEY, true);

  StoredFactoryWifi stored_wifi{};
  if (this->factory_wifi_pref_.load(&stored_wifi)) {
    this->factory_ssid_ = load_string_from_buffer(stored_wifi.ssid);
    this->factory_password_ = load_string_from_buffer(stored_wifi.password);
  }

  StoredAuthRecord stored_auth{};

#if defined(ENABLE_AUTHCODE_EFUSE) && defined(USE_ESP32)
  unsigned char read_efuse_auth[16] = {0};
  if (read_auth_code(read_efuse_auth) == ESP_OK) {
      char zero_array[16] = {0};
      if (memcmp(read_efuse_auth, zero_array, 16) != 0) {
          // It's not all zeros, so eFuse contains the auth code.
          this->auth_code_ = bytes_to_hex(read_efuse_auth, 16);
      }
  }
#else
  if (this->auth_record_pref_.load(&stored_auth)) {
    this->auth_code_ = load_string_from_buffer(stored_auth.auth_code);
  }
#endif
  if (this->auth_record_pref_.load(&stored_auth)) {
    this->auth_sha256_ = load_string_from_buffer(stored_auth.auth_sha256);
  }

  this->auth_provisioned_ = !this->auth_code_.empty();

#ifdef USE_ESP32
  {
    PsramVector<uint8_t> blob;
    if (read_nvs_blob(NORMAL_UI_CFG_V4_KEY, blob) && blob.size() >= 6) {
      uint32_t magic = 0;
      uint16_t count = 0;
      memcpy(&magic, blob.data(), 4);
      memcpy(&count, blob.data() + 4, 2);
      if (magic == NORMAL_UI_CFG_V4_MAGIC && count <= NORMAL_UI_DEVICE_MAX_COUNT) {
        size_t pos = 6;
        bool valid = true;
        PsramVector<NormalUiDeviceConfig> loaded;
        loaded.resize(count);
        auto read_str = [&](std::string &dst, size_t max_len) -> bool {
          if (pos >= blob.size()) return false;
          const size_t n = blob[pos++];
          if (n > max_len || pos + n > blob.size()) return false;
          dst.assign(reinterpret_cast<const char *>(blob.data() + pos), n);
          pos += n;
          return true;
        };
        for (size_t i = 0; i < count && valid; ++i) {
          valid = read_str(loaded[i].title, NORMAL_UI_TITLE_MAX_LENGTH) &&
                  read_str(loaded[i].type, NORMAL_UI_ICON_MAX_LENGTH) &&
                  read_str(loaded[i].entity_id, NORMAL_UI_ENTITY_ID_MAX_LENGTH);
          if (valid) loaded[i].type = normalize_device_type(loaded[i].type);
        }
        if (valid && pos == blob.size()) {
          this->normal_ui_devices_ = std::move(loaded);
        }
      }
    }
  }
#endif

  this->normal_ui_device_states_.clear();
#ifdef USE_ESP32
  {
    PsramVector<uint8_t> blob;
    if (read_nvs_blob(NORMAL_UI_ST_V5_KEY, blob) && blob.size() >= 6) {
      uint32_t magic = 0;
      uint16_t count = 0;
      memcpy(&magic, blob.data(), 4);
      memcpy(&count, blob.data() + 4, 2);
      if (magic == NORMAL_UI_ST_V5_MAGIC && count <= NORMAL_UI_DEVICE_MAX_COUNT &&
          blob.size() == 6 + size_t(count) * sizeof(NormalUiDeviceState)) {
        this->normal_ui_device_states_.resize(count);
        if (count > 0) memcpy(this->normal_ui_device_states_.data(), blob.data() + 6, size_t(count) * sizeof(NormalUiDeviceState));
      }
    }
  }
#endif

  StoredPowerPolicy stored_power_policy{};
  if (this->power_policy_pref_.load(&stored_power_policy) && stored_power_policy.magic == POWER_POLICY_MAGIC) {
    this->sleep_mode_enabled_ = stored_power_policy.sleep_mode_enabled != 0;
    this->sleep_window_start_minute_ = std::min<uint16_t>(stored_power_policy.sleep_window_start_minute, 1439);
    this->sleep_window_end_minute_ = std::min<uint16_t>(stored_power_policy.sleep_window_end_minute, 1439);
    this->sleep_timeout_ms_ = stored_power_policy.sleep_timeout_ms < 1 ? 1 : stored_power_policy.sleep_timeout_ms;
    this->standby_timeout_ms_ = stored_power_policy.standby_timeout_ms;
  }
}

void OnxStorage::dump_config() {
  ESP_LOGCONFIG(TAG, "ONX Storage:");
  ESP_LOGCONFIG(TAG, "  Auth provisioned: %s", this->auth_provisioned_ ? "YES" : "NO");
  ESP_LOGCONFIG(TAG, "  Factory SSID: %s", this->factory_ssid_.empty() ? "<empty>" : this->factory_ssid_.c_str());
  ESP_LOGCONFIG(TAG, "  Factory password: %s", this->factory_password_.empty() ? "<empty>" : "<hidden>");
  ESP_LOGCONFIG(TAG, "  Auth code: %s", this->auth_code_.empty() ? "<empty>" : "<present>");
  ESP_LOGCONFIG(TAG, "  Auth sha256: %s", this->auth_sha256_.empty() ? "<empty>" : "<present>");
}

bool OnxStorage::is_auth_provisioned() const { return this->auth_provisioned_; }

void OnxStorage::set_auth_provisioned(bool auth_provisioned) { this->auth_provisioned_ = auth_provisioned; }

bool OnxStorage::has_factory_wifi() const { return !this->factory_ssid_.empty(); }

const std::string &OnxStorage::get_factory_ssid() const { return this->factory_ssid_; }

const std::string &OnxStorage::get_factory_password() const { return this->factory_password_; }

bool OnxStorage::set_factory_wifi(const std::string &factory_ssid, const std::string &factory_password) {
  if (factory_ssid.size() > FACTORY_SSID_MAX_LENGTH || factory_password.size() > FACTORY_PASSWORD_MAX_LENGTH) {
    ESP_LOGW(TAG, "Factory Wi-Fi credentials exceed the PRD length limit");
    return false;
  }
  const std::string previous_ssid = this->factory_ssid_;
  const std::string previous_password = this->factory_password_;
  this->factory_ssid_ = factory_ssid;
  this->factory_password_ = factory_password;
  if (this->save_factory_wifi_()) {
    return true;
  }
  this->factory_ssid_ = previous_ssid;
  this->factory_password_ = previous_password;
  return false;
}

const std::string &OnxStorage::get_auth_code() const { return this->auth_code_; }

const std::string &OnxStorage::get_auth_sha256() const { return this->auth_sha256_; }

bool OnxStorage::set_auth_record(const std::string &auth_code, const std::string &auth_sha256) {
  if (auth_code.size() > AUTH_CODE_MAX_LENGTH || auth_sha256.size() > AUTH_SHA256_MAX_LENGTH) {
    ESP_LOGW(TAG, "Auth record exceeds the supported storage length");
    return false;
  }

#if defined(ENABLE_AUTHCODE_EFUSE) && defined(USE_ESP32)
  if (auth_code.length() == 32) {
      uint8_t auth_bytes[16] = {0};
      if (hex_to_bytes(auth_code, auth_bytes, 16)) {
          if (!write_auth_code_check(auth_bytes)) {
              ESP_LOGE(TAG, "Failed to write auth code to eFuse or conflict");
              return false;
          }
      } else {
          ESP_LOGE(TAG, "Invalid auth code hex string");
          return false;
      }
  } else if (!auth_code.empty()) {
      ESP_LOGE(TAG, "Auth code must be 32 hex characters");
      return false;
  }
#endif

  const std::string previous_auth_code = this->auth_code_;
  const std::string previous_auth_sha256 = this->auth_sha256_;
  const bool previous_auth_provisioned = this->auth_provisioned_;
  this->auth_code_ = auth_code;
  this->auth_sha256_ = auth_sha256;
  this->auth_provisioned_ = !auth_code.empty();
  if (this->save_auth_record_()) {
    return true;
  }
  this->auth_code_ = previous_auth_code;
  this->auth_sha256_ = previous_auth_sha256;
  this->auth_provisioned_ = previous_auth_provisioned;
  return false;
}

void OnxStorage::ensure_device_capacity_(size_t count) {
  if (count > NORMAL_UI_DEVICE_MAX_COUNT) count = NORMAL_UI_DEVICE_MAX_COUNT;
  if (this->normal_ui_devices_.size() < count) this->normal_ui_devices_.resize(count);
}

void OnxStorage::ensure_state_capacity_(size_t count) {
  if (count > NORMAL_UI_DEVICE_MAX_COUNT) count = NORMAL_UI_DEVICE_MAX_COUNT;
  if (this->normal_ui_device_states_.size() < count) {
    const size_t old_size = this->normal_ui_device_states_.size();
    this->normal_ui_device_states_.resize(count);
    for (size_t i = old_size; i < count; ++i) clear_device_state(this->normal_ui_device_states_[i]);
  }
}

bool OnxStorage::reset_normal_mode_config() {
  this->normal_ui_devices_.clear();
  this->normal_ui_device_states_.clear();
  this->standby_timeout_ms_ = 300000;
  this->sleep_timeout_ms_ = 60000;
  this->sleep_window_start_minute_ = 0;
  this->sleep_window_end_minute_ = 0;
  this->sleep_mode_enabled_ = false;

  bool ok = true;
#ifdef USE_ESP32
  ok = erase_nvs_named_key(NORMAL_UI_CFG_V4_KEY, "normal UI config v4") && ok;
  ok = erase_nvs_named_key(NORMAL_UI_ST_V5_KEY, "normal UI state v5") && ok;
  ok = erase_preference_key(fnv1_hash("onx-storage-normal-ui-config"), "old normal UI config") && ok;
  ok = erase_preference_key(fnv1_hash("onx-storage-normal-ui-state-v2"), "old normal UI state") && ok;
  ok = erase_preference_key(fnv1_hash("onx-storage-normal-ui-config-v2"), "20-entry normal UI config") && ok;
  ok = erase_preference_key(fnv1_hash("onx-storage-normal-ui-state-v3"), "20-entry normal UI state") && ok;
  ok = erase_preference_key(fnv1_hash("onx-storage-normal-ui-config-v3"), "40-entry normal UI config") && ok;
  ok = erase_preference_key(fnv1_hash("onx-storage-normal-ui-state-v4"), "40-entry normal UI state") && ok;
  ok = erase_preference_key(fnv1_hash("onx-storage-normal-ui-state"), "legacy normal UI state") && ok;
  ok = erase_preference_key(POWER_POLICY_PREF_KEY, "power policy") && ok;
#else
  ok = this->save_normal_ui_config_() && ok;
  ok = this->save_normal_ui_states_() && ok;
  ok = this->save_power_policy_() && ok;
#endif
  ESP_LOGI(TAG, "Normal mode storage config erased: %s", ok ? "ok" : "partial failure");
  return ok;
}

bool OnxStorage::set_normal_ui_config(const std::vector<NormalUiDeviceConfig> &devices) {
  const auto previous_devices = this->normal_ui_devices_;
  size_t count = std::min(devices.size(), NORMAL_UI_DEVICE_MAX_COUNT);
  while (count > 0 && devices[count - 1].entity_id.empty() && devices[count - 1].title.empty()) --count;
  this->normal_ui_devices_.resize(count);
  for (size_t i = 0; i < count; i++) {
    this->normal_ui_devices_[i].title = truncate_string(devices[i].title, NORMAL_UI_TITLE_MAX_LENGTH);
    this->normal_ui_devices_[i].type = truncate_string(normalize_device_type(devices[i].type), NORMAL_UI_ICON_MAX_LENGTH);
    this->normal_ui_devices_[i].entity_id = truncate_string(devices[i].entity_id, NORMAL_UI_ENTITY_ID_MAX_LENGTH);
  }
  bool changed = previous_devices.size() != this->normal_ui_devices_.size();
  for (size_t i = 0; !changed && i < count; ++i) {
    const auto &before = previous_devices[i];
    const auto &after = this->normal_ui_devices_[i];
    changed = before.title != after.title || before.type != after.type || before.entity_id != after.entity_id;
  }
  if (!changed || this->save_normal_ui_config_()) {
    return true;
  }
  this->normal_ui_devices_ = previous_devices;
  return false;
}

const NormalUiDeviceState &OnxStorage::get_normal_ui_device_state_record(size_t index) const {
  static const NormalUiDeviceState empty{};
  if (index >= this->normal_ui_device_states_.size()) {
    return empty;
  }
  return this->normal_ui_device_states_[index];
}

bool OnxStorage::set_normal_ui_device_state_record(size_t index, const NormalUiDeviceState &state) {
  if (index >= NORMAL_UI_DEVICE_MAX_COUNT) {
    return false;
  }
  this->ensure_state_capacity_(index + 1);
  const auto previous_state = this->normal_ui_device_states_[index];
  this->normal_ui_device_states_[index] = state;
  if (this->save_normal_ui_states_()) {
    return true;
  }
  this->normal_ui_device_states_[index] = previous_state;
  return false;
}

bool OnxStorage::get_normal_ui_device_state(size_t index) const {
  if (index >= this->normal_ui_device_states_.size()) {
    return false;
  }
  return get_device_power(this->normal_ui_device_states_[index]);
}

bool OnxStorage::set_normal_ui_device_state(size_t index, bool state) {
  if (index >= NORMAL_UI_DEVICE_MAX_COUNT) {
    return false;
  }
  this->ensure_state_capacity_(index + 1);
  const auto previous_state = this->normal_ui_device_states_[index];
  set_device_power(this->normal_ui_device_states_[index], state);
  if (this->save_normal_ui_states_()) {
    return true;
  }
  this->normal_ui_device_states_[index] = previous_state;
  return false;
}

uint8_t OnxStorage::get_normal_ui_device_brightness(size_t index) const {
  if (index >= this->normal_ui_device_states_.size()) {
    return 0;
  }
  return this->normal_ui_device_states_[index].brightness;
}

bool OnxStorage::set_normal_ui_device_brightness(size_t index, uint8_t brightness) {
  if (index >= NORMAL_UI_DEVICE_MAX_COUNT) {
    return false;
  }
  this->ensure_state_capacity_(index + 1);
  const auto previous_state = this->normal_ui_device_states_[index];
  this->normal_ui_device_states_[index].flags |= NORMAL_UI_DEVICE_STATE_FLAG_BRIGHTNESS;
  this->normal_ui_device_states_[index].brightness = brightness;
  if (this->save_normal_ui_states_()) {
    return true;
  }
  this->normal_ui_device_states_[index] = previous_state;
  return false;
}

uint16_t OnxStorage::get_normal_ui_device_color_temperature(size_t index) const {
  if (index >= this->normal_ui_device_states_.size()) {
    return 0;
  }
  return this->normal_ui_device_states_[index].color_temperature;
}

bool OnxStorage::set_normal_ui_device_color_temperature(size_t index, uint16_t color_temperature) {
  if (index >= NORMAL_UI_DEVICE_MAX_COUNT) {
    return false;
  }
  this->ensure_state_capacity_(index + 1);
  const auto previous_state = this->normal_ui_device_states_[index];
  this->normal_ui_device_states_[index].flags |= NORMAL_UI_DEVICE_STATE_FLAG_COLOR_TEMPERATURE;
  this->normal_ui_device_states_[index].color_temperature = color_temperature;
  if (this->save_normal_ui_states_()) {
    return true;
  }
  this->normal_ui_device_states_[index] = previous_state;
  return false;
}

uint8_t OnxStorage::get_normal_ui_device_position(size_t index) const {
  if (index >= this->normal_ui_device_states_.size()) {
    return 0;
  }
  return this->normal_ui_device_states_[index].position;
}

bool OnxStorage::set_normal_ui_device_position(size_t index, uint8_t position) {
  if (index >= NORMAL_UI_DEVICE_MAX_COUNT) {
    return false;
  }
  this->ensure_state_capacity_(index + 1);
  const auto previous_state = this->normal_ui_device_states_[index];
  this->normal_ui_device_states_[index].flags |= NORMAL_UI_DEVICE_STATE_FLAG_POSITION;
  this->normal_ui_device_states_[index].position = position;
  if (this->save_normal_ui_states_()) {
    return true;
  }
  this->normal_ui_device_states_[index] = previous_state;
  return false;
}

uint32_t OnxStorage::get_normal_ui_device_extra_u32(size_t index, size_t slot) const {
  if (index >= this->normal_ui_device_states_.size() || slot >= 4) {
    return 0;
  }
  return this->normal_ui_device_states_[index].extra_u32[slot];
}

bool OnxStorage::set_normal_ui_device_extra_u32(size_t index, size_t slot, uint32_t value) {
  if (index >= NORMAL_UI_DEVICE_MAX_COUNT || slot >= 4) {
    return false;
  }
  this->ensure_state_capacity_(index + 1);
  const auto previous_state = this->normal_ui_device_states_[index];
  this->normal_ui_device_states_[index].extra_u32[slot] = value;
  this->normal_ui_device_states_[index].flags |= static_cast<uint8_t>(NORMAL_UI_DEVICE_STATE_FLAG_EXTRA0 << slot);
  if (this->save_normal_ui_states_()) {
    return true;
  }
  this->normal_ui_device_states_[index] = previous_state;
  return false;
}

uint16_t OnxStorage::get_normal_ui_device_extra_u16(size_t index, size_t slot) const {
  if (index >= this->normal_ui_device_states_.size() || slot >= 4) {
    return 0;
  }
  return this->normal_ui_device_states_[index].extra_u16[slot];
}

bool OnxStorage::set_normal_ui_device_extra_u16(size_t index, size_t slot, uint16_t value) {
  if (index >= NORMAL_UI_DEVICE_MAX_COUNT || slot >= 4) {
    return false;
  }
  this->ensure_state_capacity_(index + 1);
  const auto previous_state = this->normal_ui_device_states_[index];
  this->normal_ui_device_states_[index].extra_u16[slot] = value;
  this->normal_ui_device_states_[index].flags |= static_cast<uint8_t>(NORMAL_UI_DEVICE_STATE_FLAG_EXTRA0 << slot);
  if (this->save_normal_ui_states_()) {
    return true;
  }
  this->normal_ui_device_states_[index] = previous_state;
  return false;
}

NormalDeviceSlot OnxStorage::normal_device_slot(size_t index) { return NormalDeviceSlot(this, index); }

NormalLightState OnxStorage::get_normal_light_state(size_t index) const {
  NormalLightState light{};
  if (index >= this->normal_ui_device_states_.size()) {
    return light;
  }

  const NormalUiDeviceState &state = this->normal_ui_device_states_[index];
  light.is_on = get_device_power(state);
  light.brightness_pct = static_cast<uint8_t>(std::clamp<uint32_t>(state.extra_u32[2], 0, 100));
  light.last_nonzero_brightness_pct = static_cast<uint8_t>(std::clamp<uint32_t>(state.extra_u32[3], 1, 100));
  light.color_temperature_kelvin = state.color_temperature;
  light.min_color_temperature_kelvin = state.extra_u16[0];
  light.max_color_temperature_kelvin = state.extra_u16[1];
  normalize_kelvin_range(&light.min_color_temperature_kelvin, &light.max_color_temperature_kelvin);
  light.rgb_tick = static_cast<uint8_t>(std::clamp<uint32_t>(state.extra_u32[1], 0, RGB_TICKS - 1));
  light.capability_mask = normalize_light_capability_mask(state.extra_u32[0]);
  light.custom_capability_mask = state.reserved1 == 0 ? 0xF : normalize_light_capability_mask(state.reserved1);
  return light;
}

bool OnxStorage::update_normal_light_state(size_t index, const NormalLightStatePatch &patch) {
  if (index >= NORMAL_UI_DEVICE_MAX_COUNT) {
    return false;
  }
  this->ensure_state_capacity_(index + 1);
  const auto previous_state = this->normal_ui_device_states_[index];
  NormalUiDeviceState &state = this->normal_ui_device_states_[index];

  if (patch.has_power) {
    set_device_power(state, patch.is_on);
  }
  if (patch.has_brightness_pct) {
    const uint8_t brightness = static_cast<uint8_t>(std::clamp<int>(patch.brightness_pct, 0, 100));
    state.flags |= NORMAL_UI_DEVICE_STATE_FLAG_BRIGHTNESS | NORMAL_UI_DEVICE_STATE_FLAG_EXTRA2;
    state.brightness = brightness;
    state.extra_u32[2] = brightness;
    if (brightness > 0) {
      state.flags |= NORMAL_UI_DEVICE_STATE_FLAG_EXTRA3;
      state.extra_u32[3] = brightness;
    }
  }
  if (patch.has_color_temperature_kelvin) {
    uint16_t min_kelvin = state.extra_u16[0];
    uint16_t max_kelvin = state.extra_u16[1];
    normalize_kelvin_range(&min_kelvin, &max_kelvin);
    int kelvin = patch.color_temperature_kelvin;
    if (min_kelvin > 0 && max_kelvin > 0) {
      kelvin = std::clamp<int>(kelvin, min_kelvin, max_kelvin);
    }
    state.flags |= NORMAL_UI_DEVICE_STATE_FLAG_COLOR_TEMPERATURE;
    state.color_temperature = static_cast<uint16_t>(std::clamp<int>(kelvin, 0, 65535));
  }
  if (patch.has_rgb_tick) {
    state.flags |= NORMAL_UI_DEVICE_STATE_FLAG_EXTRA1;
    state.extra_u32[1] = std::clamp<int>(patch.rgb_tick, 0, RGB_TICKS - 1);
  }
  if (patch.has_capability_mask) {
    state.flags |= NORMAL_UI_DEVICE_STATE_FLAG_EXTRA0;
    state.extra_u32[0] = normalize_light_capability_mask(patch.capability_mask);
  }
  if (patch.has_custom_capability_mask) {
    state.reserved1 = static_cast<uint16_t>(normalize_light_capability_mask(patch.custom_capability_mask));
  }
  if (patch.has_color_temperature_range) {
    uint16_t min_kelvin = patch.min_color_temperature_kelvin;
    uint16_t max_kelvin = patch.max_color_temperature_kelvin;
    normalize_kelvin_range(&min_kelvin, &max_kelvin);
    state.flags |= NORMAL_UI_DEVICE_STATE_FLAG_EXTRA0 | NORMAL_UI_DEVICE_STATE_FLAG_EXTRA1;
    state.extra_u16[0] = min_kelvin;
    state.extra_u16[1] = max_kelvin;
  }

  if (this->save_normal_ui_states_()) {
    return true;
  }
  this->normal_ui_device_states_[index] = previous_state;
  return false;
}

const std::string &OnxStorage::get_normal_ui_device_title(size_t index) const {
  static const std::string empty;
  if (index >= this->normal_ui_devices_.size()) {
    return empty;
  }
  return this->normal_ui_devices_[index].title;
}

const std::string &OnxStorage::get_normal_ui_device_type(size_t index) const {
  static const std::string empty;
  if (index >= this->normal_ui_devices_.size()) {
    return empty;
  }
  return this->normal_ui_devices_[index].type;
}

const std::string &OnxStorage::get_normal_ui_device_entity_id(size_t index) const {
  static const std::string empty;
  if (index >= this->normal_ui_devices_.size()) {
    return empty;
  }
  return this->normal_ui_devices_[index].entity_id;
}

uint32_t OnxStorage::get_standby_timeout_ms() const { return this->standby_timeout_ms_; }

bool OnxStorage::set_standby_timeout_ms(uint32_t standby_timeout_ms) {
  const uint32_t previous = this->standby_timeout_ms_;
  this->standby_timeout_ms_ = standby_timeout_ms;
  if (this->save_power_policy_()) {
    return true;
  }
  this->standby_timeout_ms_ = previous;
  return false;
}

bool OnxStorage::get_sleep_mode_enabled() const { return this->sleep_mode_enabled_; }

uint16_t OnxStorage::get_sleep_window_start_minute() const { return this->sleep_window_start_minute_; }

uint16_t OnxStorage::get_sleep_window_end_minute() const { return this->sleep_window_end_minute_; }

uint32_t OnxStorage::get_sleep_timeout_ms() const { return this->sleep_timeout_ms_; }

bool OnxStorage::set_sleep_mode_config(bool enabled, uint16_t start_minute, uint16_t end_minute, uint32_t timeout_ms) {
  const bool previous_enabled = this->sleep_mode_enabled_;
  const uint16_t previous_start = this->sleep_window_start_minute_;
  const uint16_t previous_end = this->sleep_window_end_minute_;
  const uint32_t previous_timeout = this->sleep_timeout_ms_;

  this->sleep_mode_enabled_ = enabled;
  this->sleep_window_start_minute_ = std::min<uint16_t>(start_minute, 1439);
  this->sleep_window_end_minute_ = std::min<uint16_t>(end_minute, 1439);
  this->sleep_timeout_ms_ = timeout_ms < 1 ? 1 : timeout_ms;
  if (this->save_power_policy_()) {
    return true;
  }

  this->sleep_mode_enabled_ = previous_enabled;
  this->sleep_window_start_minute_ = previous_start;
  this->sleep_window_end_minute_ = previous_end;
  this->sleep_timeout_ms_ = previous_timeout;
  return false;
}

void OnxStorage::load_defaults_() {
  this->auth_provisioned_ = false;
  this->factory_ssid_ = DEFAULT_FACTORY_SSID;
  this->factory_password_ = DEFAULT_FACTORY_PASSWORD;
  this->auth_code_.clear();
  this->auth_sha256_.clear();
  for (auto &device : this->normal_ui_devices_) {
    device.title.clear();
    device.type.clear();
    device.entity_id.clear();
  }
  for (auto &state : this->normal_ui_device_states_) {
    clear_device_state(state);
  }
  this->standby_timeout_ms_ = 300000;
  this->sleep_timeout_ms_ = 60000;
  this->sleep_window_start_minute_ = 0;
  this->sleep_window_end_minute_ = 0;
  this->sleep_mode_enabled_ = false;
}

bool OnxStorage::save_factory_wifi_() {
  StoredFactoryWifi stored_wifi{};
  copy_string_to_buffer(this->factory_ssid_, stored_wifi.ssid);
  copy_string_to_buffer(this->factory_password_, stored_wifi.password);

  if (!this->factory_wifi_pref_.save(&stored_wifi)) {
    ESP_LOGW(TAG, "Failed to write factory Wi-Fi to NVS");
    return false;
  }
  if (global_preferences != nullptr && !global_preferences->sync()) {
    ESP_LOGW(TAG, "Failed to sync factory Wi-Fi NVS writes");
    return false;
  }

  StoredFactoryWifi verify_wifi{};
  if (!this->factory_wifi_pref_.load(&verify_wifi)) {
    ESP_LOGW(TAG, "Failed to read back factory Wi-Fi from NVS");
    return false;
  }
  const bool matches = load_string_from_buffer(verify_wifi.ssid) == this->factory_ssid_ &&
                       load_string_from_buffer(verify_wifi.password) == this->factory_password_;
  if (!matches) {
    ESP_LOGW(TAG, "Factory Wi-Fi read-back verification failed");
  }
  return matches;
}

bool OnxStorage::save_auth_record_() {
  StoredAuthRecord stored_auth{};
  copy_string_to_buffer(this->auth_code_, stored_auth.auth_code);
  copy_string_to_buffer(this->auth_sha256_, stored_auth.auth_sha256);

  if (!this->auth_record_pref_.save(&stored_auth)) {
    ESP_LOGW(TAG, "Failed to write auth record to NVS");
    return false;
  }
  if (global_preferences != nullptr && !global_preferences->sync()) {
    ESP_LOGW(TAG, "Failed to sync auth record NVS writes");
    return false;
  }

  StoredAuthRecord verify_auth{};
  if (!this->auth_record_pref_.load(&verify_auth)) {
    ESP_LOGW(TAG, "Failed to read back auth record from NVS");
    return false;
  }
  const bool matches = load_string_from_buffer(verify_auth.auth_code) == this->auth_code_ &&
                       load_string_from_buffer(verify_auth.auth_sha256) == this->auth_sha256_;
  if (!matches) {
    ESP_LOGW(TAG, "Auth record read-back verification failed");
  }
  return matches;
}

bool OnxStorage::save_normal_ui_config_() {
#ifdef USE_ESP32
  // Compact length-prefixed NVS blob in PSRAM: scales with configured count
  // instead of writing a fixed multi-kilobyte matrix for empty slots.
  const uint16_t count = static_cast<uint16_t>(std::min(this->normal_ui_devices_.size(), NORMAL_UI_DEVICE_MAX_COUNT));
  PsramVector<uint8_t> blob;
  blob.reserve(6 + size_t(count) * 48);
  auto append_bytes = [&](const void *p, size_t n) {
    const auto *b = static_cast<const uint8_t *>(p);
    blob.insert(blob.end(), b, b + n);
  };
  auto append_str = [&](const std::string &s, size_t max_len) {
    const uint8_t n = static_cast<uint8_t>(std::min(s.size(), max_len));
    blob.push_back(n);
    if (n > 0) append_bytes(s.data(), n);
  };
  append_bytes(&NORMAL_UI_CFG_V4_MAGIC, 4);
  append_bytes(&count, 2);
  for (size_t i = 0; i < count; ++i) {
    append_str(this->normal_ui_devices_[i].title, NORMAL_UI_TITLE_MAX_LENGTH);
    append_str(this->normal_ui_devices_[i].type, NORMAL_UI_ICON_MAX_LENGTH);
    append_str(this->normal_ui_devices_[i].entity_id, NORMAL_UI_ENTITY_ID_MAX_LENGTH);
  }
  if (!write_nvs_blob(NORMAL_UI_CFG_V4_KEY, blob.data(), blob.size())) {
    ESP_LOGW(TAG, "Failed to write normal UI config v4 NVS (%u bytes)", static_cast<unsigned>(blob.size()));
    return false;
  }
  PsramVector<uint8_t> verify;
  if (!read_nvs_blob(NORMAL_UI_CFG_V4_KEY, verify) || verify.size() != blob.size() ||
      memcmp(verify.data(), blob.data(), blob.size()) != 0) {
    ESP_LOGW(TAG, "Normal UI config v4 read-back verification failed");
    return false;
  }
  return true;
#else
  return true;
#endif
}

bool OnxStorage::end_state_batch() {
  if (this->state_batch_depth_ == 0) return true;
  if (--this->state_batch_depth_ != 0 || !this->state_batch_dirty_) return true;
  this->state_batch_dirty_ = false;
  return this->save_normal_ui_states_();
}

bool OnxStorage::save_normal_ui_states_() {
  if (this->state_batch_depth_ != 0) {
    this->state_batch_dirty_ = true;
    return true;
  }
#ifdef USE_ESP32
  const uint16_t count = static_cast<uint16_t>(std::min(this->normal_ui_device_states_.size(), NORMAL_UI_DEVICE_MAX_COUNT));
  PsramVector<uint8_t> blob(6 + size_t(count) * sizeof(NormalUiDeviceState));
  memcpy(blob.data(), &NORMAL_UI_ST_V5_MAGIC, 4);
  memcpy(blob.data() + 4, &count, 2);
  if (count > 0) memcpy(blob.data() + 6, this->normal_ui_device_states_.data(), size_t(count) * sizeof(NormalUiDeviceState));
  if (!write_nvs_blob(NORMAL_UI_ST_V5_KEY, blob.data(), blob.size())) {
    ESP_LOGW(TAG, "Failed to write normal UI state v5 NVS (%u bytes)", static_cast<unsigned>(blob.size()));
    return false;
  }
  PsramVector<uint8_t> verify;
  if (!read_nvs_blob(NORMAL_UI_ST_V5_KEY, verify) || verify.size() != blob.size() ||
      memcmp(verify.data(), blob.data(), blob.size()) != 0) {
    ESP_LOGW(TAG, "Normal UI state v5 read-back verification failed");
    return false;
  }
  return true;
#else
  return true;
#endif
}

bool OnxStorage::save_power_policy_() {
  StoredPowerPolicy stored{};
  stored.magic = POWER_POLICY_MAGIC;
  stored.sleep_mode_enabled = this->sleep_mode_enabled_ ? 1 : 0;
  stored.reserved0 = 0;
  stored.reserved1 = 0;
  stored.sleep_window_start_minute = this->sleep_window_start_minute_;
  stored.sleep_window_end_minute = this->sleep_window_end_minute_;
  stored.sleep_timeout_ms = this->sleep_timeout_ms_;
  stored.standby_timeout_ms = this->standby_timeout_ms_;

  if (!this->power_policy_pref_.save(&stored)) {
    ESP_LOGW(TAG, "Failed to write power policy to NVS");
    return false;
  }
  if (global_preferences != nullptr && !global_preferences->sync()) {
    ESP_LOGW(TAG, "Failed to sync power policy NVS writes");
    return false;
  }

  StoredPowerPolicy verify{};
  if (!this->power_policy_pref_.load(&verify)) {
    ESP_LOGW(TAG, "Failed to read back power policy from NVS");
    return false;
  }
  const bool matches = verify.magic == POWER_POLICY_MAGIC &&
                       verify.sleep_mode_enabled == (this->sleep_mode_enabled_ ? 1 : 0) &&
                       verify.sleep_window_start_minute == this->sleep_window_start_minute_ &&
                       verify.sleep_window_end_minute == this->sleep_window_end_minute_ &&
                       verify.sleep_timeout_ms == this->sleep_timeout_ms_ &&
                       verify.standby_timeout_ms == this->standby_timeout_ms_;
  if (!matches) {
    ESP_LOGW(TAG, "Power policy read-back verification failed");
  }
  return matches;
}

}  // namespace esphome::onx_storage
