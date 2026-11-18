#pragma once
#include "onx_panel_layout.h"
#include <functional>
#include <cstdio>
#include <cstring>
#include <vector>
#include "onx_entity_config.h"
#include "onx_render.h"
#include "onx_navigation.h"
#include "onx_panel_ui.h"
#include "onx_panel_detail.h"

namespace onx_panel_pages {
using onx_panel_layout::Page;
using onx_panel_layout::Kind;
// Pages are the carousel stops, in the editor's order. Each has its own name and
// carousel icon and places 1, 2 or 4 configuration entries (full; top/bottom;
// large + three compact). Entries keep their own shortcut, gestures and quick
// actions. Entries are stored by configuration index (stable), not UI index.
constexpr size_t MAX_PAGES=onx_memory::ENTRY_COUNT;
constexpr uint8_t NO_ENTRY=0xFF;
struct StoredPage {
  char name[49]{}, icon[65]{};
  uint8_t kind{0};
  uint8_t entries[4]{NO_ENTRY,NO_ENTRY,NO_ENTRY,NO_ENTRY};
};
struct StoredPages {
  uint32_t magic{0x52435032};
  uint8_t count{0};
  StoredPage pages[MAX_PAGES]{};
};
inline StoredPages &stored=onx_memory::psram_object<StoredPages>();
inline esphome::ESPPreferenceObject preference;
inline bool loaded=false;
// Set when entries change (slot save, preset, import); reconciled lazily.
inline bool dirty=true;
inline int size_of(Kind k){return k==Kind::TWO_CONTROLS?2:k==Kind::CONTROL_SENSORS?4:1;}
inline int configured(const std::string &entity) {
  if(entity.empty())return -1;
  for(int i=0;i<int(onx_memory::ENTRY_COUNT);++i)if(custom_logic::cached_device_entity_id(i)==entity)return i;
  return -1;
}
inline std::string entry_entity(uint8_t entry) {
  return entry<onx_memory::ENTRY_COUNT?custom_logic::cached_device_entity_id(entry):std::string();
}
inline bool placed(const StoredPages &list,int entry) {
  for(int p=0;p<list.count;++p)for(auto e:list.pages[p].entries)if(e==entry)return true;
  return false;
}
// Drops entries that were cleared and gives every unplaced entry its own page,
// so nothing configured becomes unreachable.
inline void reconcile(StoredPages &list) {
  int out=0;
  for(int p=0;p<list.count;++p) {
    auto page=list.pages[p];bool complete=true;
    for(int i=0;i<size_of(Kind(page.kind));++i)complete&=!entry_entity(page.entries[i]).empty();
    if(!complete) {
      // Keep what remains as single pages rather than a broken layout.
      for(int i=0;i<4;++i)if(!entry_entity(page.entries[i]).empty() && out<int(MAX_PAGES)) {
        StoredPage single{};if(i==0){std::memcpy(single.name,page.name,sizeof(single.name));std::memcpy(single.icon,page.icon,sizeof(single.icon));}
        single.entries[0]=page.entries[i];list.pages[out++]=single;
      }
      continue;
    }
    for(int i=size_of(Kind(page.kind));i<4;++i)page.entries[i]=NO_ENTRY;
    list.pages[out++]=page;
  }
  list.count=out;
  for(int e=0;e<int(onx_memory::ENTRY_COUNT) && list.count<MAX_PAGES;++e)
    if(!entry_entity(e).empty() && !placed(list,e)){StoredPage single{};single.entries[0]=e;list.pages[list.count++]=single;}
}
// One-time migration from per-entity layouts (rc.page.v1.<entity>).
inline void migrate() {
  stored=StoredPages{};
  for(int e=0;e<int(onx_memory::ENTRY_COUNT);++e) {
    const auto entity=entry_entity(e);if(entity.empty() || placed(stored,e))continue;
    StoredPage page{};page.entries[0]=e;
    auto old_pref=esphome::global_preferences->make_preference<Page>(esphome::fnv1_hash("rc.page.v1."+entity));
    Page old{};
    if(old_pref.load(&old) && entity==old.primary && onx_panel_layout::valid(old)) {
      bool ok=true;uint8_t members[3]{NO_ENTRY,NO_ENTRY,NO_ENTRY};
      for(int i=0;i<size_of(old.kind)-1;++i){const int m=configured(old.members[i]);ok&=m>=0 && !placed(stored,m);members[i]=m<0?NO_ENTRY:m;}
      if(ok){page.kind=uint8_t(old.kind);for(int i=0;i<3;++i)page.entries[i+1]=members[i];}
    }
    stored.pages[stored.count++]=page;
  }
}
inline bool load() {
  if(loaded)return true;
  if(!esphome::global_preferences)return false;
  preference=esphome::global_preferences->make_preference<StoredPages>(esphome::fnv1_hash("rc.pages.v2"));
  const uint32_t magic=StoredPages{}.magic;
  if(!preference.load(&stored) || stored.magic!=magic || stored.count>MAX_PAGES)migrate();
  for(auto &p:stored.pages){p.name[48]=0;p.icon[64]=0;if(p.kind>2)p.kind=0;}
  loaded=true;return true;
}
inline void refresh(){if(dirty){reconcile(stored);dirty=false;}}
inline int page_index(const std::string &entity) {
  if(!load())return -1;refresh();
  const int entry=configured(entity);if(entry<0)return -1;
  for(int p=0;p<stored.count;++p)for(auto e:stored.pages[p].entries)if(e==entry)return p;
  return -1;
}
// The page drawn when the carousel stops at 'entity' (a page's first entity).
inline Page page(const std::string &entity) {
  Page out;snprintf(out.primary,sizeof(out.primary),"%s",entity.c_str());
  const int p=page_index(entity);
  if(p<0 || entry_entity(stored.pages[p].entries[0])!=entity)return out;
  out.kind=Kind(stored.pages[p].kind);
  for(int i=1;i<size_of(out.kind);++i)snprintf(out.members[i-1],sizeof(out.members[i-1]),"%s",entry_entity(stored.pages[p].entries[i]).c_str());
  return out;
}
// Page name/icon for the carousel; empty when the page uses its entity's.
inline const StoredPage *info(const std::string &entity) {
  const int p=page_index(entity);return p<0?nullptr:&stored.pages[p];
}
inline std::vector<int> order() {
  std::vector<int> out;
  if(!load()) {
    for(int e=0;e<int(onx_memory::ENTRY_COUNT);++e)if(!entry_entity(e).empty())out.push_back(int(custom_logic::normal_ui_slot_for_device_config_slot(e)));
    return out;
  }
  refresh();
  for(int p=0;p<stored.count;++p)out.push_back(int(custom_logic::normal_ui_slot_for_device_config_slot(stored.pages[p].entries[0])));
  return out;
}
inline const bool order_installed=(onx_entity_config::page_order=&order,true);
inline bool valid_icon(const std::string &icon) {
  if(icon.empty())return true;
  if(icon.size()>64 || icon.rfind("mdi:",0)!=0 || icon.size()<5)return false;
  for(size_t i=4;i<icon.size();++i){const char c=icon[i];if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'))return false;}
  return true;
}
inline std::string rpc(const std::string &op,const std::string &data) {
  // Native (portrait) size and the orientation in use since boot, so the
  // configuration preview matches either model and orientation.
  if(op=="panel_screen")return onx_entity_config::build_json([](JsonObject root) {
    root["width"]=onx_panel_ui::native_width;root["height"]=onx_panel_ui::native_height;
    root["orientation"]=onx_panel_ui::turns*90;
  });
  if(op!="panel_pages" && op!="panel_pages_save")return "";
  if(!load())return "{\"error\":\"Page storage is not ready\"}";
  if(op=="panel_pages_save") {
    JsonDocument doc;
    if(deserializeJson(doc,data)!=DeserializationError::Ok)return "{\"error\":\"Invalid page configuration\"}";
    auto pages=doc["pages"].as<JsonArrayConst>();
    if(pages.size()>MAX_PAGES)return "{\"error\":\"Too many pages\"}";
    auto next_buffer=custom_logic::make_ram_allocated<StoredPages>();
    if(!next_buffer)return "{\"error\":\"Not enough memory to save pages\"}";
    auto &next=*next_buffer;next=StoredPages{};
    for(JsonObjectConst item:pages) {
      StoredPage page{};Kind kind;
      const std::string name=item["name"] | "",icon=item["icon"] | "";
      if(!onx_panel_layout::parse(item["layout"] | "",kind))return "{\"error\":\"Choose a layout for every page\"}";
      if(name.size()>48)return "{\"error\":\"Page names are limited to 48 bytes\"}";
      if(!valid_icon(icon))return "{\"error\":\"Icons use the mdi:name form\"}";
      auto entities=item["entities"].as<JsonArrayConst>();
      if(entities.size()!=size_t(size_of(kind)))return "{\"error\":\"Fill every position on each page\"}";
      for(int i=0;i<size_of(kind);++i) {
        const std::string entity=entities[i] | "";const int entry=configured(entity);
        if(entry<0)return "{\"error\":\"Save the page entities before the pages\"}";
        if(placed(next,entry))return "{\"error\":\"Each entity can be on one page only\"}";
        for(int j=0;j<i;++j)if(page.entries[j]==entry)return "{\"error\":\"Each entity can be on one page only\"}";
        if(kind==Kind::CONTROL_SENSORS && i>0 && !onx_panel_layout::sensor(entity))return "{\"error\":\"Compact positions show sensors and binary sensors\"}";
        page.entries[i]=uint8_t(entry);
      }
      page.kind=uint8_t(kind);
      snprintf(page.name,sizeof(page.name),"%s",name.c_str());snprintf(page.icon,sizeof(page.icon),"%s",icon.c_str());
      next.pages[next.count++]=page;
    }
    reconcile(next);
    if(!preference.save(&next) || !esphome::global_preferences->sync())return "{\"error\":\"Could not save pages\"}";
    stored=next;
  }
  reconcile(stored);
  return onx_entity_config::build_json([](JsonObject root) {
    auto out=root["pages"].to<JsonArray>();
    for(int p=0;p<stored.count;++p) {
      const auto &page=stored.pages[p];auto item=out.add<JsonObject>();
      item["name"]=std::string(page.name);item["icon"]=std::string(page.icon);
      item["layout"]=onx_panel_layout::name(Kind(page.kind));
      auto entities=item["entities"].to<JsonArray>();
      for(int i=0;i<size_of(Kind(page.kind));++i)entities.add(entry_entity(page.entries[i]));
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
  if (detail) suspend_underlay();
  else if (suspended_page) restore_underlay();
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
  if (suspended_page) restore_underlay();
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
