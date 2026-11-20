Import("env")

from pathlib import Path


# ESPHome 2026.11.0 performs HTTP OTA synchronously from its main loop.
# Keep its transport, flash lock, verification and reboot hooks while moving
# the blocking transfer to an internal-RAM FreeRTOS task.
component = Path(env.subst("$PROJECT_SRC_DIR")) / "esphome/components/http_request"


def replace_once(text, original, replacement):
    if replacement in text:
        return text
    if text.count(original) != 1:
        raise RuntimeError("ESPHome HTTP OTA source changed. Review the OTA worker patch.")
    return text.replace(original, replacement, 1)


def update_file(path, transform):
    original = path.read_text(encoding="utf-8")
    patched = transform(original)
    if patched != original:
        path.write_text(patched, encoding="utf-8")


def patch_header(source):
    return replace_once(
        source,
        """\
  uint8_t do_ota_();
  std::string get_url_with_auth_(const std::string &url);
  bool http_get_md5_();""",
        """\
  struct Install {
    OtaHttpRequestComponent *owner;
    Request request;
  };
  static void transfer_task_(void *argument);
  void finish_transfer_(Install *install, uint8_t status);
  uint8_t do_ota_(Request &request);
  std::string get_url_with_auth_(const Request &request, const std::string &url);
  bool http_get_md5_(Request &request);
  bool installing_{false};""",
    )


def patch_transfer(source):
    if "OtaHttpRequestComponent::transfer_task_(" in source:
        return source
    source = replace_once(
        source,
        "#include <cctype>",
        "#include <cctype>\n#include <new>\n\n#include <freertos/FreeRTOS.h>\n#include <freertos/task.h>",
    )
    source = replace_once(
        source,
        """\
  ESP_LOGI(TAG, "Starting update");
#ifdef USE_OTA_STATE_LISTENER
  this->notify_state_(ota::OTA_STARTED, 0.0f, 0);
#endif

  auto ota_status = this->do_ota_();""",
        """\
  if (this->installing_) {
    ESP_LOGW(TAG, "HTTP OTA already in progress");
    return;
  }
  this->installing_ = true;
  ESP_LOGI(TAG, "Starting update");
#ifdef USE_OTA_STATE_LISTENER
  this->notify_state_(ota::OTA_STARTED, 0.0f, 0);
#endif

  // Own the URL, credentials and digest on the worker. The main loop may
  // process new commands while the worker is running.
  auto *install = new (std::nothrow) Install{this, *this->request_};
  if (install == nullptr || xTaskCreate(transfer_task_, "http_ota", 10240, install, 1, nullptr) != pdPASS) {
    ESP_LOGE(TAG, "Could not allocate HTTP OTA task in internal RAM");
    this->finish_transfer_(install, ota::OTA_RESPONSE_ERROR_UNKNOWN);
  }
}

void OtaHttpRequestComponent::transfer_task_(void *argument) {
  auto *install = static_cast<Install *>(argument);
  auto *owner = install->owner;
  const uint8_t result = owner->do_ota_(install->request);
  owner->defer([owner, install, result]() { owner->finish_transfer_(install, result); });
  vTaskDelete(nullptr);
}

void OtaHttpRequestComponent::finish_transfer_(Install *install, uint8_t ota_status) {
  if (install != nullptr) {
    this->request_->md5_computed = std::move(install->request.md5_computed);
    delete install;
  }
  this->installing_ = false;""",
    )
    source = replace_once(source, "uint8_t OtaHttpRequestComponent::do_ota_()", "uint8_t OtaHttpRequestComponent::do_ota_(Request &request)")
    source = replace_once(
        source,
        "std::string OtaHttpRequestComponent::get_url_with_auth_(const std::string &url)",
        "std::string OtaHttpRequestComponent::get_url_with_auth_(const Request &request, const std::string &url)",
    )
    source = replace_once(source, "bool OtaHttpRequestComponent::http_get_md5_()", "bool OtaHttpRequestComponent::http_get_md5_(Request &request)")
    for start, end in (
        ("uint8_t OtaHttpRequestComponent::do_ota_", "static std::string url_encode("),
        ("std::string OtaHttpRequestComponent::get_url_with_auth_", "bool OtaHttpRequestComponent::validate_url_"),
    ):
        left, right = source.index(start), source.index(end)
        body = source[left:right].replace("this->request_->", "request.")
        # The worker is not subscribed to the loop task's watchdog.
        body = "\n".join(line for line in body.split("\n") if "App.feed_wdt();" not in line)
        body = body.replace("this->get_url_with_auth_(request.", "this->get_url_with_auth_(request, request.")
        body = body.replace("this->http_get_md5_()", "this->http_get_md5_(request)")
        source = source[:left] + body + source[right:]
    return replace_once(
        source,
        "      this->notify_state_(ota::OTA_IN_PROGRESS, percentage, 0);",
        '      this->defer("http_ota_progress", [this, percentage]() {\n'
        '        if (this->installing_)\n'
        '          this->notify_state_(ota::OTA_IN_PROGRESS, percentage, 0);\n'
        '      });',
    )


def patch_update(source):
    source = replace_once(
        source,
        "void HttpRequestUpdate::update() {\n",
        "void HttpRequestUpdate::update() {\n"
        "  if (this->state_ == update::UPDATE_STATE_INSTALLING)\n"
        "    return;\n",
    )
    return replace_once(
        source,
        "void HttpRequestUpdate::perform(bool force) {\n",
        "void HttpRequestUpdate::perform(bool force) {\n"
        "  if (this->state_ == update::UPDATE_STATE_INSTALLING || this->task_result_ != nullptr)\n"
        "    return;\n",
    )


if (component / "ota/ota_http_request.cpp").is_file():
    update_file(component / "ota/ota_http_request.h", patch_header)
    update_file(component / "ota/ota_http_request.cpp", patch_transfer)
    update_file(component / "update/http_request_update.cpp", patch_update)
    print("ESPHome HTTP OTA install worker patch active")
