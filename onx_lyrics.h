#pragma once
// Synced lyrics pushed by Home Assistant (blueprints/room_companion_lyrics.yaml).
// Home Assistant fetches an LRC text from LRCLIB once per track and calls the
// panel's `set_lyrics` action; the panel follows the media position it already
// projects, so no per-line traffic is needed. Nothing is stored unless the
// optional blueprint is installed. The text is kept in one buffer (PSRAM on
// the board: CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL is 0).
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace onx_lyrics {
constexpr std::size_t MAX_TEXT = 12 * 1024;
constexpr std::size_t MAX_LINES = 400;

struct Line { uint32_t ms; uint32_t offset; uint16_t length; };
struct Store {
  std::string entity, title, text;
  std::vector<Line> lines;
  bool hidden{false};      // Tapped away on the media screen (until the next track).
  uint32_t version{0};     // Bumped on every change so the screen rebuilds.
};
inline Store &store() { static Store s; return s; }

// "[mm:ss.xx]" / "[mm:ss]" / "[mm:ss.xxx]"; returns characters consumed or 0.
inline std::size_t parse_tag(const std::string &s, std::size_t i, uint32_t &ms) {
  if (i >= s.size() || s[i] != '[') return 0;
  std::size_t j = i + 1;
  auto digits = [&](uint32_t &v) { std::size_t n = 0; v = 0; while (j < s.size() && s[j] >= '0' && s[j] <= '9' && n < 4) { v = v * 10 + (s[j++] - '0'); ++n; } return n; };
  uint32_t m = 0, sec = 0, frac = 0;
  if (!digits(m) || j >= s.size() || s[j++] != ':' || digits(sec) != 2) return 0;
  std::size_t fd = 0;
  if (j < s.size() && (s[j] == '.' || s[j] == ':')) { ++j; fd = digits(frac); if (!fd) return 0; }
  if (j >= s.size() || s[j] != ']') return 0;
  while (fd < 3) { frac *= 10; ++fd; }
  while (fd > 3) { frac /= 10; --fd; }
  ms = (m * 60 + sec) * 1000 + frac;
  return j + 1 - i;
}

inline void clear() {
  auto &s = store();
  if (s.entity.empty() && s.lines.empty()) return;
  s.entity.clear(); s.title.clear(); s.text.clear(); s.lines.clear(); s.text.shrink_to_fit(); s.lines.shrink_to_fit();
  s.hidden = false; ++s.version;
}

// Keeps only timed lines; plain (unsynced) lyrics clear the store.
inline bool set(const std::string &entity, const std::string &title, const std::string &lrc) {
  clear();
  auto &s = store();
  std::vector<Line> lines;
  std::string text;
  std::size_t pos = 0;
  while (pos < lrc.size() && lines.size() < MAX_LINES) {
    std::size_t end = lrc.find('\n', pos);
    if (end == std::string::npos) end = lrc.size();
    std::string row = lrc.substr(pos, end - pos);
    pos = end + 1;
    if (!row.empty() && row.back() == '\r') row.pop_back();
    std::vector<uint32_t> stamps;
    std::size_t i = 0;
    uint32_t ms = 0;
    while (std::size_t n = parse_tag(row, i, ms)) { stamps.push_back(ms); i += n; }
    if (stamps.empty()) continue;
    std::size_t a = row.find_first_not_of(" \t", i);
    std::string words = a == std::string::npos ? "" : row.substr(a);
    while (!words.empty() && (words.back() == ' ' || words.back() == '\t')) words.pop_back();
    if (words.size() > 400) words.resize(400);
    if (text.size() + words.size() > MAX_TEXT) break;
    const uint32_t off = text.size();
    text += words;
    for (uint32_t t : stamps) if (lines.size() < MAX_LINES) lines.push_back({t, off, uint16_t(words.size())});
  }
  std::stable_sort(lines.begin(), lines.end(), [](const Line &x, const Line &y) { return x.ms < y.ms; });
  if (lines.empty()) return false;
  s.entity = entity; s.title = title; s.text = std::move(text); s.lines = std::move(lines); ++s.version;
  return true;
}

// Lyrics for this player's current track, if any.
inline bool available(const std::string &entity, const std::string &title) {
  const auto &s = store();
  return !s.lines.empty() && s.entity == entity && s.title == title;
}
inline bool shown(const std::string &entity, const std::string &title) { return available(entity, title) && !store().hidden; }
inline void toggle() { store().hidden = !store().hidden; ++store().version; }

// Index of the line sung at `seconds`, or -1 before the first one.
inline int index_at(float seconds) {
  const auto &l = store().lines;
  const uint32_t ms = seconds <= 0 ? 0 : uint32_t(seconds * 1000.0f);
  auto it = std::upper_bound(l.begin(), l.end(), ms, [](uint32_t v, const Line &x) { return v < x.ms; });
  return int(it - l.begin()) - 1;
}
// Text of line `i`; out of range or an instrumental gap gives "".
inline std::string line(int i) {
  const auto &s = store();
  if (i < 0 || i >= int(s.lines.size())) return "";
  return s.text.substr(s.lines[i].offset, s.lines[i].length);
}
inline int count() { return int(store().lines.size()); }
}  // namespace onx_lyrics
