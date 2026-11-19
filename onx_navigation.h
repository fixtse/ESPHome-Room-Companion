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
constexpr int DASHBOARD=-2, NO_TARGET=-1;
inline int pinned_target=NO_TARGET, dock_offset=0, selected_target=NO_TARGET;
inline std::array<int,VISIBLE_ICONS> targets{};
inline int dock_min=0,dock_max=0;
inline bool scroll_dock(int steps) {
  const int next = std::clamp(dock_offset + steps, dock_min, dock_max);
  if (next == dock_offset) return false;
  dock_offset = next;
  return true;
}
inline int selected_rank=-1;
inline bool visible=false;
inline bool pin_right=false;
inline std::array<bool,VISIBLE_ICONS> entry_available{};
inline int dock_count=0;
inline int dock_target_at(int x) {
  if(!visible || !dock_count || x<0 || x>=strip.width)return NO_TARGET;
  return targets[x*dock_count/strip.width];
}
inline std::function<void()> shortcut_action;
// A touched dock icon does a little dance: it hops up, falls back, bounces once
// and shimmies while in the air. One animation computes the whole motion from
// its progress and always ends (or is cancelled) with the icon at rest, so an
// icon can never be left raised or lowered. Only its translation changes (no
// rotation or pivot, which shift how an image is drawn): nothing else moves and
// no extra buffer is needed.
inline void dance_step(void *o,int32_t t) {  // t: 0..1000
  auto *icon=static_cast<lv_obj_t *>(o);
  const float p=t/1000.0f,hop=std::max(8,strip.icon*55/100);
  float y=0;
  if(p<0.22f){const float k=p/0.22f;y=-hop*(1-(1-k)*(1-k));}                 // Up, easing out.
  else if(p<0.55f){const float k=(p-0.22f)/0.33f;y=-hop*(1-k*k);}            // Fall, accelerating.
  else if(p<0.78f){y=-hop*0.28f*std::sin(3.14159f*(p-0.55f)/0.23f);}         // One small bounce.
  const float shimmy=p<0.7f?hop*0.3f*std::sin(6.28318f*2.5f*p)*(1-p/0.7f):0;  // Side to side, dying out.
  lv_obj_set_style_translate_y(icon,int32_t(y),0);
  lv_obj_set_style_translate_x(icon,int32_t(shimmy),0);
}
inline void dance_reset(lv_obj_t *icon) {
  onx_render::style_number(icon, LV_STYLE_TRANSLATE_Y, 0, 0);
  onx_render::style_number(icon, LV_STYLE_TRANSLATE_X, 0, 0);
}
inline void dance_rest(lv_anim_t *a) {dance_reset(static_cast<lv_obj_t *>(lv_anim_get_user_data(a)));}
inline void dance(lv_obj_t *icon) {
  if(icon==nullptr || lv_obj_has_flag(icon,LV_OBJ_FLAG_HIDDEN))return;
  lv_anim_delete(icon,dance_step);
  lv_anim_t a;lv_anim_init(&a);
  lv_anim_set_var(&a,icon);lv_anim_set_user_data(&a,icon);
  lv_anim_set_exec_cb(&a,dance_step);lv_anim_set_values(&a,0,1000);
  lv_anim_set_duration(&a,620);lv_anim_set_path_cb(&a,lv_anim_path_linear);
  lv_anim_set_completed_cb(&a,dance_rest);lv_anim_set_deleted_cb(&a,dance_rest);
  lv_anim_start(&a);
}
// Touching an edge icon re-centres the dock, so the icon slots change targets.
// Remember the touched target and dance whichever slot shows it after the
// next layout (or straight away if the dock does not move).
inline int pending_dance=NO_TARGET;
inline uint32_t pending_dance_at=0;
inline void dance_pending() {
  if(pending_dance==NO_TARGET)return;
  const int target=pending_dance;pending_dance=NO_TARGET;
  if(lv_tick_elaps(pending_dance_at)>1500)return;
  for(int n=0;n<dock_count;++n)if(targets[n]==target){dance(icons[n]);return;}
}
inline void press_at(int x) {
  const int target=dock_target_at(x);
  if(target==NO_TARGET)return;
  pending_dance=target;pending_dance_at=lv_tick_get();
  // Selecting the current page does not lay the dock out again.
  if(target==selected_target || target==DASHBOARD)dance_pending();
}
inline void icon_offset(void *widget,int32_t offset_milli) {
  const auto point=arc_point(offset_milli);
  lv_obj_align(static_cast<lv_obj_t *>(widget),LV_ALIGN_CENTER,point.x-screen_width()/2,point.y-screen_height()/2);
}
inline void hide() {
  if(dock)lv_obj_add_flag(dock,LV_OBJ_FLAG_HIDDEN);
  for(auto *icon:icons)if(icon)lv_anim_delete(icon,icon_offset);
  visible=false;
  dock_offset=0;
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
inline void set_label(const char *text) {if(label)onx_render::label(label,text);}
// Panel-local controls ([Local] Dashboard / [Local] Assist): set by the touch
// panels at boot.
inline std::function<bool(const std::string &)> local_action;     // Handles a press; true if handled.
inline std::function<bool(int io)> local_state;                     // On/off for the icon.
inline std::function<esphome::image::Image *(const std::string &)> local_icon;
template<typename Bind> void render(std::vector<int> entries,int selected,Bind bind) {
  if(selected!=selected_target)dock_offset=0;
  selected_target=selected;
  auto current=std::find(entries.begin(),entries.end(),selected);
  if(current==entries.end()) {hide();return;}
  create();
  int center=static_cast<int>(current-entries.begin());
  if(pinned_target!=NO_TARGET) {
    const auto pin=std::find(entries.begin(),entries.end(),pinned_target);
    if(pin!=entries.end()) {
      if(pin<current)--center;
      entries.erase(pin);
    }
  }
  if(strip.enabled) {
    const bool has_pin=pinned_target!=NO_TARGET;
    const int total=int(entries.size());
    const int scrolling=std::min(total,VISIBLE_ICONS-int(has_pin));
    dock_count=scrolling+int(has_pin);
    const int max_start=total-scrolling;
    const int home=std::clamp(center-scrolling/2,0,max_start);
    dock_min=-home;dock_max=max_start-home;
    dock_offset=std::clamp(dock_offset,dock_min,dock_max);
    for(int n=0;n<VISIBLE_ICONS;++n) {
      auto *icon=icons[n];
      if(n>=dock_count){lv_obj_add_flag(icon,LV_OBJ_FLAG_HIDDEN);continue;}
      const bool pinned=has_pin && n==(pin_right?dock_count-1:0);
      const int index=home+dock_offset+n-int(has_pin&&!pin_right);
      targets[n]=pinned?pinned_target:entries[index];
      const bool active=targets[n]==selected;
      lv_obj_remove_flag(icon,LV_OBJ_FLAG_HIDDEN);
      // Any icon that is not mid-dance sits exactly at its place.
      if(lv_anim_get(icon,dance_step)==nullptr)dance_reset(icon);
      const int x=strip.width*(2*n+1)/(2*dock_count)-strip.width/2;
      lv_obj_align(icon,LV_ALIGN_CENTER,x,strip.center_y-strip.height/2);
      bind(icon,targets[n],active);
      onx_render::style_opa(icon,active||pinned?LV_OPA_COVER:170,0);
      if(active)lv_obj_align(marker,LV_ALIGN_CENTER,x,strip.center_y-strip.height/2);
    }
    const bool selected_visible=std::find(targets.begin(),targets.begin()+dock_count,selected)!=targets.begin()+dock_count;
    if(selected_visible)lv_obj_remove_flag(marker,LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(marker,LV_OBJ_FLAG_HIDDEN);
    onx_render::style_bg_color(marker,lv_color_hex(onx_display_settings::accent_soft()),0);
    lv_obj_remove_flag(dock,LV_OBJ_FLAG_HIDDEN);visible=true;
    dance_pending();
    return;
  }
  center=std::clamp(center,0,std::max(0,int(entries.size())-1));
  dock_min=-center;dock_max=std::max(0,int(entries.size())-1)-center;
  dock_offset=std::clamp(dock_offset,dock_min,dock_max);
  const int rank=center+dock_offset;
  const bool changed=!visible || rank!=selected_rank;
  const int shift=visible?std::clamp(rank-selected_rank,-1,1):0;
  for(int n=0;n<VISIBLE_ICONS;++n) {
    const int offset=n-2,index=rank+offset;
    const bool pinned=pinned_target!=NO_TARGET && n==(pin_right?4:0);
    targets[n]=pinned?pinned_target:index>=0 && index<int(entries.size())?entries[index]:NO_TARGET;
    entry_available[n]=targets[n]!=NO_TARGET;
    if(!entry_available[n]) {
      lv_anim_delete(icons[n],icon_offset);
      lv_obj_add_flag(icons[n],LV_OBJ_FLAG_HIDDEN);continue;
    }
    const bool was_hidden=lv_obj_has_flag(icons[n],LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(icons[n],LV_OBJ_FLAG_HIDDEN);
    bind(icons[n],targets[n],targets[n]==selected);
    onx_render::style_opa(icons[n],(pinned || targets[n]==selected)?LV_OPA_COVER:std::abs(offset)==1?170:110,0);
    if(changed || was_hidden || pinned) {
      lv_anim_delete(icons[n],icon_offset);
      icon_offset(icons[n],(offset+(pinned?0:shift))*1000);
      if(shift && !pinned) {
        lv_anim_t animation;lv_anim_init(&animation);
        lv_anim_set_var(&animation,icons[n]);lv_anim_set_exec_cb(&animation,icon_offset);
        lv_anim_set_values(&animation,(offset+shift)*1000,offset*1000);
        lv_anim_set_duration(&animation,strip.enabled?80:180);lv_anim_set_path_cb(&animation,lv_anim_path_ease_out);
        lv_anim_start(&animation);
      }
    }
  }
  selected_rank=rank;
  if(strip.enabled) {
    if(dock_offset==0 && pinned_target!=selected)lv_obj_remove_flag(marker,LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(marker,LV_OBJ_FLAG_HIDDEN);
  }
  // The selection marker is a tint of the configurable accent colour.
  onx_render::style_bg_color(marker,lv_color_hex(onx_display_settings::accent_soft()),0);
  lv_obj_clear_flag(dock,LV_OBJ_FLAG_HIDDEN);visible=true;
}
}  // namespace onx_navigation
