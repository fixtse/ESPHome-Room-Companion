Import("env")

from pathlib import Path


# Keep ESPHome's OTA backend, MD5 checks, update entity and reboot handling.
# Only recover the HTTP stream when a known-length download stalls.
ota_dir = (
    Path(env.subst("$PROJECT_SRC_DIR"))
    / "esphome"
    / "components"
    / "http_request"
    / "ota"
)
source_path = ota_dir / "ota_http_request.cpp"
header_path = ota_dir / "ota_http_request.h"

if source_path.is_file():
    source = source_path.read_text(encoding="utf-8")
    header = header_path.read_text(encoding="utf-8")
    backend_dir = ota_dir.parent.parent / "ota"
    backend_header = backend_dir / "ota_backend_esp_idf.h"
    backend_source = backend_dir / "ota_backend_esp_idf.cpp"

    def replace_once(text, original, replacement):
        if replacement in text:
            return text
        if text.count(original) != 1:
            raise RuntimeError("ESPHome HTTP OTA source changed. Review the OTA resume patch.")
        return text.replace(original, replacement, 1)

    backend_h = replace_once(
        backend_header.read_text(encoding="utf-8"),
        "  void set_update_md5(const char *md5)",
        """\
  OTAResponseTypes prepare_http_image(size_t image_size);
  void set_update_md5(const char *md5)""",
    )
    backend_cpp = replace_once(
        backend_source.read_text(encoding="utf-8"),
        "void IDFOTABackend::set_update_md5(const char *expected_md5) {",
        """\
OTAResponseTypes IDFOTABackend::prepare_http_image(size_t image_size) {
#ifdef USE_OTA_BLOCK_ERASE_AHEAD
  // Erase the inactive slot in bounded blocks before accepting the HTTP body.
  // Yield after each erase so Wi-Fi, idle and watchdog tasks can run.
  while (this->erased_end_ < image_size) {
    const size_t end = std::min(this->erased_end_ + OTA_BLOCK_ERASE_SIZE, image_size);
    const auto result = this->erase_ahead_(end);
    if (result != OTA_RESPONSE_OK)
      return result;
    delay(10);
  }
#endif
  return OTA_RESPONSE_OK;
}

void IDFOTABackend::set_update_md5(const char *expected_md5) {""",
    )
    backend_cpp = replace_once(
        backend_cpp,
        '#include "esphome/core/defines.h"',
        '#include "esphome/core/application.h"\n#include "esphome/core/defines.h"',
    )
    source = replace_once(
        source,
        "  // NOTE: HttpContainer::read()",
        """\
#ifdef USE_ESP32
  const size_t expected_length = container->content_length;
  if (expected_length != 0 && expected_length != SIZE_MAX) {
    container->end();
    ESP_LOGI(TAG, "Preparing inactive OTA slot for %u bytes", static_cast<unsigned>(expected_length));
    error_code = backend->prepare_http_image(expected_length);
    if (error_code != ota::OTA_RESPONSE_OK) {
      this->cleanup_(std::move(backend), container, /*abort_backend=*/true);
      return error_code;
    }
    auto stream = this->parent_->get(url_with_auth);
    if (stream == nullptr || stream->status_code != HTTP_STATUS_OK ||
        stream->content_length != expected_length) {
      if (stream != nullptr)
        stream->end();
      this->cleanup_(std::move(backend), container, /*abort_backend=*/true);
      return OTA_CONNECTION_ERROR;
    }
    container = std::move(stream);
  }
#endif

  // NOTE: HttpContainer::read()""",
    )
    header = replace_once(header, "HTTP_RECV_BUFFER = 256;", "HTTP_RECV_BUFFER = 1024;")
    source = replace_once(
        source,
        "  while (container->get_bytes_read() < container->content_length) {",
        """\
  const size_t original_length = container->content_length;
  size_t written = 0;
  unsigned resume_attempts = 0;
  const auto reopen = [&]() {
    if (written == 0 || original_length == 0 || original_length == SIZE_MAX ||
        written >= original_length) {
      return false;
    }
    const std::vector<Header> headers{{"Range", "bytes=" + std::to_string(written) + "-"}};
    const std::string expected_range =
        "bytes " + std::to_string(written) + "-" + std::to_string(original_length - 1) +
        "/" + std::to_string(original_length);
    container->end();
    while (resume_attempts < 3) {
      resume_attempts++;
      ESP_LOGW(TAG, "Retrying OTA download at byte %u of %u (attempt %u/3)",
               static_cast<unsigned>(written), static_cast<unsigned>(original_length), resume_attempts);
      auto partial = this->parent_->get(url_with_auth, headers, {"content-range"});
      if (partial != nullptr) {
        if (partial->status_code == HTTP_STATUS_PARTIAL_CONTENT &&
            partial->content_length == original_length - written &&
            partial->get_response_header("content-range") == expected_range) {
          container = std::move(partial);
          last_data_time = millis();
          return true;
        }
        ESP_LOGW(TAG, "OTA server did not return the requested byte range");
        partial->end();
      }
      delay(100);
    }
    return false;
  };

  while (container->get_bytes_read() < container->content_length) {""",
    )
    source = replace_once(
        source,
        """\
      this->cleanup_(std::move(backend), container, /*abort_backend=*/true);
      return OTA_CONNECTION_ERROR;
    }

    // At this point""",
        """\
      if (reopen())
        continue;
      this->cleanup_(std::move(backend), container, /*abort_backend=*/true);
      return OTA_CONNECTION_ERROR;
    }

    // At this point""",
    )
    source = replace_once(
        source,
        """\
    }

    uint32_t now = millis();
    if ((now - last_progress > 1000) or (container->get_bytes_read() == container->content_length)) {
      last_progress = now;
      float percentage = container->get_bytes_read() * 100.0f / container->content_length;""",
        """\
      written += static_cast<size_t>(bufsize_or_error);
    }

    uint32_t now = millis();
    if ((now - last_progress > 1000) or (container->get_bytes_read() == container->content_length)) {
      last_progress = now;
      const size_t length = original_length == SIZE_MAX ? container->content_length : original_length;
      float percentage = written * 100.0f / length;""",
    )
    if header_path.read_text(encoding="utf-8") != header:
        header_path.write_text(header, encoding="utf-8")
    if source_path.read_text(encoding="utf-8") != source:
        source_path.write_text(source, encoding="utf-8")
    if backend_header.read_text(encoding="utf-8") != backend_h:
        backend_header.write_text(backend_h, encoding="utf-8")
    if backend_source.read_text(encoding="utf-8") != backend_cpp:
        backend_source.write_text(backend_cpp, encoding="utf-8")
    print("ESPHome HTTP OTA slot preparation and byte-range resume patch active")
