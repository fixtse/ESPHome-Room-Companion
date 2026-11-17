#pragma once
#include "onx_panel_layout.h"
#include <functional>
#include <cstdio>
#include "onx_entity_config.h"
#include "onx_render.h"
#include "onx_navigation.h"
#include "onx_panel_ui.h"
#include "onx_panel_detail.h"

namespace onx_panel_pages {
using onx_panel_layout::Page;
using onx_panel_layout::Kind;
inline onx_memory::StateArray<Page,20> pages;
inline std::array<esphome::ESPPreferenceObject,20> preferences;
inline int configured(const std::string &entity) {
  if(entity.empty())return -1;
  for(int i=0;i<20;++i)if(custom_logic::cached_device_entity_id(i)==entity)return i;
  return -1;
}
inline Page page(const std::string &entity) {
  const int slot=configured(entity);
  Page empty;snprintf(empty.primary,sizeof(empty.primary),"%s",entity.c_str());
  if(slot<0 || !esphome::global_preferences)return empty;
  if(entity!=pages[slot].primary) {
    preferences[slot]=esphome::global_preferences->make_preference<Page>(esphome::fnv1_hash("rc.page.v1."+entity));
    Page saved{};
    if(preferences[slot].load(&saved)) {
      saved.primary[128]=0;for(auto &m:saved.members)m[128]=0;
      if(entity==saved.primary && onx_panel_layout::valid(saved))empty=saved;
    }
    pages[slot]=empty;
  }
  return pages[slot];
}
inline std::string rpc(const std::string &op,const std::string &data) {
  // Native (portrait) size and the orientation in use since boot, so the
  // configuration preview matches either model and orientation.
  if(op=="panel_screen")return onx_entity_config::build_json([](JsonObject root) {
    root["width"]=onx_panel_ui::native_width;root["height"]=onx_panel_ui::native_height;
    root["orientation"]=onx_panel_ui::turns*90;
  });
  if(op!="panel_pages" && op!="panel_page_save")return "";
  if(op=="panel_page_save") {
    JsonDocument doc;
    if(deserializeJson(doc,data)!=DeserializationError::Ok)return "{\"error\":\"Invalid page configuration\"}";
    Page next;const std::string entity=doc["entity"] | "";
    const int slot=configured(entity);
    snprintf(next.primary,sizeof(next.primary),"%s",entity.c_str());
    if(slot<0 || !onx_panel_layout::parse(doc["layout"] | "",next.kind))return "{\"error\":\"Choose a configured page and layout\"}";
    auto members=doc["members"].as<JsonArrayConst>();
    const int count=next.kind==Kind::TWO_CONTROLS?1:next.kind==Kind::CONTROL_SENSORS?3:0;
    if(members.size()!=static_cast<size_t>(count))return "{\"error\":\"Complete every position in this layout\"}";
    for(int i=0;i<count;++i) {
      const std::string member=members[i] | "";
      if(configured(member)<0 || member.size()>128)return "{\"error\":\"Page items must be configured entities\"}";
      snprintf(next.members[i],sizeof(next.members[i]),"%s",member.c_str());
    }
    if(!onx_panel_layout::valid(next))return "{\"error\":\"Use distinct entities and sensors in the sensor positions\"}";
    page(entity);
    if(!esphome::global_preferences || !preferences[slot].save(&next) || !esphome::global_preferences->sync())return "{\"error\":\"Could not save this page\"}";
    pages[slot]=next;
  }
  return onx_entity_config::build_json([](JsonObject root) {
    auto out=root["pages"].to<JsonArray>();
    for(int i=0;i<20;++i) {
      const auto entity=custom_logic::cached_device_entity_id(i);if(entity.empty())continue;
      const auto p=page(entity);auto item=out.add<JsonObject>();
      item["entity"]=entity;item["layout"]=onx_panel_layout::name(p.kind);
      auto members=item["members"].to<JsonArray>();
      const int count=p.kind==Kind::TWO_CONTROLS?1:p.kind==Kind::CONTROL_SENSORS?3:0;
      for(int j=0;j<count;++j)members.add(std::string(p.members[j]));
    }
  });
}
inline onx_detail_view::View timer_view(const std::string &entity,const std::string &title) {
  auto result=onx_detail_view::build(entity,title);
  const auto *d=custom_logic_entity_sub::entity_detail(entity);
  if(!d)return result;
  onx_timer::View timer;
  if(d->voice_timer) {
    const uint32_t age=esphome::millis()-d->timer_received_ms;
    if(d->state!="active" || age<5000)
      timer=onx_timer::view(d->state,d->voice_duration,d->voice_remaining,d->voice_remaining+1,1.0+age/1000.0);
  }else{
    time_t finish=0;
    const bool known=custom_logic_entity_sub::parse_ha_datetime_epoch(d->timer_finishes_at,&finish) ||
        (d->state=="idle" && custom_logic_entity_sub::parse_ha_datetime_epoch(d->timer_last_finish,&finish));
    timer=onx_timer::view(d->state,onx_timer::duration_seconds(d->timer_duration),
        onx_timer::duration_seconds(d->timer_remaining),onx_timer::deadline_epoch(known,finish),std::time(nullptr));
  }
  timer.ready &= d->available && d->received;
  result.value.text=timer.ready?onx_timer::countdown(timer.seconds_left):"--:--";
  result.context.text=!timer.ready?"Syncing":d->state=="paused"?"Paused":d->state=="idle"?"Idle":
      d->state=="ringing" || timer.seconds_left<=0?"Time is up":"Running";
  result.badge.text="Timer";result.value_ring=timer.ready?int(timer.fraction*1000):-1;
  return result;
}
struct Item {std::string entity,name,value,context,hint,icon; int ring{-1};bool available{false},graph{false},on{false};};
inline std::function<onx_detail_view::View(const std::string &,bool)> view_for;
inline Item item(const std::string &entity,bool detail=false) {
  Item out;out.entity=entity;
  if(configured(entity)<0){out.name="Item removed";out.value="—";out.context="Choose a replacement in page settings";return out;}
  out.name=custom_logic::ha_entity_title_from_registry_cache(entity);if(out.name.empty())out.name=entity;
  auto view=view_for?view_for(entity,detail):(detail?onx_detail_view::build(entity,out.name):onx_detail_view::carousel(entity,out.name));
  out.value=view.value.text;out.context=view.context.text;
  if(onx_panel_layout::sensor(entity) && !view.name.text.empty() && view.name.text!=out.name)out.context=view.name.text;
  if(detail && !view.badge.text.empty())out.context=view.badge.text+(out.context.empty()?"":" · "+out.context);
  out.hint=view.hint.text;
  out.icon=view.icon;out.available=view.available;out.ring=view.value_ring;out.graph=view.layout=="graph";
  // Active entities tint their icon with the accent colour.
  if(const auto *d=custom_logic_entity_sub::entity_detail(entity); d && d->available) {
    const auto &st=d->state;
    out.on=st=="on" || st=="playing" || st=="open" || st=="opening" || st=="active" ||
        (entity.rfind("climate.",0)==0 && st!="off");
  }
  return out;
}
inline lv_obj_t *layer=nullptr;
struct Card {
  lv_obj_t *box,*name,*value,*context,*hint,*icon,*bar;
  Item previous;
  onx_panel_layout::Rect rect{};
  Kind kind{Kind::SINGLE};
  uint32_t color{0};
  bool initialized{false},detail{false};
};
inline bool same(const Item &a,const Item &b) {
  return a.entity==b.entity && a.name==b.name && a.value==b.value && a.context==b.context &&
      a.hint==b.hint && a.icon==b.icon && a.ring==b.ring && a.available==b.available && a.graph==b.graph && a.on==b.on;
}
inline std::array<Card,4> cards{};
inline std::array<onx_panel_layout::Rect,4> rects{};
inline std::array<std::string,4> entities{};
inline int count=0;
inline bool showing=false,detail_view=false;
inline std::string parent_entity;
inline std::function<void(const std::string &)> open;
inline std::function<void(lv_obj_t *,const std::string &,int)> draw_graph;
inline const lv_font_t *small=nullptr,*normal=nullptr,*large=nullptr;
inline void setup(const lv_font_t *s,const lv_font_t *n,const lv_font_t *l) {
  small=s;normal=n;large=l;
  layer=lv_obj_create(lv_layer_top());lv_obj_remove_flag(layer,static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE));
  lv_obj_set_style_bg_color(layer,lv_color_hex(onx_display_settings::background()),0);lv_obj_set_style_bg_opa(layer,LV_OPA_COVER,0);
  lv_obj_set_style_border_width(layer,0,0);lv_obj_set_style_pad_all(layer,0,0);lv_obj_set_style_radius(layer,0,0);
  for(auto &c:cards) {
    c.box=lv_obj_create(layer);lv_obj_remove_flag(c.box,static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_set_style_bg_color(c.box,lv_color_hex(0x232323),0);lv_obj_set_style_border_width(c.box,0,0);
    lv_obj_set_style_radius(c.box,8,0);lv_obj_set_style_pad_all(c.box,0,0);
    lv_obj_set_style_shadow_width(c.box,0,0);lv_obj_set_style_outline_width(c.box,0,0);
    c.name=lv_label_create(c.box);c.value=lv_label_create(c.box);c.context=lv_label_create(c.box);c.hint=lv_label_create(c.box);
    for(auto *label:{c.name,c.value,c.context,c.hint}) {
      lv_label_set_long_mode(label,LV_LABEL_LONG_DOT);lv_obj_set_style_text_color(label,lv_color_hex(0xFFFFFF),0);
      lv_obj_remove_flag(label,LV_OBJ_FLAG_CLICKABLE);
    }
    c.icon=lv_image_create(c.box);c.bar=lv_bar_create(c.box);lv_bar_set_range(c.bar,0,1000);
    lv_obj_set_style_bg_color(c.bar,lv_color_hex(0x414850),LV_PART_MAIN);
    lv_obj_set_style_bg_color(c.bar,lv_color_hex(0x62B0FF),LV_PART_INDICATOR);
  }
  lv_obj_add_flag(layer,LV_OBJ_FLAG_HIDDEN);
}
inline std::vector<lv_obj_t *> suspended;
inline lv_obj_t *suspended_page=nullptr;
inline void restore_underlay() {
  for(auto *obj:suspended)if(lv_obj_is_valid(obj))lv_obj_remove_flag(obj,LV_OBJ_FLAG_HIDDEN);
  suspended.clear();suspended_page=nullptr;
}
inline void suspend_underlay() {
  auto *page=lv_scr_act();
  if(page!=suspended_page){restore_underlay();suspended_page=page;}
  // Keep the existing presenters/actions alive, but avoid rendering covered
  // base-layout body containers or letting their old touch widgets receive clicks.
  // The navigator and native overlays live separately on lv_layer_top().
  for(uint32_t i=0;i<lv_obj_get_child_count(page);++i) {
    auto *obj=lv_obj_get_child(page,i);
    if(!lv_obj_check_type(obj,&lv_obj_class) || lv_obj_has_flag(obj,LV_OBJ_FLAG_HIDDEN))continue;
    if(std::find(suspended.begin(),suspended.end(),obj)==suspended.end())suspended.push_back(obj);
    lv_obj_add_flag(obj,LV_OBJ_FLAG_HIDDEN);
  }
}
inline void hide() {
  onx_panel_detail::hide();
  showing=false;if(layer)lv_obj_add_flag(layer,LV_OBJ_FLAG_HIDDEN);
  if(suspended_page)restore_underlay();
}
inline void label(lv_obj_t *obj,const std::string &text,int x,int y,int w,const lv_font_t *font) {
  onx_render::style_text_font(obj,font,0);onx_render::label(obj,text.c_str());
  lv_obj_set_pos(obj,x,y);lv_obj_set_width(obj,w);
  if(text.empty())lv_obj_add_flag(obj,LV_OBJ_FLAG_HIDDEN);else lv_obj_remove_flag(obj,LV_OBJ_FLAG_HIDDEN);
}
template<typename Bind> void render(const std::string &entity,int width,int height,bool detail,Bind bind) {
  if(!layer)return;
  suspend_underlay();
  onx_render::style_bg_color(layer,lv_color_hex(onx_display_settings::background()),0);
  const auto p=detail?Page{}:page(entity);const auto kind=p.kind;
  rects=onx_panel_layout::geometry(kind,width,height,detail);
  entities[0]=entity;count=kind==Kind::TWO_CONTROLS?2:kind==Kind::CONTROL_SENSORS?4:1;
  for(int i=1;i<count;++i)entities[i]=p.members[i-1];
  lv_obj_set_size(layer,width,height);lv_obj_set_pos(layer,0,0);
  // Keep the dock, dashboard and bell above the opaque panel content.
  if(lv_obj_get_index(layer)!=0)lv_obj_move_background(layer);lv_obj_remove_flag(layer,LV_OBJ_FLAG_HIDDEN);showing=true;detail_view=detail;
  if(!onx_panel_detail::bind_icon)onx_panel_detail::bind_icon=[bind](lv_obj_t *icon,const std::string &target){bind(icon,target);};
  // Redesigned full-screen detail screens with direct touch controls.
  if(detail && onx_panel_detail::supports(entity)) {
    for(auto &c:cards){lv_obj_add_flag(c.box,LV_OBJ_FLAG_HIDDEN);c.initialized=false;}
    auto title=custom_logic::ha_entity_title_from_registry_cache(entity);if(title.empty())title=entity;
    const auto view=view_for?view_for(entity,true):onx_detail_view::build(entity,title);
    onx_panel_detail::render(layer,entity,onx_media::display_text(title),view,width,height);
    onx_panel_detail::tick(entity);
    return;
  }
  // Main pages: cards drawn by the shared widget engine (onx_panel_detail.h).
  for(auto &c:cards){lv_obj_add_flag(c.box,LV_OBJ_FLAG_HIDDEN);c.initialized=false;}
  std::vector<onx_panel_detail::Tile> tiles;
  for(int i=0;i<count;++i) {
    const auto v=item(entities[i],false);const auto r=rects[i];
    onx_panel_detail::Tile t;
    t.entity=entities[i];t.name=onx_media::display_text(v.name);t.value=v.value;t.context=v.context;
    t.on=v.on;t.ring=v.ring;t.r={r.x,r.y,r.w,r.h};t.compact=kind==Kind::CONTROL_SENSORS && i>0;
    tiles.push_back(std::move(t));
  }
  onx_panel_detail::render_main(layer,tiles,width,height);
}
// The carousel's Settings item, on the same base as the other main pages.
inline void render_settings(int width,int height) {
  if(!layer)return;
  suspend_underlay();
  onx_render::style_bg_color(layer,lv_color_hex(onx_display_settings::background()),0);
  for(auto &c:cards){lv_obj_add_flag(c.box,LV_OBJ_FLAG_HIDDEN);c.initialized=false;}
  rects=onx_panel_layout::geometry(Kind::SINGLE,width,height,false);
  entities[0]=onx_panel_detail::SETTINGS;count=1;
  lv_obj_set_size(layer,width,height);lv_obj_set_pos(layer,0,0);
  if(lv_obj_get_index(layer)!=0)lv_obj_move_background(layer);
  lv_obj_remove_flag(layer,LV_OBJ_FLAG_HIDDEN);showing=true;detail_view=false;
  onx_panel_detail::Tile t;
  t.entity=onx_panel_detail::SETTINGS;t.name="Settings";
  t.r={rects[0].x,rects[0].y,rects[0].w,rects[0].h};
  onx_panel_detail::render_main(layer,{t},width,height);
}
// The panel's own Assist timers: full screen, above the dock.
inline void show_timers(int width,int height) {
  if(!layer)return;
  suspend_underlay();
  onx_render::style_bg_color(layer,lv_color_hex(onx_display_settings::background()),0);
  for(auto &c:cards){lv_obj_add_flag(c.box,LV_OBJ_FLAG_HIDDEN);c.initialized=false;}
  lv_obj_set_size(layer,width,height);lv_obj_set_pos(layer,0,0);
  lv_obj_move_foreground(layer);lv_obj_remove_flag(layer,LV_OBJ_FLAG_HIDDEN);showing=true;detail_view=true;
  onx_panel_detail::render_timers(layer,width,height);
}
inline bool tap(int x,int y) {
  if(showing && detail_view)return onx_panel_detail::active;  // Only its own controls act.
  if(!showing)return false;
  // Settings: the shared press action opens the menu.
  if(entities[0]==onx_panel_detail::SETTINGS)return false;
  for(int i=0;i<count;++i) {
    const auto r=rects[i];if(x>=r.x && x<r.x+r.w && y>=r.y && y<r.y+r.h) {
      if(configured(entities[i])>=0 && open){parent_entity=entities[0];open(entities[i]);}return true;
    }
  }
  return false;
}
}
