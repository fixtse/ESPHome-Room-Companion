#pragma once
#include "onx_panel_layout.h"
#include "onx_entity_config.h"
#include "onx_render.h"
#include "onx_navigation.h"

namespace onx_panel_pages {
using onx_panel_layout::Page;
using onx_panel_layout::Kind;
inline std::array<Page,20> pages{};
inline std::array<esphome::ESPPreferenceObject,20> preferences;
inline bool loaded=false;
inline void load() {
  if(loaded || !esphome::global_preferences)return;
  for(size_t i=0;i<pages.size();++i) {
    preferences[i]=esphome::global_preferences->make_preference<Page>(esphome::fnv1_hash("rc.page.v1."+std::to_string(i)));
    Page saved{};
    if(preferences[i].load(&saved)) {
      saved.primary[128]=0;for(auto &m:saved.members)m[128]=0;
      if(onx_panel_layout::valid(saved))pages[i]=saved;
    }
  }
  loaded=true;
}
inline int configured(const std::string &entity) {
  if(entity.empty())return -1;
  for(int i=0;i<20;++i)if(custom_logic::cached_device_entity_id(i)==entity)return i;
  return -1;
}
inline Page page(const std::string &entity) {
  load();
  for(const auto &p:pages)if(entity==p.primary && onx_panel_layout::valid(p))return p;
  Page p;snprintf(p.primary,sizeof(p.primary),"%s",entity.c_str());return p;
}
inline std::string rpc(const std::string &op,const std::string &data) {
  if(op!="panel_pages" && op!="panel_page_save")return "";
  load();
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
    if(!loaded || !preferences[slot].save(&next) || !esphome::global_preferences->sync())return "{\"error\":\"Could not save this page\"}";
    // Clear an older location for the same entity in RAM; entity identity guards
    // against reordering or replacing configured slots across restarts.
    for(auto &p:pages)if(entity==p.primary)p=Page{};
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
      for(int j=0;j<count;++j)members.add(p.members[j]);
    }
  });
}
struct Item {std::string entity,name,value,context,hint,icon; int ring{-1};bool available{false};};
inline Item item(const std::string &entity,bool detail=false) {
  Item out;out.entity=entity;
  if(configured(entity)<0){out.name="Item removed";out.value="—";out.context="Choose a replacement in page settings";return out;}
  out.name=custom_logic::ha_entity_title_from_registry_cache(entity);if(out.name.empty())out.name=entity;
  auto view=detail?onx_detail_view::build(entity,out.name):onx_detail_view::carousel(entity,out.name);
  const int config=configured(entity),ui=custom_logic::normal_ui_slot_for_device_config_slot(config);
  if(detail)onx_entity_config::customize_control_view(ui,entity,view);
  out.value=view.value.text;out.context=view.context.text;out.hint=view.hint.text;
  out.icon=view.icon;out.available=view.available;out.ring=view.value_ring;
  return out;
}
inline lv_obj_t *layer=nullptr;
struct Card {lv_obj_t *box,*name,*value,*context,*hint,*icon,*bar;};
inline std::array<Card,4> cards{};
inline std::array<onx_panel_layout::Rect,4> rects{};
inline std::array<std::string,4> entities{};
inline int count=0;
inline bool showing=false,detail_view=false;
inline std::function<void(const std::string &)> open;
inline const lv_font_t *small=nullptr,*normal=nullptr,*large=nullptr;
inline void setup(const lv_font_t *s,const lv_font_t *n,const lv_font_t *l) {
  small=s;normal=n;large=l;
  layer=lv_obj_create(lv_layer_top());lv_obj_remove_flag(layer,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(layer,lv_color_hex(0x101418),0);lv_obj_set_style_bg_opa(layer,LV_OPA_COVER,0);
  lv_obj_set_style_border_width(layer,0,0);lv_obj_set_style_pad_all(layer,0,0);lv_obj_set_style_radius(layer,0,0);
  for(auto &c:cards) {
    c.box=lv_obj_create(layer);lv_obj_remove_flag(c.box,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(c.box,lv_color_hex(0x232323),0);lv_obj_set_style_border_width(c.box,0,0);
    lv_obj_set_style_radius(c.box,8,0);lv_obj_set_style_pad_all(c.box,0,0);
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
inline void hide() {showing=false;if(layer)lv_obj_add_flag(layer,LV_OBJ_FLAG_HIDDEN);}
inline void label(lv_obj_t *obj,const std::string &text,int x,int y,int w,const lv_font_t *font) {
  onx_render::style_text_font(obj,font,0);onx_render::label(obj,text.c_str());
  lv_obj_set_pos(obj,x,y);lv_obj_set_width(obj,w);
  if(text.empty())lv_obj_add_flag(obj,LV_OBJ_FLAG_HIDDEN);else lv_obj_remove_flag(obj,LV_OBJ_FLAG_HIDDEN);
}
template<typename Bind> void render(const std::string &entity,int width,int height,bool detail,Bind bind) {
  if(!layer)return;
  const auto p=detail?Page{}:page(entity);const auto kind=p.kind;
  rects=onx_panel_layout::geometry(kind,width,height,detail);
  entities[0]=entity;count=kind==Kind::TWO_CONTROLS?2:kind==Kind::CONTROL_SENSORS?4:1;
  for(int i=1;i<count;++i)entities[i]=p.members[i-1];
  lv_obj_set_size(layer,width,height);lv_obj_set_pos(layer,0,0);
  // Keep the dock, dashboard and bell above the opaque panel content.
  lv_obj_move_background(layer);lv_obj_remove_flag(layer,LV_OBJ_FLAG_HIDDEN);showing=true;detail_view=detail;
  for(int i=0;i<4;++i) {
    auto &c=cards[i];if(i>=count){lv_obj_add_flag(c.box,LV_OBJ_FLAG_HIDDEN);continue;}
    lv_obj_remove_flag(c.box,LV_OBJ_FLAG_HIDDEN);const auto r=rects[i];
    lv_obj_set_pos(c.box,r.x,r.y);lv_obj_set_size(c.box,r.w,r.h);
    const auto v=item(entities[i],detail);
    const bool compact=kind==Kind::CONTROL_SENSORS && i>0;
    const int pad=12;
    label(c.name,v.name,pad,pad,r.w-pad*2,small);
    label(c.value,v.value,pad,compact?r.h/2: r.h*39/100,r.w-pad*2,compact?normal:large);
    label(c.context,compact?"":v.context,pad,r.h*67/100,r.w-pad*2,small);
    label(c.hint,detail?v.hint:"",pad,r.h-48,r.w-pad*2,small);
    if(!compact && r.h>150) {
      bind(c.icon,entities[i]);lv_image_set_scale(c.icon,128);
      lv_obj_align(c.icon,LV_ALIGN_TOP_RIGHT,-pad,28);
      lv_obj_remove_flag(c.icon,LV_OBJ_FLAG_HIDDEN);
    }else lv_obj_add_flag(c.icon,LV_OBJ_FLAG_HIDDEN);
    if(v.ring>=0 && !compact) {
      lv_obj_set_pos(c.bar,pad,r.h-16);lv_obj_set_size(c.bar,r.w-pad*2,4);
      lv_bar_set_value(c.bar,v.ring,LV_ANIM_OFF);lv_obj_remove_flag(c.bar,LV_OBJ_FLAG_HIDDEN);
    }else lv_obj_add_flag(c.bar,LV_OBJ_FLAG_HIDDEN);
  }
}
inline bool tap(int x,int y) {
  if(!showing || detail_view)return false;
  for(int i=0;i<count;++i) {
    const auto r=rects[i];if(x>=r.x && x<r.x+r.w && y>=r.y && y<r.y+r.h) {
      if(configured(entities[i])>=0 && open)open(entities[i]);return true;
    }
  }
  return false;
}
}
