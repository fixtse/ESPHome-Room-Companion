#pragma once
#include "onx_panel_layout.h"
#include <functional>
#include <cstdio>
#include "onx_entity_config.h"
#include "onx_render.h"
#include "onx_navigation.h"

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
struct Item {std::string entity,name,value,context,hint,icon; int ring{-1};bool available{false},graph{false};};
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
      a.hint==b.hint && a.icon==b.icon && a.ring==b.ring && a.available==b.available && a.graph==b.graph;
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
  lv_obj_set_style_bg_color(layer,lv_color_hex(0x101418),0);lv_obj_set_style_bg_opa(layer,LV_OPA_COVER,0);
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
  // knob body containers or letting their old touch widgets receive clicks.
  // The navigator and native overlays live separately on lv_layer_top().
  for(uint32_t i=0;i<lv_obj_get_child_count(page);++i) {
    auto *obj=lv_obj_get_child(page,i);
    if(!lv_obj_check_type(obj,&lv_obj_class) || lv_obj_has_flag(obj,LV_OBJ_FLAG_HIDDEN))continue;
    if(std::find(suspended.begin(),suspended.end(),obj)==suspended.end())suspended.push_back(obj);
    lv_obj_add_flag(obj,LV_OBJ_FLAG_HIDDEN);
  }
}
inline void hide() {
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
  const auto p=detail?Page{}:page(entity);const auto kind=p.kind;
  rects=onx_panel_layout::geometry(kind,width,height,detail);
  entities[0]=entity;count=kind==Kind::TWO_CONTROLS?2:kind==Kind::CONTROL_SENSORS?4:1;
  for(int i=1;i<count;++i)entities[i]=p.members[i-1];
  lv_obj_set_size(layer,width,height);lv_obj_set_pos(layer,0,0);
  // Keep the dock, dashboard and bell above the opaque panel content.
  if(lv_obj_get_index(layer)!=0)lv_obj_move_background(layer);lv_obj_remove_flag(layer,LV_OBJ_FLAG_HIDDEN);showing=true;detail_view=detail;
  for(int i=0;i<4;++i) {
    auto &c=cards[i];if(i>=count){lv_obj_add_flag(c.box,LV_OBJ_FLAG_HIDDEN);continue;}
    lv_obj_remove_flag(c.box,LV_OBJ_FLAG_HIDDEN);const auto r=rects[i];
    lv_obj_set_pos(c.box,r.x,r.y);lv_obj_set_size(c.box,r.w,r.h);
    const auto v=item(entities[i],detail);
    const bool compact=kind==Kind::CONTROL_SENSORS && i>0;
    const bool unchanged=c.initialized && c.kind==kind && c.detail==detail &&
        c.rect.x==r.x && c.rect.y==r.y && c.rect.w==r.w && c.rect.h==r.h &&
        c.color==onx_display_settings::current.media_color && same(c.previous,v);
    if(unchanged) {
      // Rebind asynchronously downloaded icons, but don't reset the layout or
      // label styles ten times per second when the presentation is unchanged.
      if(!compact && !v.graph)bind(c.icon,entities[i]);
      if(v.graph && detail && draw_graph)draw_graph(c.box,entities[i],r.h*38/100);
      continue;
    }
    c.previous=v;c.rect=r;c.kind=kind;c.detail=detail;c.color=onx_display_settings::current.media_color;c.initialized=true;
    const int pad=12;
    label(c.name,v.name,pad,compact?(r.h-lv_font_get_line_height(small))/2:pad,compact?r.w*55/100-pad:kind==Kind::TWO_CONTROLS?r.w-64:r.w-pad*2,small);
    const auto *value_font=compact?(r.h>=lv_font_get_line_height(normal)+8?normal:small):(r.h<150?normal:large);
    label(c.value,v.value,compact?r.w*55/100:pad,
          compact?(r.h-lv_font_get_line_height(value_font))/2:r.h*(v.graph?18:kind==Kind::TWO_CONTROLS?35:49)/100,
          compact?r.w*45/100-pad:r.w-pad*2,value_font);
    label(c.context,compact?"":v.context,pad,r.h*67/100,r.w-pad*2,small);
    label(c.hint,detail?v.hint:"",pad,r.h-48,r.w-pad*2,small);
    if(v.graph && detail && draw_graph)draw_graph(c.box,entities[i],r.h*38/100);
    if(!compact && !v.graph) {
      bind(c.icon,entities[i]);lv_image_set_scale(c.icon,kind==Kind::TWO_CONTROLS?64:96);
      onx_render::align(c.icon,LV_ALIGN_TOP_RIGHT,-pad,kind==Kind::TWO_CONTROLS?12:28);
      lv_obj_remove_flag(c.icon,LV_OBJ_FLAG_HIDDEN);
    }else lv_obj_add_flag(c.icon,LV_OBJ_FLAG_HIDDEN);
    if(v.ring>=0 && !compact) {
      onx_render::style_bg_color(c.bar,lv_color_hex(onx_display_settings::current.media_color),LV_PART_INDICATOR);
      lv_obj_set_pos(c.bar,pad,r.h-16);lv_obj_set_size(c.bar,r.w-pad*2,4);
      lv_bar_set_value(c.bar,v.ring,LV_ANIM_OFF);lv_obj_remove_flag(c.bar,LV_OBJ_FLAG_HIDDEN);
    }else lv_obj_add_flag(c.bar,LV_OBJ_FLAG_HIDDEN);
  }
}
inline bool tap(int x,int y) {
  if(!showing || detail_view)return false;
  for(int i=0;i<count;++i) {
    const auto r=rects[i];if(x>=r.x && x<r.x+r.w && y>=r.y && y<r.y+r.h) {
      if(configured(entities[i])>=0 && open){parent_entity=entities[0];open(entities[i]);}return true;
    }
  }
  return false;
}
}
