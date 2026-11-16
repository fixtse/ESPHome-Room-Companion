#pragma once
#include "lvgl.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include "esp_heap_caps.h"
#include "onx_emoji_pixels.h"
#include <vector>
#include <deque>
#include <cstring>
#include <string>
#include <string_view>
#include <algorithm>

namespace onx_emoji {
struct CachedGlyph { uint32_t code; uint16_t size; lv_image_dsc_t image; };
inline constexpr uint32_t FIRST_GLYPH = 0xF0000;
inline std::deque<CachedGlyph> cache;
inline size_t cache_bytes = 0;
inline constexpr size_t CACHE_LIMIT = 512 * 1024;
// LVGL shapes one codepoint at a time: collapse a recognized sequence to a
// supplementary private-use glyph before layout, preserving one advance width.
inline std::string encode(const char *text) {
  std::string normalized;
  for (size_t at = 0; text[at];) {
    if (std::strncmp(text + at, "\xEF\xB8\x8F", 3) == 0) { at += 3; continue; }
    normalized += text[at++];
  }
  std::string result;
  for (size_t at = 0; at < normalized.size();) {
    bool matched = false;
    size_t matched_length = 0, matched_index = 0;
    const auto *begin = std::begin(GLYPHS);
    const auto *end = std::end(GLYPHS);
    // Narrow the candidate range as UTF-8 bytes arrive; stop as soon as no
    // glyph has this prefix. Ordinary text usually needs only one lookup.
    for (size_t length = 1; at + length <= normalized.size(); ++length) {
      const std::string_view prefix(normalized.data() + at, length);
      const auto *candidate = std::lower_bound(begin, end, prefix,
        [](const Glyph &glyph, std::string_view key) {
          return std::string_view(glyph.sequence) < key;
        });
      if (candidate == end) break;
      const std::string_view sequence(candidate->sequence);
      if (sequence.substr(0, length) != prefix) break;
      begin = candidate;
      if (sequence.size() == length) {
        matched_length = length;
        matched_index = candidate - std::begin(GLYPHS);
      }
    }
    if (matched_length) {
      const uint32_t code = FIRST_GLYPH + matched_index;
      result += static_cast<char>(0xF0 | (code >> 18));
      result += static_cast<char>(0x80 | ((code >> 12) & 63));
      result += static_cast<char>(0x80 | ((code >> 6) & 63));
      result += static_cast<char>(0x80 | (code & 63));
      at += matched_length;
      matched = true;
    }
    if (!matched) result += normalized[at++];
  }
  return result;
}
inline const void *source(const lv_font_t *font, uint32_t code, uint32_t, int32_t *offset_y, void *) {
  // LVGL: top = line_top + line_height - base_line - image_height - offset_y.
  // Full-line images need to include the descent to stay inside the label.
  if (offset_y) *offset_y = -static_cast<int32_t>(font->base_line);
  const uint16_t size = font->line_height;
  for (const auto &entry : cache) {
    if (entry.code == code && entry.size == size) {
      return &entry.image;
    }
  }
  if (code >= FIRST_GLYPH && code - FIRST_GLYPH < sizeof(GLYPHS) / sizeof(GLYPHS[0])) {
    const auto &glyph = GLYPHS[code - FIRST_GLYPH];
    uint8_t decoded[96 + ATLAS_SIZE * ATLAS_SIZE];
    size_t input = 0, output = 0;
    while (input < glyph.length && output < sizeof(decoded)) {
      const uint8_t control = glyph.pixels[input++];
      const size_t count = control < 128 ? control + 1 : control - 127;
      if (output + count > sizeof(decoded)) return nullptr;
      if (control < 128) {
        if (input + count > glyph.length) return nullptr;
        std::memcpy(decoded + output, glyph.pixels + input, count);
        input += count;
      } else {
        if (input >= glyph.length) return nullptr;
        std::memset(decoded + output, glyph.pixels[input++], count);
      }
      output += count;
    }
    if (output != sizeof(decoded)) return nullptr;
    auto *pixels = static_cast<uint8_t *>(heap_caps_malloc(size * size * 4, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!pixels) return nullptr;  // Preserve the original monochrome fallback.
    for (uint16_t y = 0; y < size; ++y) {
      for (uint16_t x = 0; x < size; ++x) {
        const auto *src = decoded + decoded[96 + (y * ATLAS_SIZE / size) * ATLAS_SIZE + x * ATLAS_SIZE / size] * 3;
        auto *dst = pixels + (y * size + x) * 4;
        const uint16_t rgb = src[0] | (src[1] << 8);
        dst[0] = (rgb & 31) * 255 / 31;
        dst[1] = ((rgb >> 5) & 63) * 255 / 63;
        dst[2] = (rgb >> 11) * 255 / 31;
        dst[3] = src[2];
      }
    }
    lv_image_dsc_t image{};
    image.header.magic = LV_IMAGE_HEADER_MAGIC;
    image.header.cf = LV_COLOR_FORMAT_ARGB8888;
    image.header.stride = size * 4;
    image.header.w = size;
    image.header.h = size;
    image.data_size = size * size * 4;
    image.data = pixels;
    cache.push_back({code, size, image});
    cache_bytes += size * size * 4;
    return &cache.back().image;
  }
  return nullptr;
}
// Drawing is deferred: retire cached descriptors only after the frame completes.
inline void trim_cache(lv_event_t *) {
  if (cache_bytes <= CACHE_LIMIT) return;
  for (const auto &entry : cache) {
    lv_image_cache_drop(&entry.image);
    heap_caps_free(const_cast<uint8_t *>(entry.image.data));
  }
  cache.clear();
  cache_bytes = 0;
}
struct Font { const lv_font_t *original; lv_font_t *color; };
inline std::vector<Font> fonts;
inline void enable(lv_obj_t *label) {
  const auto *original = lv_obj_get_style_text_font(label, LV_PART_MAIN);
  for (const auto &entry : fonts) {
    if (entry.color == original) return;
    if (entry.original == original) {
      lv_obj_set_style_text_font(label, entry.color, LV_PART_MAIN);
      return;
    }
  }
  if (fonts.empty()) lv_display_add_event_cb(lv_display_get_default(), trim_cache, LV_EVENT_REFR_READY, nullptr);
  auto *color = lv_imgfont_create(original->line_height, source, nullptr);
  if (!color) return;
  color->base_line = original->base_line;
  color->fallback = original;
  fonts.push_back({original, color});
  lv_obj_set_style_text_font(label, color, LV_PART_MAIN);
}
}
