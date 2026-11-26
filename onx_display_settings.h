#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
namespace onx_display_settings {
struct Settings {
  uint32_t magic{0x4F4E5844}, media_color{0x62B0FF};
  uint8_t standby_scope{0}; // 0 main carousel only, 1 all views, 2 disabled
};
inline Settings current;
struct BackgroundSettings { uint32_t magic{0x4F4E5842}, color{0x101418}; };
inline BackgroundSettings background_settings;
struct Orientation {uint32_t magic{0x4F4E584F};uint8_t turns{0};};
inline Orientation orientation;
inline bool parse_orientation(const std::string &text,uint8_t &turns) {
  if(text=="0")turns=0;else if(text=="90")turns=1;else if(text=="180")turns=2;else if(text=="270")turns=3;else return false;
  return true;
}
struct BehaviorSettings {uint32_t magic{0x4F4E584E};uint8_t cover_timers{1},clock_seconds{0};};
inline BehaviorSettings behavior;
// Progress cue while holding the button on a detail page.
struct HoldCue {uint32_t magic{0x4F4E5848};uint8_t show{1};};
inline HoldCue hold_cue;
// Mute registered playing media players while Assist is active.
struct AssistMuteMedia {uint32_t magic{0x4F4E5841};uint8_t enabled{0};};
inline AssistMuteMedia assist_mute_media;
struct IdleSettings {uint32_t magic{0x4F4E5849};uint8_t media{0};};
inline IdleSettings idle;
struct DateTimeSettings {
  uint32_t magic{0x4F4E5854};
  uint8_t date_format{0}, hour_format{0}, show_ampm{1}; // Numeric/name month variants; 24 / 12 hour
};
inline DateTimeSettings datetime;
inline std::string date_format_text(uint8_t value) {
  static const char *const formats[]={"dd-mm","mm-dd","yyyy-mm-dd","dd-mmm","mmm-dd","yyyy-mmm-dd"};
  return formats[value<6?value:0];
}
inline bool parse_date_format(const std::string &text, uint8_t &value) {
  if (text == "dd-mm") value = 0;
  else if (text == "mm-dd") value = 1;
  else if (text == "yyyy-mm-dd") value = 2;
  else if (text == "dd-mmm") value = 3;
  else if (text == "mmm-dd") value = 4;
  else if (text == "yyyy-mmm-dd") value = 5;
  else return false;
  return true;
}
inline bool parse_hour_format(const std::string &text, uint8_t &value) {
  if (text == "24") value = 0;
  else if (text == "12") value = 1;
  else return false;
  return true;
}
inline const char *date_pattern() {
  static const char *const patterns[]={"%d-%m","%m-%d","%Y-%m-%d","%d %b","%b %d","%Y-%b-%d"};
  return patterns[datetime.date_format<6?datetime.date_format:0];
}
inline const char *time_pattern() { return datetime.hour_format == 1 ? (datetime.show_ampm ? "%I:%M %p" : "%I:%M") : "%H:%M"; }
inline std::string format_iso_date(const std::string &iso) {
  if (iso.size() < 10 || iso[4] != '-' || iso[7] != '-') return iso;
  for (size_t i : {size_t(0),size_t(1),size_t(2),size_t(3),size_t(5),size_t(6),size_t(8),size_t(9)})
    if (iso[i] < '0' || iso[i] > '9') return iso;
  const unsigned month=(iso[5]-'0')*10+(iso[6]-'0');
  if(month<1 || month>12) return iso;
  static const char *const months[]={"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
  const std::string name=months[month-1],day=iso.substr(8,2),year=iso.substr(0,4);
  switch(datetime.date_format) {
    case 1:return iso.substr(5,5);
    case 2:return iso.substr(0,10);
    case 3:return day+" "+name;
    case 4:return name+" "+day;
    case 5:return year+"-"+name+"-"+day;
    default:return day+"-"+iso.substr(5,2);
  }
}
inline bool parse_color(const std::string &text, uint32_t &color) {
  if (text.size() != 7 || text[0] != '#') return false;
  uint32_t value = 0;
  for (size_t i = 1; i < 7; ++i) {
    const char c = text[i];
    int digit = c >= '0' && c <= '9' ? c-'0' : c >= 'a' && c <= 'f' ? c-'a'+10 : c >= 'A' && c <= 'F' ? c-'A'+10 : -1;
    if (digit < 0) return false;
    value = (value << 4) | digit;
  }
  color = value; return true;
}
// The configurable accent (formerly "ring color") tints every highlighted
// element: progress, the selected navigator item, filled buttons and graphs.
constexpr uint32_t DEFAULT_ACCENT=0x62B0FF;
inline uint32_t accent() { return current.media_color & 0xFFFFFF; }
// Linear blend: weight 0..100 of `a` over `b`, per channel.
inline uint32_t mix(uint32_t a, uint32_t b, unsigned weight) {
  if (weight > 100) weight = 100;
  uint32_t out = 0;
  for (int shift = 0; shift <= 16; shift += 8) {
    const unsigned ca = (a >> shift) & 0xFF, cb = (b >> shift) & 0xFF;
    out |= ((ca * weight + cb * (100 - weight) + 50) / 100) << shift;
  }
  return out;
}
inline uint32_t background() { return background_settings.color & 0xFFFFFF; }
inline bool light_color(uint32_t c) {return ((c>>16)*299+((c>>8)&255)*587+(c&255)*114)>150000;
}
inline bool light_background() {return light_color(background());}
inline uint32_t foreground() { return light_background()?0x161B22:0xFFFFFF; }
// Light theme: white cards on a tinted background. 0xFEFEFE draws as white
// (RGB565) but stays distinct from widgets that are deliberately pure white,
// so the palette mapper never confuses them. A near-white custom background
// falls back to slightly darker cards so they still stand out.
inline uint32_t surface() {
  if(!light_background())return mix(0xFFFFFF,background(),8);
  return ((background()>>16)*299+((background()>>8)&255)*587+(background()&255)*114)>247000?mix(0x000000,background(),5):0xFEFEFE;
}
inline uint32_t control_surface() { return mix(light_background()?0x000000:0xFFFFFF,background(),light_background()?7:13); }
inline uint32_t track_surface() { return mix(light_background()?0x000000:0xFFFFFF,background(),light_background()?14:23); }
inline uint32_t muted_text() { return mix(foreground(),background(),68); }
inline uint32_t accent_strong() { return mix(accent(), 0x000000, 80); }  // Filled buttons (white symbol).
inline uint32_t accent_soft() { return mix(accent(), background(), 28); }    // Tinted selection surfaces.
inline std::string color_text(uint32_t color) {
  char text[8]; snprintf(text, sizeof(text), "#%06X", static_cast<unsigned>(color & 0xFFFFFF)); return text;
}
inline bool parse_scope(const std::string &text, uint8_t &scope) {
  if (text == "main") scope = 0;
  else if (text == "all") scope = 1;
  else if (text == "never") scope = 2;
  else return false;
  return true;
}
inline std::string scope_text(uint8_t scope) { return scope == 1 ? "all" : scope == 2 ? "never" : "main"; }
inline bool standby_allowed(uint8_t scope, bool detail_active) { return scope != 2 && (scope == 1 || !detail_active); }
}
