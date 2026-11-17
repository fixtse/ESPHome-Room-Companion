#pragma once
#include <cstring>
#include "lvgl.h"
#include "onx_emoji.h"
#include "onx_display_settings.h"
#include <array>
#include <algorithm>
#include <new>

// Avoid reallocating label buffers or invalidating image caches for unchanged UI.
namespace onx_render {
// LVGL refreshes/invalidate styles even when the local value is unchanged.
// Preserve inherited/state-specific styles: only skip an identical LOCAL value.
inline void style_number(lv_obj_t *widget, lv_style_prop_t prop, int32_t number,
                         lv_style_selector_t selector) {
  lv_style_value_t old;
  if (lv_obj_get_local_style_prop(widget, prop, &old, selector) == LV_STYLE_RES_FOUND && old.num == number) return;
  lv_style_value_t value{}; value.num = number;
  lv_obj_set_local_style_prop(widget, prop, value, selector);
}
// Dark mode (panel "Dark mode" switch) maps the base palette to near-black.
// Applying it here, where widgets set their colours, keeps periodic refreshes
// from flashing the light colour before the palette sweep darkens it again.
struct PalettePair { uint32_t normal, dark; };
inline constexpr PalettePair DARK_SURFACES[] = {
  {0x1A1A1A, 0x010101}, {0x272727, 0x121213}, {0x232323, 0x0E0E0F},
  {0x2D2D2D, 0x151516}, {0x3D3D3D, 0x1F1F21}, {0x101418, 0x020304},
  {0x2A2F35, 0x111417}, {0x24323A, 0x0D161B}, {0x414850, 0x22272C},
  {0x5A5A5A, 0x333335}, {0x404040, 0x252527},
};
inline constexpr PalettePair DARK_TEXTS[] = {
  {0xFFFFFF, 0xE4E4E6}, {0xD2D2D2, 0x9C9C9E}, {0xD8D8D8, 0xA2A2A4},
  {0xB8B8B8, 0x858587}, {0xAAAAAA, 0x7A7A7C},
};
inline bool panel_palette = false;
struct PanelColors { uint32_t bg, surface, control, track, text, muted; };
inline std::array<PanelColors,8> panel_history{};
inline size_t panel_history_count=0;
inline const PanelColors *panel_source=nullptr;
inline PanelColors panel_colors() {
  using namespace onx_display_settings;
  return {background(),surface(),control_surface(),track_surface(),foreground(),muted_text()};
}
inline void remember_panel_colors() {
  const auto c=panel_colors();
  if(panel_history_count && panel_history[(panel_history_count-1)%panel_history.size()].bg==c.bg)return;
  panel_history[panel_history_count++%panel_history.size()]=c;
}
inline uint32_t panel_color(uint32_t c,bool text) {
  const auto now=panel_colors();
  if(text?(c==now.text||c==now.muted):(c==now.bg||c==now.surface||c==now.control||c==now.track))return c;
  if(panel_source){const auto &old=*panel_source;
    if(text){if(c==old.text)return now.text;if(c==old.muted)return now.muted;}
    else {if(c==old.bg)return now.bg;if(c==old.surface)return now.surface;if(c==old.control)return now.control;if(c==old.track)return now.track;}
  }
  for(size_t i=0;i<std::min(panel_history_count,panel_history.size());++i){
    const auto &old=panel_history[i];
    if(text){if(c==old.text)return now.text;if(c==old.muted)return now.muted;}
    else {if(c==old.bg)return now.bg;if(c==old.surface)return now.surface;if(c==old.control)return now.control;if(c==old.track)return now.track;}
  }
  if(text){
    if(c==0xFFFFFF||c==0xE4E4E6||c==0)return now.text;
    for(const auto &p:DARK_TEXTS)if(c==p.normal||c==p.dark)return now.muted;
    if(c==0x9C9C9E)return now.muted;
  }else{
    if(c==0||c==0x1A1A1A||c==0x010101||c==0x101418||c==0x020304)return now.bg;
    for(size_t i=0;i<std::size(DARK_SURFACES);++i){const auto &p=DARK_SURFACES[i];if(c==p.normal||c==p.dark)return i<=3?now.surface:i<=7?now.control:now.track;}
  }
  return c;
}
inline uint32_t panel_property_color(lv_obj_t *obj,lv_style_prop_t prop,uint32_t color) {
  const bool text=prop==LV_STYLE_TEXT_COLOR||prop==LV_STYLE_IMAGE_RECOLOR;
  if(text && obj){
    auto *parent=lv_obj_get_parent(obj);lv_style_value_t value;
    if(parent && lv_obj_get_local_style_prop(parent,LV_STYLE_BG_COLOR,&value,LV_PART_MAIN)==LV_STYLE_RES_FOUND){
      const auto bg=lv_color_to_u32(value.color)&0xFFFFFF;
      if(bg==onx_display_settings::accent()||bg==onx_display_settings::accent_strong())
        return onx_display_settings::light_color(bg)?0x161B22:0xFFFFFF;
    }
  }
  return panel_color(color,text);
}
inline void apply_panel_palette(lv_obj_t *obj) {
  if(!panel_palette||!obj)return;
  for(auto part:{LV_PART_MAIN,LV_PART_INDICATOR,LV_PART_KNOB,LV_PART_ITEMS})
    for(auto prop:{LV_STYLE_BG_COLOR,LV_STYLE_BG_GRAD_COLOR,LV_STYLE_BORDER_COLOR,LV_STYLE_ARC_COLOR,LV_STYLE_LINE_COLOR,LV_STYLE_OUTLINE_COLOR,LV_STYLE_TEXT_COLOR,LV_STYLE_IMAGE_RECOLOR}){
      lv_style_value_t value;
      if(lv_obj_get_local_style_prop(obj,prop,&value,part)!=LV_STYLE_RES_FOUND){
        if(prop!=LV_STYLE_TEXT_COLOR||part!=LV_PART_MAIN||!lv_obj_check_type(obj,&lv_label_class))continue;
        value.color=lv_obj_get_style_text_color(obj,part);
      }
      const auto from=lv_color_to_u32(value.color)&0xFFFFFF;
      const auto to=panel_property_color(obj,prop,from);
      if(from!=to){value.color=lv_color_hex(to);lv_obj_set_local_style_prop(obj,prop,value,part);}
    }
  for(uint32_t i=0;i<lv_obj_get_child_count(obj);++i)apply_panel_palette(lv_obj_get_child(obj,i));
}
inline void register_panel_screen(lv_obj_t *screen) {
  auto *colors=new(std::nothrow) PanelColors(panel_colors());
  if(!colors)return;
  lv_obj_add_event_cb(screen,[](lv_event_t *event){
    auto *colors=static_cast<PanelColors *>(lv_event_get_user_data(event));
    if(lv_event_get_code(event)==LV_EVENT_DELETE){delete colors;return;}
    if(lv_event_get_code(event)!=LV_EVENT_SCREEN_LOAD_START)return;
    auto *screen=static_cast<lv_obj_t *>(lv_event_get_target(event));
    panel_source=colors;apply_panel_palette(screen);panel_source=nullptr;
    lv_obj_set_style_bg_color(screen,lv_color_hex(onx_display_settings::background()),0);
    lv_obj_set_style_text_color(screen,lv_color_hex(onx_display_settings::foreground()),0);
    *colors=panel_colors();
  },LV_EVENT_ALL,colors);
}
inline bool dark_palette = false;
inline lv_color_t themed(lv_style_prop_t prop, lv_color_t color,lv_obj_t *obj=nullptr) {
  if(panel_palette)return lv_color_hex(panel_property_color(obj,prop,lv_color_to_u32(color)&0xFFFFFF));
  if (!dark_palette) return color;
  const uint32_t c = lv_color_to_u32(color) & 0xFFFFFF;
  const bool text = prop == LV_STYLE_TEXT_COLOR || prop == LV_STYLE_IMAGE_RECOLOR;
  if (text) { for (const auto &p : DARK_TEXTS) if (c == p.normal) return lv_color_hex(p.dark); }
  else { for (const auto &p : DARK_SURFACES) if (c == p.normal) return lv_color_hex(p.dark); }
  return color;
}
inline void style_color(lv_obj_t *widget, lv_style_prop_t prop, lv_color_t color,
                        lv_style_selector_t selector) {
  color = themed(prop, color,widget);
  lv_style_value_t old;
  if (lv_obj_get_local_style_prop(widget, prop, &old, selector) == LV_STYLE_RES_FOUND && lv_color_eq(old.color, color)) return;
  lv_style_value_t value{}; value.color = color;
  lv_obj_set_local_style_prop(widget, prop, value, selector);
}
inline void style_text_font(lv_obj_t *widget, const lv_font_t *font, lv_style_selector_t selector) {
  lv_style_value_t old;
  if (lv_obj_get_local_style_prop(widget, LV_STYLE_TEXT_FONT, &old, selector) == LV_STYLE_RES_FOUND) {
    if (old.ptr == font) return;
    // label() installs a colour-emoji wrapper for this same underlying font.
    for (const auto &entry : onx_emoji::fonts)
      if (old.ptr == entry.color && font == entry.original) return;
  }
  lv_obj_set_style_text_font(widget, font, selector);
}
inline void style_bg_color(lv_obj_t *obj, lv_color_t value, lv_style_selector_t selector) {
  style_color(obj, LV_STYLE_BG_COLOR, value, selector);
}
inline void style_border_color(lv_obj_t *obj, lv_color_t value, lv_style_selector_t selector) {
  style_color(obj, LV_STYLE_BORDER_COLOR, value, selector);
}
inline void style_text_color(lv_obj_t *obj, lv_color_t value, lv_style_selector_t selector) {
  style_color(obj, LV_STYLE_TEXT_COLOR, value, selector);
}
inline void style_arc_color(lv_obj_t *obj, lv_color_t value, lv_style_selector_t selector) {
  style_color(obj, LV_STYLE_ARC_COLOR, value, selector);
}
inline void style_image_recolor(lv_obj_t *obj, lv_color_t value, lv_style_selector_t selector) {
  style_color(obj, LV_STYLE_IMAGE_RECOLOR, value, selector);
}
inline void style_opa(lv_obj_t *obj, int32_t value, lv_style_selector_t selector) {
  style_number(obj, LV_STYLE_OPA, value, selector);
}
inline void style_bg_opa(lv_obj_t *obj, int32_t value, lv_style_selector_t selector) {
  style_number(obj, LV_STYLE_BG_OPA, value, selector);
}
inline void style_text_opa(lv_obj_t *obj, int32_t value, lv_style_selector_t selector) {
  style_number(obj, LV_STYLE_TEXT_OPA, value, selector);
}
inline void style_arc_opa(lv_obj_t *obj, int32_t value, lv_style_selector_t selector) {
  style_number(obj, LV_STYLE_ARC_OPA, value, selector);
}
inline void style_image_recolor_opa(lv_obj_t *obj, int32_t value, lv_style_selector_t selector) {
  style_number(obj, LV_STYLE_IMAGE_RECOLOR_OPA, value, selector);
}
inline void style_border_width(lv_obj_t *obj, int32_t value, lv_style_selector_t selector) {
  style_number(obj, LV_STYLE_BORDER_WIDTH, value, selector);
}
inline void style_radius(lv_obj_t *obj, int32_t value, lv_style_selector_t selector) {
  style_number(obj, LV_STYLE_RADIUS, value, selector);
}
inline void style_text_align(lv_obj_t *obj, int32_t value, lv_style_selector_t selector) {
  style_number(obj, LV_STYLE_TEXT_ALIGN, value, selector);
}

inline void align(lv_obj_t *widget, lv_align_t alignment, int32_t x, int32_t y) {
  if (lv_obj_get_style_align(widget, LV_PART_MAIN) == alignment &&
      lv_obj_get_style_x(widget, LV_PART_MAIN) == x && lv_obj_get_style_y(widget, LV_PART_MAIN) == y) return;
  lv_obj_align(widget, alignment, x, y);
}

inline void label(lv_obj_t *widget, const char *text) {
  onx_emoji::enable(widget);
  const auto encoded = onx_emoji::encode(text);
  if (std::strcmp(lv_label_get_text(widget), encoded.c_str()) != 0) lv_label_set_text(widget, encoded.c_str());
}
inline void image(lv_obj_t *widget, const void *source) {
  if (lv_img_get_src(widget) != source) lv_img_set_src(widget, source);
}
// Share the Settings list appearance with other rotary menus.
inline void settings_roller(lv_obj_t *roller,const lv_font_t *normal_font,const lv_font_t *selected_font,
                            bool reserve_arrow=true) {
  lv_obj_set_style_bg_opa(roller,LV_OPA_TRANSP,LV_PART_MAIN);
  lv_obj_set_style_border_width(roller,0,LV_PART_MAIN);
  lv_obj_set_style_text_color(roller,lv_color_hex(0x4D4D4D),LV_PART_MAIN);
  lv_obj_set_style_text_font(roller,normal_font,LV_PART_MAIN);
  lv_obj_set_style_text_line_space(roller,15,LV_PART_MAIN);
  lv_obj_set_style_bg_color(roller,lv_color_hex(0x3D3D3D),LV_PART_SELECTED);
  lv_obj_set_style_bg_opa(roller,LV_OPA_COVER,LV_PART_SELECTED);
  lv_obj_set_style_radius(roller,15,LV_PART_SELECTED);
  lv_obj_set_style_text_color(roller,lv_color_white(),LV_PART_SELECTED);
  lv_obj_set_style_text_font(roller,selected_font,LV_PART_SELECTED);
  lv_obj_set_style_pad_left(roller,20,LV_PART_SELECTED);
  lv_obj_set_style_pad_right(roller,reserve_arrow?40:20,LV_PART_SELECTED);
}

}
