#pragma once
// PSRAM RGB565 compositor and cache for Action Grid pages.
//
// Action grids can hold up to 16 rounded keycaps with icons and wrapped labels.
// Drawing all 16 widget subtrees through LVGL software rendering on every page
// visit or single-key toggle is slow on ESP32-S3. Instead, each visited grid
// page keeps one flat RGB565 bitmap of its body area in PSRAM (up to 4 cached
// pages; ~248 KB each on 3.5", ~109 KB on 2.8").
//
// - Revisiting a cached grid page blits one flat RGB565 lv_image (memcpy speed).
// - Toggling a single key re-rasterizes ONLY that tile's rectangle in PSRAM and
//   invalidates ONLY that tile's area on screen.
// - Downscaled monochrome icons and NVS custom artwork stay cached in PSRAM so
//   neither CPU area-averaging nor NVS reads repeat on page switches.
// - Uses pure C++ pixel composition + LVGL's public font glyph reader; never
//   uses lv_snapshot_take() or display flush hooks.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include "lvgl.h"
#if defined(__has_include)
#if __has_include("lvgl_private.h")
#include "lvgl_private.h"
#define ONX_GRID_HAVE_DECODER_DSC 1
#elif __has_include("draw/lv_image_decoder_private.h")
#include "draw/lv_image_decoder_private.h"
#define ONX_GRID_HAVE_DECODER_DSC 1
#endif
#endif
#ifndef ONX_GRID_HAVE_DECODER_DSC
#define ONX_GRID_HAVE_DECODER_DSC 0
#endif
#include "esp_heap_caps.h"
#include "onx_display_settings.h"
#include "onx_emoji.h"
#include "onx_panel_artwork.h"

namespace onx_grid_cache {

inline uint16_t pack565(uint32_t rgb) {
  const uint32_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
  return static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

inline uint16_t blend565(uint16_t dst, uint32_t fg_rgb, uint8_t alpha) {
  if (alpha == 0) return dst;
  if (alpha == 255) return pack565(fg_rgb);
  const uint32_t r5 = (dst >> 11) & 31, g6 = (dst >> 5) & 63, b5 = dst & 31;
  const uint32_t dr = (r5 << 3) | (r5 >> 2);
  const uint32_t dg = (g6 << 2) | (g6 >> 4);
  const uint32_t db = (b5 << 3) | (b5 >> 2);
  const uint32_t sr = (fg_rgb >> 16) & 0xFF, sg = (fg_rgb >> 8) & 0xFF, sb = fg_rgb & 0xFF;
  const uint32_t inv = 255 - alpha;
  const uint32_t rt = sr * alpha + dr * inv + 128;
  const uint32_t gt = sg * alpha + dg * inv + 128;
  const uint32_t bt = sb * alpha + db * inv + 128;
  const uint32_t r = (rt + (rt >> 8)) >> 8;
  const uint32_t g = (gt + (gt >> 8)) >> 8;
  const uint32_t b = (bt + (bt >> 8)) >> 8;
  return static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

inline uint16_t blend565_px(uint16_t dst, uint16_t src, uint8_t alpha) {
  if (alpha == 0) return dst;
  if (alpha == 255) return src;
  const uint32_t r5 = (src >> 11) & 31, g6 = (src >> 5) & 63, b5 = src & 31;
  const uint32_t sr = (r5 << 3) | (r5 >> 2);
  const uint32_t sg = (g6 << 2) | (g6 >> 4);
  const uint32_t sb = (b5 << 3) | (b5 >> 2);
  return blend565(dst, (sr << 16) | (sg << 8) | sb, alpha);
}

struct Clip { int x0, y0, x1, y1; };

inline void fill_rect(uint16_t *buf, int bw, int bh, int x, int y, int w, int h, uint16_t c) {
  const int x0 = std::max(0, x), y0 = std::max(0, y);
  const int x1 = std::min(bw, x + w), y1 = std::min(bh, y + h);
  for (int py = y0; py < y1; ++py) {
    uint16_t *row = buf + size_t(py) * bw + x0;
    for (int px = x0; px < x1; ++px) *row++ = c;
  }
}

// Anti-aliased coverage (0..255) at pixel (px, py) inside rounded rect [rx..rx+rw, ry..ry+rh].
inline uint8_t rrect_cov(int px, int py, int rx, int ry, int rw, int rh, int radius) {
  if (px < rx || py < ry || px >= rx + rw || py >= ry + rh) return 0;
  if (radius <= 0) return 255;
  const int r = std::min(radius, std::min(rw, rh) / 2);
  int cx = -1, cy = -1;
  if (px < rx + r) cx = rx + r;
  else if (px >= rx + rw - r) cx = rx + rw - r;
  if (py < ry + r) cy = ry + r;
  else if (py >= ry + rh - r) cy = ry + rh - r;
  if (cx < 0 || cy < 0) return 255;
  const float dx = (px + 0.5f) - cx, dy = (py + 0.5f) - cy;
  const float d = std::sqrt(dx * dx + dy * dy);
  const float edge = float(r) - d;
  if (edge >= 0.5f) return 255;
  if (edge <= -0.5f) return 0;
  return static_cast<uint8_t>(std::lround((edge + 0.5f) * 255.0f));
}

// Draws a rounded rectangle with optional anti-aliased inner border.
inline void draw_rounded_box(uint16_t *buf, int bw, int bh, int x, int y, int w, int h, int radius,
                             uint32_t fill_rgb, uint8_t fill_opa, int border_w, uint32_t border_rgb) {
  const int x0 = std::max(0, x), y0 = std::max(0, y);
  const int x1 = std::min(bw, x + w), y1 = std::min(bh, y + h);
  const int r = std::min(radius, std::min(w, h) / 2);
  const uint16_t fill565 = pack565(fill_rgb);
  const uint16_t border565 = pack565(border_rgb);
  const int inner_r = std::max(0, r - border_w);
  for (int py = y0; py < y1; ++py) {
    uint16_t *row = buf + size_t(py) * bw;
    const bool corner_y = (py < y + r) || (py >= y + h - r);
    const bool border_y = border_w > 0 && ((py < y + border_w) || (py >= y + h - border_w));
    if (!corner_y && !border_y && fill_opa == 255) {
      int lx = x0, rx = x1;
      if (border_w > 0) {
        const int bl = std::min(x1, x + border_w);
        const int br = std::max(x0, x + w - border_w);
        for (int px = lx; px < bl; ++px) row[px] = border565;
        for (int px = br; px < rx; ++px) row[px] = border565;
        lx = std::max(lx, bl);
        rx = std::min(rx, br);
      }
      for (int px = lx; px < rx; ++px) row[px] = fill565;
      continue;
    }
    for (int px = x0; px < x1; ++px) {
      const uint8_t out_a = rrect_cov(px, py, x, y, w, h, r);
      if (out_a == 0) continue;
      if (border_w > 0) {
        const uint8_t in_a = rrect_cov(px, py, x + border_w, y + border_w, w - 2 * border_w, h - 2 * border_w, inner_r);
        if (in_a == 255) {
          row[px] = fill_opa == 255 ? fill565 : blend565(row[px], fill_rgb, fill_opa);
        } else if (in_a == 0) {
          row[px] = out_a == 255 ? border565 : blend565(row[px], border_rgb, out_a);
        } else {
          // Anti-aliased transition between inner fill and border rim.
          const uint16_t bg_fill = fill_opa == 255 ? fill565 : blend565(row[px], fill_rgb, fill_opa);
          const uint16_t mixed = blend565_px(border565, bg_fill, in_a);
          row[px] = out_a == 255 ? mixed : blend565_px(row[px], mixed, out_a);
        }
      } else {
        const uint8_t a = fill_opa == 255 ? out_a : uint8_t((uint32_t(out_a) * fill_opa + 127) / 255);
        row[px] = a == 255 ? fill565 : blend565(row[px], fill_rgb, a);
      }
    }
  }
}

inline void draw_disc(uint16_t *buf, int bw, int bh, float cx, float cy, float radius, uint32_t rgb) {
  const int x0 = std::max(0, int(std::floor(cx - radius - 1.0f)));
  const int y0 = std::max(0, int(std::floor(cy - radius - 1.0f)));
  const int x1 = std::min(bw, int(std::ceil(cx + radius + 1.0f)));
  const int y1 = std::min(bh, int(std::ceil(cy + radius + 1.0f)));
  for (int py = y0; py < y1; ++py) {
    uint16_t *row = buf + size_t(py) * bw;
    for (int px = x0; px < x1; ++px) {
      const float dx = (px + 0.5f) - cx, dy = (py + 0.5f) - cy;
      const float edge = radius - std::sqrt(dx * dx + dy * dy);
      if (edge <= -0.5f) continue;
      const uint8_t a = edge >= 0.5f ? 255 : static_cast<uint8_t>(std::lround((edge + 0.5f) * 255.0f));
      row[px] = blend565(row[px], rgb, a);
    }
  }
}

// ------------------------------------------------------------ PSRAM scaled-alpha icon cache
struct ScaledAlpha {
  const void *src_ptr{nullptr};
  std::string file_key;
  int req_size{0}, w{0}, h{0};
  uint8_t *alpha{nullptr};
  uint32_t used{0};
  void release() {
    if (alpha) { heap_caps_free(alpha); alpha = nullptr; }
    src_ptr = nullptr; file_key.clear(); req_size = w = h = 0; used = 0;
  }
};

constexpr size_t SCALED_ICON_CACHE_LIMIT = 64;
inline std::array<ScaledAlpha, SCALED_ICON_CACHE_LIMIT> scaled_icons{};
inline uint32_t scaled_icon_clock = 0;

inline bool extract_alpha(const uint8_t *data, uint32_t cf, int w, int h, uint32_t stride, int x, int y, uint8_t &a) {
  if (x < 0 || y < 0 || x >= w || y >= h || !data) return false;
  switch (cf) {
    case LV_COLOR_FORMAT_ARGB8888: a = data[size_t(y) * (stride ? stride : w * 4) + x * 4 + 3]; return true;
    case LV_COLOR_FORMAT_RGB565A8: a = data[size_t(stride ? stride : w * 2) * h + size_t(y) * w + x]; return true;
    case LV_COLOR_FORMAT_A8: a = data[size_t(y) * (stride ? stride : w) + x]; return true;
    default: return false;
  }
}

inline const ScaledAlpha *get_scaled_alpha(const void *src, int size) {
  if (!src || size <= 0) return nullptr;
  const auto type = lv_image_src_get_type(src);
  std::string fkey;
  if (type == LV_IMAGE_SRC_FILE) fkey = static_cast<const char *>(src);
  else if (type != LV_IMAGE_SRC_VARIABLE) return nullptr;

  for (auto &entry : scaled_icons) {
    if (!entry.alpha || entry.req_size != size) continue;
    if (type == LV_IMAGE_SRC_VARIABLE ? entry.src_ptr == src : entry.file_key == fkey) {
      entry.used = ++scaled_icon_clock;
      return &entry;
    }
  }

  const uint8_t *src_data = nullptr;
  uint32_t cf = 0, stride = 0;
  int sw = 0, sh = 0;
#if ONX_GRID_HAVE_DECODER_DSC
  lv_image_decoder_dsc_t dec{};
  bool decoded_open = false;
#endif

  if (type == LV_IMAGE_SRC_VARIABLE) {
    const auto *d = static_cast<const lv_image_dsc_t *>(src);
    src_data = d->data; cf = d->header.cf; sw = d->header.w; sh = d->header.h; stride = d->header.stride;
  } else {
#if ONX_GRID_HAVE_DECODER_DSC
    if (lv_image_decoder_open(&dec, src, nullptr) != LV_RESULT_OK || !dec.decoded || !dec.decoded->data)
      return nullptr;
    decoded_open = true;
    src_data = dec.decoded->data;
    cf = dec.decoded->header.cf;
    sw = dec.decoded->header.w;
    sh = dec.decoded->header.h;
    stride = dec.decoded->header.stride;
#else
    return nullptr;
#endif
  }

  uint8_t probe = 0;
  if (sw <= 0 || sh <= 0 || !extract_alpha(src_data, cf, sw, sh, stride, 0, 0, probe)) {
#if ONX_GRID_HAVE_DECODER_DSC
    if (decoded_open) lv_image_decoder_close(&dec);
#endif
    return nullptr;
  }

  const int ow = std::max(1, sw >= sh ? size : size * sw / sh);
  const int oh = std::max(1, sh >= sw ? size : size * sh / sw);
  auto *pixels = static_cast<uint8_t *>(heap_caps_malloc(size_t(ow) * oh, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!pixels) {
#if ONX_GRID_HAVE_DECODER_DSC
    if (decoded_open) lv_image_decoder_close(&dec);
#endif
    return nullptr;
  }

  for (int y = 0; y < oh; ++y) {
    const int y0 = y * sh / oh, y1 = std::max(y0 + 1, (y + 1) * sh / oh);
    for (int x = 0; x < ow; ++x) {
      const int x0 = x * sw / ow, x1 = std::max(x0 + 1, (x + 1) * sw / ow);
      uint32_t sum = 0, n = 0;
      for (int yy = y0; yy < y1; ++yy)
        for (int xx = x0; xx < x1; ++xx) {
          uint8_t a = 0;
          extract_alpha(src_data, cf, sw, sh, stride, xx, yy, a);
          sum += a;
          ++n;
        }
      pixels[size_t(y) * ow + x] = n ? static_cast<uint8_t>(sum / n) : 0;
    }
  }
#if ONX_GRID_HAVE_DECODER_DSC
  if (decoded_open) lv_image_decoder_close(&dec);
#endif

  auto victim = scaled_icons.begin();
  for (auto it = scaled_icons.begin(); it != scaled_icons.end(); ++it) {
    if (!it->alpha) { victim = it; break; }
    if (it->used < victim->used) victim = it;
  }
  victim->release();
  victim->src_ptr = type == LV_IMAGE_SRC_VARIABLE ? src : nullptr;
  victim->file_key = std::move(fkey);
  victim->req_size = size;
  victim->w = ow;
  victim->h = oh;
  victim->alpha = pixels;
  victim->used = ++scaled_icon_clock;
  return &*victim;
}

inline void invalidate_scaled_icon(const void *src) {
  if (!src) return;
  for (auto &entry : scaled_icons) if (entry.src_ptr == src) entry.release();
}

inline void blend_alpha_icon(uint16_t *buf, int bw, int bh, const ScaledAlpha &icon, int cx, int cy,
                             uint32_t fg_rgb, const Clip &clip) {
  const int ox = cx - icon.w / 2, oy = cy - icon.h / 2;
  const int x0 = std::max({0, clip.x0, ox}), y0 = std::max({0, clip.y0, oy});
  const int x1 = std::min({bw, clip.x1, ox + icon.w}), y1 = std::min({bh, clip.y1, oy + icon.h});
  for (int py = y0; py < y1; ++py) {
    uint16_t *dst = buf + size_t(py) * bw;
    const uint8_t *src = icon.alpha + size_t(py - oy) * icon.w;
    for (int px = x0; px < x1; ++px) {
      const uint8_t a = src[px - ox];
      if (a) dst[px] = blend565(dst[px], fg_rgb, a);
    }
  }
}

// ------------------------------------------------------------ PSRAM custom artwork cache
struct ArtworkSlot {
  onx_panel_artwork::Image image;
  uint32_t used{0};
};
constexpr size_t ARTWORK_CACHE_LIMIT = 32;
inline onx_memory::StateArray<ArtworkSlot, ARTWORK_CACHE_LIMIT> artwork_lru;
inline uint32_t artwork_lru_clock = 0;

inline onx_panel_artwork::Image *get_artwork(uint64_t id) {
  if (!id) return nullptr;
  for (size_t i = 0; i < ARTWORK_CACHE_LIMIT; ++i) {
    if (artwork_lru[i].image.id == id && artwork_lru[i].image.source) {
      artwork_lru[i].used = ++artwork_lru_clock;
      return &artwork_lru[i].image;
    }
  }
  size_t victim = 0;
  for (size_t i = 0; i < ARTWORK_CACHE_LIMIT; ++i) {
    if (!artwork_lru[i].image.id) { victim = i; break; }
    if (artwork_lru[i].used < artwork_lru[victim].used) victim = i;
  }
  if (!artwork_lru[victim].image.load(id)) return nullptr;
  artwork_lru[victim].used = ++artwork_lru_clock;
  return &artwork_lru[victim].image;
}

inline void blit_rgb565_scaled(uint16_t *buf, int bw, int bh, const lv_image_dsc_t *src,
                               int cx, int cy, int target_w, int target_h,
                               int clip_rx, int clip_ry, int clip_rw, int clip_rh, int clip_radius) {
  if (!src || !src->data || src->header.cf != LV_COLOR_FORMAT_RGB565 || src->header.w == 0 || src->header.h == 0) return;
  const int sw = src->header.w, sh = src->header.h;
  const auto *sp = reinterpret_cast<const uint16_t *>(src->data);
  const int stride = src->header.stride ? src->header.stride / 2 : sw;
  const int ox = cx - target_w / 2, oy = cy - target_h / 2;
  const int x0 = std::max({0, clip_rx, ox}), y0 = std::max({0, clip_ry, oy});
  const int x1 = std::min({bw, clip_rx + clip_rw, ox + target_w}), y1 = std::min({bh, clip_ry + clip_rh, oy + target_h});
  for (int py = y0; py < y1; ++py) {
    const int sy = std::clamp((py - oy) * sh / std::max(1, target_h), 0, sh - 1);
    const uint16_t *srow = sp + size_t(sy) * stride;
    uint16_t *drow = buf + size_t(py) * bw;
    for (int px = x0; px < x1; ++px) {
      const uint8_t a = clip_radius > 0 ? rrect_cov(px, py, clip_rx, clip_ry, clip_rw, clip_rh, clip_radius) : 255;
      if (!a) continue;
      const int sx = std::clamp((px - ox) * sw / std::max(1, target_w), 0, sw - 1);
      drow[px] = a == 255 ? srow[sx] : blend565_px(drow[px], srow[sx], a);
    }
  }
}

// ------------------------------------------------------------ PSRAM glyph text renderer
inline uint32_t next_utf8(const std::string &s, size_t &i) {
  const auto c = static_cast<unsigned char>(s[i]);
  if (c < 0x80) { ++i; return c; }
  uint32_t cp = 0; int n = 0;
  if ((c >> 5) == 6) { cp = c & 0x1F; n = 1; }
  else if ((c >> 4) == 14) { cp = c & 0x0F; n = 2; }
  else if ((c >> 3) == 30) { cp = c & 0x07; n = 3; }
  else { ++i; return 0xFFFD; }
  ++i;
  for (int k = 0; k < n && i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80; ++k)
    cp = (cp << 6) | (static_cast<unsigned char>(s[i++]) & 0x3F);
  return cp;
}

inline int measure_text(const std::string &s, const lv_font_t *font) {
  if (!font || s.empty()) return 0;
  lv_point_t sz{};
  lv_text_get_size(&sz, s.c_str(), font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  return sz.x;
}

inline std::string truncate_line(const std::string &s, const lv_font_t *font, int max_w) {
  if (measure_text(s, font) <= max_w) return s;
  const std::string dots = "...";
  std::string out;
  size_t i = 0;
  while (i < s.size()) {
    const size_t prev = i;
    next_utf8(s, i);
    const std::string cand = s.substr(0, i);
    if (measure_text(cand + dots, font) > max_w) break;
    out = cand;
    (void)prev;
  }
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out + dots;
}

inline std::vector<std::string> wrap_lines(const std::string &text, const lv_font_t *font, int max_w, int max_lines) {
  std::vector<std::string> out;
  if (!font || text.empty() || max_w <= 0 || max_lines <= 0) return out;
  if (max_lines == 1 || measure_text(text, font) <= max_w) {
    out.push_back(truncate_line(text, font, max_w));
    return out;
  }
  // Word wrap up to max_lines; last line truncates with "...".
  size_t pos = 0;
  while (pos < text.size() && int(out.size()) < max_lines) {
    while (pos < text.size() && text[pos] == ' ') ++pos;
    if (pos >= text.size()) break;
    const std::string rest = text.substr(pos);
    if (int(out.size()) == max_lines - 1) {
      out.push_back(truncate_line(rest, font, max_w));
      break;
    }
    if (measure_text(rest, font) <= max_w) {
      out.push_back(rest);
      break;
    }
    // Find longest prefix fitting max_w, preferring a word boundary.
    size_t i = pos, fit_end = pos, word_end = pos;
    while (i < text.size()) {
      const size_t step_start = i;
      const uint32_t cp = next_utf8(text, i);
      if (measure_text(text.substr(pos, i - pos), font) > max_w) {
        if (fit_end == pos) fit_end = i;  // Ensure progress on single wide glyph.
        break;
      }
      fit_end = i;
      if (cp == ' ' || cp == '-' || cp == '/') word_end = cp == ' ' ? step_start : i;
      else if (i < text.size() && text[i] == ' ') word_end = i;
    }
    const size_t take = word_end > pos ? word_end : fit_end;
    std::string line = text.substr(pos, take - pos);
    while (!line.empty() && line.back() == ' ') line.pop_back();
    out.push_back(std::move(line));
    pos = take;
  }
  return out;
}

// Reusable scratch draw buffer for LVGL glyph bitmap extraction (PSRAM).
inline lv_draw_buf_t *glyph_scratch(uint32_t w, uint32_t h) {
  static lv_draw_buf_t *buf = nullptr;
  static uint32_t cap_w = 0, cap_h = 0;
  if (w == 0 || h == 0) return nullptr;
  if (buf && w <= cap_w && h <= cap_h) {
    if (auto *r = lv_draw_buf_reshape(buf, LV_COLOR_FORMAT_UNKNOWN, w, h, LV_STRIDE_AUTO)) return r;
  }
  if (buf) { lv_draw_buf_destroy(buf); buf = nullptr; cap_w = cap_h = 0; }
  cap_w = std::max<uint32_t>(64, (w + 15) & ~15u);
  cap_h = std::max<uint32_t>(64, (h + 15) & ~15u);
  buf = lv_draw_buf_create(cap_w, cap_h, LV_COLOR_FORMAT_A8, LV_STRIDE_AUTO);
  if (!buf) { cap_w = cap_h = 0; return nullptr; }
  return lv_draw_buf_reshape(buf, LV_COLOR_FORMAT_UNKNOWN, w, h, LV_STRIDE_AUTO);
}

inline bool needs_emoji_overlay(const std::string &text) {
  bool missing = false;
  const auto encoded = onx_emoji::encode(text.c_str(), &missing);
  size_t i = 0;
  while (i < encoded.size()) {
    if (next_utf8(encoded, i) >= onx_emoji::FIRST_GLYPH) return true;
  }
  return false;
}

inline void draw_text_aligned(uint16_t *buf, int bw, int bh, const std::vector<std::string> &lines,
                              const lv_font_t *font, uint32_t fg_rgb, int rx, int ry, int rw, bool centered, const Clip &clip) {
  if (!font || lines.empty()) return;
  const int lh = lv_font_get_line_height(font);
  const int ascent = font->line_height - font->base_line;
  for (size_t li = 0; li < lines.size(); ++li) {
    const auto &line = lines[li];
    if (line.empty()) continue;
    const int lw = centered ? measure_text(line, font) : 0;
    int pen_x = centered ? rx + (rw - lw) / 2 : rx;
    const int line_y = ry + int(li) * lh;
    size_t i = 0;
    while (i < line.size()) {
      const uint32_t cp = next_utf8(line, i);
      size_t peek = i;
      const uint32_t next_cp = i < line.size() ? next_utf8(line, peek) : 0;
      lv_font_glyph_dsc_t g{};
      if (!lv_font_get_glyph_dsc(font, &g, cp, next_cp)) continue;
      if (g.box_w > 0 && g.box_h > 0 && g.format > LV_FONT_GLYPH_FORMAT_NONE && g.format < LV_FONT_GLYPH_FORMAT_IMAGE) {
        if (auto *scratch = glyph_scratch(g.box_w, g.box_h)) {
          const auto *bm = static_cast<const lv_draw_buf_t *>(lv_font_get_glyph_bitmap(&g, scratch));
          if (bm && bm->data) {
            const int gx = pen_x + g.ofs_x;
            const int gy = line_y + ascent - g.box_h - g.ofs_y;
            const int x0 = std::max({0, clip.x0, gx}), y0 = std::max({0, clip.y0, gy});
            const int x1 = std::min({bw, clip.x1, gx + int(g.box_w)}), y1 = std::min({bh, clip.y1, gy + int(g.box_h)});
            const uint32_t stride = bm->header.stride ? bm->header.stride : g.box_w;
            for (int py = y0; py < y1; ++py) {
              uint16_t *dst = buf + size_t(py) * bw;
              const uint8_t *src = bm->data + size_t(py - gy) * stride;
              for (int px = x0; px < x1; ++px) {
                const uint8_t a = src[px - gx];
                if (a) dst[px] = blend565(dst[px], fg_rgb, a);
              }
            }
          }
        }
      }
      lv_font_glyph_release_draw_data(&g);
      pen_x += g.adv_w;
    }
  }
}

inline void draw_text_centered(uint16_t *buf, int bw, int bh, const std::vector<std::string> &lines,
                               const lv_font_t *font, uint32_t fg_rgb, int rx, int ry, int rw, const Clip &clip) {
  draw_text_aligned(buf, bw, bh, lines, font, fg_rgb, rx, ry, rw, true, clip);
}

// ------------------------------------------------------------ PSRAM grid page framebuffer cache
struct GridPage {
  std::string key;
  int w{0}, h{0}, top_y{0};
  uint32_t palette_sig{0};
  uint16_t *pixels{nullptr};
  size_t capacity{0};
  lv_image_dsc_t dsc{};
  int tile_count{0};
  std::array<uint64_t, 16> tile_sigs{};
  uint32_t used{0};
  bool valid{false};

  bool ensure_buffer(int width, int height) {
    const size_t need = size_t(width) * height;
    if (pixels && capacity >= need) {
      w = width; h = height;
      setup_dsc();
      return true;
    }
    if (pixels) { lv_image_cache_drop(&dsc); heap_caps_free(pixels); pixels = nullptr; capacity = 0; }
    pixels = static_cast<uint16_t *>(heap_caps_malloc(need * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!pixels) return false;
    capacity = need; w = width; h = height;
    setup_dsc();
    return true;
  }
  void setup_dsc() {
    dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    dsc.header.w = w;
    dsc.header.h = h;
    dsc.header.stride = w * 2;
    dsc.data_size = uint32_t(w) * h * 2;
    dsc.data = reinterpret_cast<const uint8_t *>(pixels);
  }
  void invalidate_all() {
    valid = false;
    tile_sigs.fill(0);
  }
};

constexpr size_t GRID_PAGE_CACHE_LIMIT = 4;
inline std::array<GridPage, GRID_PAGE_CACHE_LIMIT> grid_pages{};
inline uint32_t grid_page_clock = 0;

inline bool has_valid_page(const std::string &key, int w, int h, int top_y, uint32_t palette_sig, int tile_count) {
  for (const auto &p : grid_pages) {
    if (p.key == key && p.pixels && p.valid && p.w == w && p.h == h &&
        p.top_y == top_y && p.palette_sig == palette_sig && p.tile_count == tile_count)
      return true;
  }
  return false;
}

inline GridPage *acquire_page(const std::string &key, int w, int h, int top_y, uint32_t palette_sig, int tile_count,
                              bool &full_redraw, const GridPage *protect = nullptr) {
  full_redraw = false;
  GridPage *slot = nullptr;
  for (auto &p : grid_pages) {
    if (p.key == key && p.pixels) { slot = &p; break; }
  }
  if (!slot) {
    for (auto &p : grid_pages) {
      if (&p == protect) continue;
      if (!p.pixels) { slot = &p; break; }
      if (!slot || p.used < slot->used) slot = &p;
    }
    if (!slot) slot = &grid_pages[0];
    slot->key = key;
    slot->invalidate_all();
    full_redraw = true;
  }
  if (!slot->ensure_buffer(w, h)) return nullptr;
  if (!slot->valid || slot->top_y != top_y || slot->palette_sig != palette_sig || slot->tile_count != tile_count) {
    slot->top_y = top_y;
    slot->palette_sig = palette_sig;
    slot->tile_count = tile_count;
    slot->invalidate_all();
    full_redraw = true;
  }
  slot->used = ++grid_page_clock;
  return slot;
}

inline void invalidate_all_pages() {
  for (auto &p : grid_pages) p.invalidate_all();
}

}  // namespace onx_grid_cache
