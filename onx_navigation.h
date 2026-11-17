#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <string>
#include <vector>
#include "lvgl.h"
#include "onx_render.h"
#include "onx_display_settings.h"
#include "esphome/components/image/image.h"

namespace onx_navigation {
constexpr int VISIBLE_ICONS=5;
struct Point {int x,y;};
// Base layout (strip disabled): icons follow a curve on a 240x240 screen.
// Panels: icons sit in a straight row centred in a bottom strip (configure_strip).
struct Strip {
  bool enabled{false};
  int width{240},height{240},center_y{0},spacing{48},marker{24};
  int icon{18};             // Icon size in layout units (default 18).
  int label_y{0};           // Name of the selected control, below the strip.
  const lv_font_t *font{nullptr};
};
inline Strip strip;
inline lv_obj_t *label=nullptr;
inline void configure_strip(int width,int height,int center_y,int spacing,int marker,int icon,int label_y,const lv_font_t *font) {
  strip={true,width,height,center_y,spacing,marker,icon,label_y,font};
}
// Carousel icon size in layout units (images are scaled by onx_geom).
inline int icon_size() {return strip.enabled?strip.icon:18;}
inline Point arc_point(int offset_milli) {
  if(strip.enabled)
    return {strip.width/2+static_cast<int>(std::lround(offset_milli*strip.spacing/1000.0f)),strip.center_y};
  const float angle=offset_milli*(17.0f*3.14159265f/180000.0f);
  return {120+static_cast<int>(std::lround(102*std::sin(angle))),
          120+static_cast<int>(std::lround(102*std::cos(angle)))};
}
inline int screen_width() {return strip.enabled?strip.width:240;}
inline int screen_height() {return strip.enabled?strip.height:240;}
inline lv_obj_t *dock=nullptr, *marker=nullptr;
inline std::array<lv_obj_t *,VISIBLE_ICONS> icons{};
inline int selected_rank=-1;
inline bool visible=false;
inline lv_obj_t *shortcut=nullptr, *shortcut_image=nullptr;
inline bool shortcut_enabled=false, shortcut_right=false;
inline std::array<bool,VISIBLE_ICONS> entry_available{};
inline std::function<void()> shortcut_action;
inline bool shortcut_hit(int x,int y) {
  if(!shortcut || !lv_obj_is_visible(shortcut))return false;
  lv_area_t a;lv_obj_get_coords(shortcut,&a);
  return x>=a.x1 && x<=a.x2 && y>=a.y1 && y<=a.y2;
}
inline void icon_offset(void *widget,int32_t offset_milli) {
  const auto point=arc_point(offset_milli);
  lv_obj_align(static_cast<lv_obj_t *>(widget),LV_ALIGN_CENTER,point.x-screen_width()/2,point.y-screen_height()/2);
}
inline void hide() {
  if(dock)lv_obj_add_flag(dock,LV_OBJ_FLAG_HIDDEN);
  for(auto *icon:icons)if(icon)lv_anim_delete(icon,icon_offset);
  visible=false;
}
inline void create() {
  if(dock)return;
  // A transparent top layer keeps the navigator independent of sliding pages.
  dock=lv_obj_create(lv_layer_top());
  lv_obj_set_size(dock,screen_width(),screen_height());lv_obj_set_pos(dock,0,0);
  lv_obj_set_style_pad_all(dock,0,0);lv_obj_set_style_border_width(dock,0,0);
  lv_obj_set_style_radius(dock,0,0);lv_obj_set_style_bg_opa(dock,LV_OPA_TRANSP,0);
  lv_obj_set_style_shadow_width(dock,0,0);
  lv_obj_clear_flag(dock,LV_OBJ_FLAG_SCROLLABLE);lv_obj_clear_flag(dock,LV_OBJ_FLAG_CLICKABLE);
  marker=lv_obj_create(dock);
  const int marker_size=strip.enabled?strip.marker:24;
  lv_obj_set_size(marker,marker_size,marker_size);
  lv_obj_align(marker,LV_ALIGN_CENTER,0,strip.enabled?strip.center_y-strip.height/2:102);
  lv_obj_set_style_pad_all(marker,0,0);lv_obj_set_style_border_width(marker,0,0);
  lv_obj_set_style_radius(marker,marker_size/2,0);
  lv_obj_set_style_bg_color(marker,lv_color_hex(onx_display_settings::accent_soft()),0);
  lv_obj_set_style_bg_opa(marker,LV_OPA_COVER,0);
  lv_obj_clear_flag(marker,LV_OBJ_FLAG_SCROLLABLE);lv_obj_clear_flag(marker,LV_OBJ_FLAG_CLICKABLE);
  for(auto &icon:icons) {icon=lv_img_create(dock);lv_obj_clear_flag(icon,LV_OBJ_FLAG_CLICKABLE);}
  if(strip.enabled && strip.font!=nullptr && strip.label_y>=0) {
    label=lv_label_create(dock);
    lv_obj_set_width(label,strip.width-24);
    lv_label_set_long_mode(label,LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_set_style_text_font(label,strip.font,0);
    lv_obj_set_style_text_color(label,lv_color_hex(0xD2D2D2),0);
    lv_obj_align(label,LV_ALIGN_TOP_MID,0,strip.label_y-lv_font_get_line_height(strip.font)/2);
    lv_label_set_text(label,"");
  }
}
// Reserve one edge position for the dashboard, independently of carousel slots.
inline void update_shortcut(const void *source,bool enabled,bool right) {
  shortcut_enabled=enabled;shortcut_right=right;
  if(dock && visible)for(int i=0;i<VISIBLE_ICONS;++i) {
    const bool hidden=!entry_available[i] || (enabled && i==(right?4:0));
    if(hidden)lv_obj_add_flag(icons[i],LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(icons[i],LV_OBJ_FLAG_HIDDEN);
  }
  if(!dock || !visible || !enabled) {
    if(shortcut)lv_obj_add_flag(shortcut,LV_OBJ_FLAG_HIDDEN);
    return;
  }
  if(!shortcut) {
    shortcut=lv_obj_create(dock);
    lv_obj_set_style_bg_opa(shortcut,LV_OPA_TRANSP,0);
    lv_obj_set_style_border_width(shortcut,0,0);
    lv_obj_set_style_pad_all(shortcut,0,0);
    lv_obj_remove_flag(shortcut,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(shortcut,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(shortcut,[](lv_event_t *) {if(shortcut_action)shortcut_action();},LV_EVENT_CLICKED,nullptr);
    shortcut_image=lv_image_create(shortcut);
    lv_obj_remove_flag(shortcut_image,LV_OBJ_FLAG_CLICKABLE);
    lv_image_set_inner_align(shortcut_image,LV_IMAGE_ALIGN_CONTAIN);
  }
  const auto point=arc_point(right?2000:-2000);
  lv_obj_set_size(shortcut,strip.marker,strip.marker);
  lv_obj_set_pos(shortcut,point.x-strip.marker/2,point.y-strip.marker/2);
  lv_obj_set_size(shortcut_image,onx_geom::S(icon_size()),onx_geom::S(icon_size()));
  lv_image_set_src(shortcut_image,source);lv_obj_center(shortcut_image);
  lv_obj_set_style_image_recolor(shortcut_image,lv_color_hex(onx_display_settings::foreground()),0);
  lv_obj_set_style_image_recolor_opa(shortcut_image,LV_OPA_COVER,0);
  lv_obj_remove_flag(shortcut,LV_OBJ_FLAG_HIDDEN);
  const int reserved=right?4:0;
  lv_anim_delete(icons[reserved],icon_offset);
  lv_obj_add_flag(icons[reserved],LV_OBJ_FLAG_HIDDEN);
}
inline void set_label(const char *text) {if(label)onx_render::label(label,text);}
// Panel-local controls ([Local] Dashboard / [Local] Assist): set by the touch
// panels at boot.
inline std::function<bool(const std::string &)> local_action;     // Handles a press; true if handled.
inline std::function<bool(int io)> local_state;                     // On/off for the icon.
inline std::function<esphome::image::Image *(const std::string &)> local_icon;
template<typename Bind> void render(const std::vector<int> &entries,int selected,Bind bind) {
  auto current=std::find(entries.begin(),entries.end(),selected);
  if(current==entries.end()) {hide();return;}
  create();
  const int rank=static_cast<int>(current-entries.begin());
  const bool changed=!visible || rank!=selected_rank;
  const int shift=visible?std::clamp(rank-selected_rank,-1,1):0;
  for(int n=0;n<VISIBLE_ICONS;++n) {
    const int offset=n-2,index=rank+offset;
    entry_available[n]=index>=0 && index<static_cast<int>(entries.size());
    if((shortcut_enabled && n==(shortcut_right?4:0)) || index<0 || index>=static_cast<int>(entries.size())) {
      lv_anim_delete(icons[n],icon_offset);
      lv_obj_add_flag(icons[n],LV_OBJ_FLAG_HIDDEN);continue;
    }
    lv_obj_clear_flag(icons[n],LV_OBJ_FLAG_HIDDEN);
    bind(icons[n],entries[index],offset==0);
    onx_render::style_opa(icons[n],offset==0?LV_OPA_COVER:std::abs(offset)==1?170:110,0);
    if(changed) {
      lv_anim_delete(icons[n],icon_offset);
      icon_offset(icons[n],(offset+shift)*1000);
      if(shift) {
        lv_anim_t animation;lv_anim_init(&animation);
        lv_anim_set_var(&animation,icons[n]);lv_anim_set_exec_cb(&animation,icon_offset);
        lv_anim_set_values(&animation,(offset+shift)*1000,offset*1000);
        lv_anim_set_duration(&animation,strip.enabled?80:180);lv_anim_set_path_cb(&animation,lv_anim_path_ease_out);
        lv_anim_start(&animation);
      }
    }
  }
  selected_rank=rank;
  // The selection marker is a tint of the configurable accent colour.
  onx_render::style_bg_color(marker,lv_color_hex(onx_display_settings::accent_soft()),0);
  lv_obj_clear_flag(dock,LV_OBJ_FLAG_HIDDEN);visible=true;
}
}  // namespace onx_navigation
