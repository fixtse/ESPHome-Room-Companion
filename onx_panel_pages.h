#pragma once
#include "onx_panel_layout.h"
#include <functional>
#include <cstdio>
#include <cstring>
#include <vector>
#include "onx_entity_config.h"
#include "onx_presets.h"
#include "onx_render.h"
#include "onx_navigation.h"
#include "onx_panel_ui.h"
#include "onx_panel_detail.h"

namespace onx_panel_pages {
using onx_panel_layout::Page;
using onx_panel_layout::Kind;
// Pages use stable configuration indices so all keys share HA subscriptions.
constexpr size_t MAX_PAGES=onx_memory::ENTRY_COUNT;
constexpr uint8_t NO_ENTRY=0xFF;
using onx_panel_layout::MAX_CELLS;
struct LegacyPage {
  char name[49]{}, icon[65]{};
  uint8_t kind{0}, entries[4]{};
};
struct LegacyPages {
  uint32_t magic;
  uint8_t count;
  LegacyPage pages[MAX_PAGES];
};
// The artwork index uses six bits. Keep the presentation flag in its
// unused high bit so existing page preferences retain their exact format.
constexpr uint8_t ICON_ONLY=0x80;
constexpr uint8_t ART_INDEX=0x7F;
static_assert(onx_panel_artwork::LIMIT<=ART_INDEX);
struct StoredPage {
  char name[49]{}, icon[65]{};
  uint8_t kind{0}, rows{2}, cols{2}, carousel_artwork{0};
  std::array<uint8_t,MAX_CELLS> entries{}, artwork{};
  StoredPage() { entries.fill(NO_ENTRY); }
};
struct StoredPages {
  static constexpr uint32_t MAGIC=0x52435033;
  uint32_t magic{MAGIC};
  uint8_t count{0};
  StoredPage pages[MAX_PAGES]{};
  std::array<uint64_t,onx_panel_artwork::LIMIT> images{};
};
inline StoredPages &stored=onx_memory::psram_object<StoredPages>();
// Icon animations for action-grid keys, per entity entry (so they follow the
// entity through page edits). A separate preference keeps StoredPages' format.
enum Effect : uint8_t {EFFECT_NONE=0,EFFECT_PULSE=1,EFFECT_BLINK=2,EFFECT_SPIN=3};
inline const char *effect_name(uint8_t e){return e==EFFECT_PULSE?"pulse":e==EFFECT_BLINK?"blink":e==EFFECT_SPIN?"spin":"none";}
inline int effect_parse(const std::string &s){return s=="none"?EFFECT_NONE:s=="pulse"?EFFECT_PULSE:s=="blink"?EFFECT_BLINK:s=="spin"?EFFECT_SPIN:-1;}
struct StoredEffects {
  static constexpr uint32_t MAGIC=0x52434658;
  uint32_t magic{MAGIC};
  std::array<uint8_t,onx_memory::ENTRY_COUNT> fx{};
};
inline StoredEffects effects{};
inline esphome::ESPPreferenceObject effects_preference;
inline uint8_t effect_of(uint8_t entry){return entry<onx_memory::ENTRY_COUNT?effects.fx[entry]:EFFECT_NONE;}
inline esphome::ESPPreferenceObject preference;
inline bool loaded=false;
// Set when entries change (slot save, preset, import); reconciled lazily.
inline bool dirty=true;
inline int size_of(Kind k){return k==Kind::TWO_CONTROLS?2:k==Kind::CONTROL_SENSORS?4:1;}
inline int size_of(const StoredPage &page) {
  return Kind(page.kind)==Kind::ACTION_GRID ? page.rows*page.cols : size_of(Kind(page.kind));
}
inline uint8_t lead(const StoredPage &page) {
  for(int i=0;i<size_of(page);++i)if(page.entries[i]!=NO_ENTRY)return page.entries[i];
  return NO_ENTRY;
}
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
    auto page=list.pages[p];
    const bool grid=Kind(page.kind)==Kind::ACTION_GRID;
    const int n=size_of(page);
    bool complete=true;
    for(int i=0;i<n;++i) {
      if(!entry_entity(page.entries[i]).empty())continue;
      page.entries[i]=NO_ENTRY;
      page.artwork[i]=0;
      complete=false;
    }
    if(grid && lead(page)==NO_ENTRY)continue;
    if(!grid && !complete) {
      // Keep the remaining entries from an incomplete legacy layout.
      for(int i=0;i<n;++i)if(page.entries[i]!=NO_ENTRY && out<int(MAX_PAGES)) {
        StoredPage single{};
        single.artwork[0]=page.artwork[i];
        if(i==0)single.carousel_artwork=page.carousel_artwork;
        if(i==0){std::memcpy(single.name,page.name,sizeof(single.name));std::memcpy(single.icon,page.icon,sizeof(single.icon));}
        single.entries[0]=page.entries[i];list.pages[out++]=single;
      }
      continue;
    }
    if(out>=int(MAX_PAGES))break;
    for(int i=n;i<MAX_CELLS;++i)page.entries[i]=NO_ENTRY;
    // If clearing an entry changed the lead to one already used, leave
    // the remaining entities for the unplaced-entry pass.
    const auto first=lead(page);
    if(std::any_of(list.pages,list.pages+out,[first](const StoredPage &p){return lead(p)==first;}))continue;
    list.pages[out++]=page;
  }
  list.count=out;
  // Registered timers (timer./sensor. with the timer flag) need no page: their
  // popups and the timer screen find them without a carousel stop.
  for(int e=0;e<int(onx_memory::ENTRY_COUNT) && list.count<MAX_PAGES;++e)
    if(!entry_entity(e).empty() && !placed(list,e) && !onx_entity_config::timer_hidden(e,entry_entity(e))){StoredPage single{};single.entries[0]=e;list.pages[list.count++]=single;}
}
// One-time migration from per-entity layouts (rc.page.v1.<entity>).
inline void migrate() {
  stored.magic=StoredPages::MAGIC;
  stored.count=0;
  stored.images.fill(0);
  for(int e=0;e<int(onx_memory::ENTRY_COUNT);++e) {
    const auto entity=entry_entity(e);if(entity.empty() || placed(stored,e) || onx_entity_config::timer_hidden(e,entity))continue;
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
  preference=esphome::global_preferences->make_preference<StoredPages>(esphome::fnv1_hash("rc.pages.v3"));
  effects_preference=esphome::global_preferences->make_preference<StoredEffects>(esphome::fnv1_hash("rc.pages.fx.v1"));
  if(!effects_preference.load(&effects) || effects.magic!=StoredEffects::MAGIC)effects=StoredEffects{};
  for(auto &f:effects.fx)if(f>EFFECT_SPIN)f=EFFECT_NONE;
  if(!preference.load(&stored) || stored.magic!=StoredPages::MAGIC || stored.count>MAX_PAGES) {
    auto legacy=custom_logic::make_ram_allocated<LegacyPages>();
    auto old_pref=esphome::global_preferences->make_preference<LegacyPages>(esphome::fnv1_hash("rc.pages.v2"));
    if(!legacy || !old_pref.load(legacy.get()) || legacy->magic!=0x52435032 || legacy->count>MAX_PAGES) {
      migrate();
    } else {
      stored.magic=StoredPages::MAGIC;
      stored.count=legacy->count;
      stored.images.fill(0);
      for(int i=0;i<stored.count;++i) {
        const auto &old=legacy->pages[i];
        auto &page=stored.pages[i];
        page=StoredPage{};
        std::memcpy(page.name,old.name,sizeof(page.name));
        std::memcpy(page.icon,old.icon,sizeof(page.icon));
        page.kind=old.kind<=2?old.kind:0;
        std::copy_n(old.entries,4,page.entries.begin());
      }
    }
  }
  for(int i=0;i<stored.count;++i) {
    auto &p=stored.pages[i];
    p.name[48]=0;p.icon[64]=0;
    if(p.kind>uint8_t(Kind::ACTION_GRID) || !onx_panel_layout::grid_axis(p.rows) || !onx_panel_layout::grid_axis(p.cols)) {
      p.kind=0;p.rows=2;p.cols=2;
    }
  }
  loaded=true;return true;
}
inline uint32_t seen_revision=0;
inline void refresh(){if(dirty || seen_revision!=custom_logic::entity_revision){reconcile(stored);dirty=false;seen_revision=custom_logic::entity_revision;}}
inline int page_index(const std::string &entity) {
  if(!load())return -1;refresh();
  const int entry=configured(entity);if(entry<0)return -1;
  // An entity may appear on several pages (one entry, one subscription); the
  // page it leads wins.
  for(int p=0;p<stored.count;++p)if(lead(stored.pages[p])==entry)return p;
  for(int p=0;p<stored.count;++p)for(auto e:stored.pages[p].entries)if(e==entry)return p;
  return -1;
}
// Page name/icon for the carousel; empty when the page uses its entity's.
inline const StoredPage *info(const std::string &entity) {
  const int p=page_index(entity);return p<0?nullptr:&stored.pages[p];
}
inline const StoredPage *led_by(const std::string &entity) {
  const auto *p=info(entity);
  return p && entry_entity(lead(*p))==entity?p:nullptr;
}
inline std::vector<int> order() {
  std::vector<int> out;
  if(!load()) {
    for(int e=0;e<int(onx_memory::ENTRY_COUNT);++e)if(!entry_entity(e).empty())out.push_back(int(custom_logic::normal_ui_slot_for_device_config_slot(e)));
    return out;
  }
  refresh();
  for(int p=0;p<stored.count;++p)out.push_back(int(custom_logic::normal_ui_slot_for_device_config_slot(lead(stored.pages[p]))));
  return out;
}
inline const bool order_installed=(onx_entity_config::page_order=&order,true);
inline std::string rpc(const std::string &op,const std::string &data,const std::string &asset={},unsigned offset=0);
inline uint64_t artwork_id(const StoredPages &list,const StoredPage &p,int pos) {
  const auto index=pos<0?p.carousel_artwork:p.artwork[pos]&ART_INDEX;
  return index && index<=list.images.size()?list.images[index-1]:0;
}
inline bool set_artwork(StoredPages &list,StoredPage &p,int pos,uint64_t value) {
  for(size_t i=0;i<list.images.size();++i) {
    if(list.images[i] && list.images[i]!=value)continue;
    list.images[i]=value;(pos<0?p.carousel_artwork:p.artwork[pos])=i+1;return true;
  }
  return false;
}
inline bool bind_carousel_artwork(lv_obj_t *icon,const StoredPage &page) {
  const auto value=artwork_id(stored,page,-1);
  if(!value)return false;
  const auto slot=std::find(onx_navigation::icons.begin(),onx_navigation::icons.end(),icon);
  if(slot==onx_navigation::icons.end())return false;
  static onx_memory::StateArray<onx_panel_artwork::Image,onx_navigation::VISIBLE_ICONS> cache;
  auto &art=cache[slot-onx_navigation::icons.begin()];
  if(!art.load(value))return false;
  lv_image_set_src(icon,art.source);
  lv_image_set_scale(icon,onx_navigation::icon_size()*256/art.source->header.w);
  lv_obj_set_style_image_recolor_opa(icon,LV_OPA_TRANSP,0);
  return true;
}
inline bool resolve_artwork(StoredPages &list,StoredPage &page,int pos,const std::string &ref) {
  if(ref.empty())return true;
  const auto value=onx_panel_artwork::id(ref);
  return value && onx_panel_artwork::read(value,nullptr) && set_artwork(list,page,pos,value);
}
inline void encode(JsonArray out) {
  if(!load())return;refresh();
  for(int p=0;p<stored.count;++p) {
    const auto &page=stored.pages[p];auto item=out.add<JsonObject>();
    item["name"]=std::string(page.name);item["icon"]=std::string(page.icon);
    item["layout"]=onx_panel_layout::name(Kind(page.kind));
    if(page.carousel_artwork)item["carousel_artwork"]=onx_panel_artwork::key(artwork_id(stored,page,-1));
    if(Kind(page.kind)==Kind::ACTION_GRID){item["rows"]=page.rows;item["columns"]=page.cols;}
    auto entities=item["entities"].to<JsonArray>();
    for(int i=0;i<size_of(page);++i)entities.add(entry_entity(page.entries[i]));
    if(std::any_of(page.artwork.begin(),page.artwork.begin()+size_of(page),[](uint8_t i){return (i&ART_INDEX)!=0;})) {
      auto images=item["artwork"].to<JsonArray>();
      for(int i=0;i<size_of(page);++i)images.add(onx_panel_artwork::key(artwork_id(stored,page,i)));
    }
    if(Kind(page.kind)==Kind::ACTION_GRID &&
       std::any_of(page.artwork.begin(),page.artwork.end(),[](uint8_t i){return (i&ICON_ONLY)!=0;})) {
      auto flags=item["icon_only"].to<JsonArray>();
      for(int i=0;i<size_of(page);++i)flags.add((page.artwork[i]&ICON_ONLY)!=0);
    }
    if(Kind(page.kind)==Kind::ACTION_GRID) {
      JsonObject animation;
      for(int i=0;i<size_of(page);++i) {
        const auto fx=effect_of(page.entries[i]);if(!fx)continue;
        if(animation.isNull())animation=item["animation"].to<JsonObject>();
        animation[entry_entity(page.entries[i])]=effect_name(fx);
      }
    }
  }
}
// A preset's pages, or {"pages":[]} for one page per entity. Invalid lists
// (e.g. an entry failed to save) fall back to one page per entity.
inline void apply(const std::string &json) {
  dirty=true;
  const auto result=rpc("panel_pages_save",json.empty()?std::string("{\"pages\":[]}"):json);
  if(result.find("\"error\"")!=std::string::npos)rpc("panel_pages_save","{\"pages\":[]}");
}
inline const bool presets_installed=(onx_presets::encode_pages=&encode,onx_presets::apply_pages=&apply,true);
inline constexpr char ENTITY_ICON[]="entity";
inline bool valid_icon(const std::string &icon) {
  if(icon.empty() || icon==ENTITY_ICON)return true;
  if(icon.size()>64 || icon.rfind("mdi:",0)!=0 || icon.size()<5)return false;
  for(size_t i=4;i<icon.size();++i){const char c=icon[i];if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'))return false;}
  return true;
}
// Reclaim images that no saved panel page or preset uses. Recent uploads may
// still be in a browser's save transaction.
inline std::array<std::pair<uint64_t,uint32_t>,onx_panel_artwork::LIMIT> recently_uploaded{};
inline void prune_artwork() {
  std::vector<uint64_t> keep;
  auto add=[&keep](uint64_t value){if(value && std::find(keep.begin(),keep.end(),value)==keep.end())keep.push_back(value);};
  if(!load())return;
  for(int i=0;i<stored.count;++i)for(int j=-1;j<size_of(stored.pages[i]);++j)add(artwork_id(stored,stored.pages[i],j));
  const uint32_t now=esphome::millis();
  for(const auto &[value,when]:recently_uploaded)if(now-when<120000)add(value);
  onx_presets::load();
  for(const auto &preset:onx_presets::summaries) {
    onx_presets::Blob raw;
    if(!onx_presets::store_read(onx_presets::key_name(preset.key),raw))return;
    auto doc=custom_logic::make_psram_json_document();
    if(deserializeJson(doc,raw.c_str(),raw.size()))return;
    for(JsonObjectConst page:doc["pages"].as<JsonArrayConst>()) {
      if(const char *key=page["carousel_artwork"].as<const char *>())add(onx_panel_artwork::id(key));
      for(const char *key:page["artwork"].as<JsonArrayConst>())if(key)add(onx_panel_artwork::id(key));
    }
  }
  onx_panel_artwork::remove_unused([&keep](uint64_t value){return std::find(keep.begin(),keep.end(),value)!=keep.end();});
}
inline std::string artwork_rpc(const std::string &op,const std::string &data,const std::string &asset,unsigned offset) {
  using namespace onx_panel_artwork;
  using Buffer=std::array<uint8_t,BYTES>;
  if(op=="panel_art_get") {
    auto pixels=custom_logic::make_ram_allocated<Buffer>();
    if(!pixels)return R"({"error":"Could not allocate image storage"})";
    const size_t size=read(id(data),pixels->data(),pixels->size());
    if(offset>=size || offset%CHUNK)return R"({"error":"Image is unavailable"})";
    const size_t length=std::min(CHUNK,size-offset);
    custom_logic::PsramString encoded(encoded_size(length)+1,'\0');
    size_t written=0;
    if(mbedtls_base64_encode(reinterpret_cast<uint8_t *>(encoded.data()),encoded.size(),&written,pixels->data()+offset,length)!=0)
      return R"({"error":"Could not encode image"})";
    encoded.resize(written);
    return onx_entity_config::build_json([&](JsonObject root){
      root["data"]=encoded.c_str();root["next"]=offset+length;root["total"]=size;
    });
  }
  struct Upload {
    custom_logic::RamAllocatedPtr<Buffer> pixels;
    uint64_t id{0};
    size_t next{0};
    uint32_t touched{0};
  };
  static Upload upload;
  const auto expected=id(asset);
  const bool legacy=asset.empty();
  if((!legacy && !expected) || (legacy && offset) || offset>=BYTES || offset%CHUNK)
    return R"({"error":"Invalid image upload offset"})";
  const size_t chunk=legacy?LEGACY_BYTES:std::min(CHUNK,BYTES-offset);
  if(data.size()!=encoded_size(chunk))return R"({"error":"Invalid square image"})";
  const uint32_t received=esphome::millis();
  if(!offset) {
    upload.pixels=custom_logic::make_ram_allocated<Buffer>();
    upload.id=expected;upload.next=0;upload.touched=received;
  }
  if(!upload.pixels || upload.id!=expected || upload.next!=offset || received-upload.touched>30000)
    return R"({"error":"Image upload expired. Save the image again."})";
  upload.touched=received;
  custom_logic::PsramString encoded(data.begin(),data.end());
  std::replace(encoded.begin(),encoded.end(),'-','+');
  std::replace(encoded.begin(),encoded.end(),'_','/');
  size_t written=0;
  auto &pixels=*upload.pixels;
  if(mbedtls_base64_decode(pixels.data()+offset,chunk,&written,reinterpret_cast<const uint8_t *>(encoded.data()),encoded.size())!=0 || written!=chunk)
    return R"({"error":"Invalid square image"})";
  upload.next+=written;
  const size_t size=legacy?LEGACY_BYTES:BYTES;
  if(upload.next<size)return "{\"next\":"+std::to_string(upload.next)+"}";
  auto complete=std::move(upload.pixels);
  const auto value=hash(complete->data(),size);
  if(expected && expected!=value)return R"({"error":"Image checksum differs from the selected crop"})";
  auto existing=custom_logic::make_ram_allocated<Buffer>();
  if(!existing)return R"({"error":"Could not allocate image storage"})";
  const size_t existing_size=read(value,existing->data(),existing->size());
  if(existing_size) {
    if(existing_size!=size || !std::equal(complete->begin(),complete->begin()+size,existing->begin()))
      return R"({"error":"Image conflicts with one already stored"})";
  } else {
    if(count()>=LIMIT || !write(value,complete->data(),size)) {
      prune_artwork();
      if(count()>=LIMIT || !write(value,complete->data(),size))
        return R"({"error":"Panel image storage is full"})";
    }
  }
  const auto now=esphome::millis();
  auto oldest=recently_uploaded.begin();
  for(auto it=recently_uploaded.begin();it!=recently_uploaded.end();++it) {
    if(it->first==value || !it->first){oldest=it;break;}
    if(now-it->second>now-oldest->second)oldest=it;
  }
  *oldest={value,now};
  return "{\"id\":\""+key(value)+"\"}";
}
inline std::string rpc(const std::string &op,const std::string &data,const std::string &asset,unsigned offset) {
  if(op=="panel_art_get" || op=="panel_art_put")return artwork_rpc(op,data,asset,offset);
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
    auto &next=*next_buffer;
    StoredEffects next_effects{};
    for(JsonObjectConst item:pages) {
      StoredPage page{};Kind kind;
      const std::string name=item["name"] | "",icon=item["icon"] | "";
      if(!onx_panel_layout::parse(item["layout"] | "",kind))return "{\"error\":\"Choose a layout for every page\"}";
      if(name.size()>48)return "{\"error\":\"Page names are limited to 48 bytes\"}";
      if(!valid_icon(icon))return "{\"error\":\"Icons use the mdi:name form\"}";
      page.kind=uint8_t(kind);
      if(kind==Kind::ACTION_GRID) {
        const int rows=item["rows"] | 0, cols=item["columns"] | 0;
        if(!onx_panel_layout::grid_axis(rows) || !onx_panel_layout::grid_axis(cols))
          return R"({"error":"Choose 1–4 rows and columns"})";
        page.rows=rows;page.cols=cols;
      }
      if(!resolve_artwork(next,page,-1,item["carousel_artwork"] | ""))
        return R"({"error":"Carousel image is unavailable. Re-upload it before saving."})";
      auto entities=item["entities"].as<JsonArrayConst>();
      auto images=item["artwork"].as<JsonArrayConst>();
      auto flags=item["icon_only"].as<JsonArrayConst>();
      if(!item["icon_only"].isNull() && (kind!=Kind::ACTION_GRID || flags.size()!=entities.size()))
        return R"({"error":"Icon-only settings must match the action grid"})";
      if(!images.isNull() && images.size()!=entities.size())return R"({"error":"Image positions must match the layout"})";
      if(entities.size()!=size_t(size_of(page)))return R"({"error":"The number of positions must match the layout"})";
      for(int i=0;i<size_of(page);++i) {
        const std::string entity=entities[i] | "";const int entry=configured(entity);
        if(entity.empty() && kind==Kind::ACTION_GRID)continue;
        if(entry<0)return "{\"error\":\"Save the page entities before the pages\"}";
        // Reuse is fine (same entry, same subscription), but a carousel stop
        // needs one leading entity and a page shows an entity once.
        for(int j=0;j<i;++j)if(page.entries[j]==entry)return "{\"error\":\"A page shows each entity once\"}";
        if(kind==Kind::CONTROL_SENSORS && i>0 && !onx_panel_layout::sensor(entity))return "{\"error\":\"Compact positions show sensors and binary sensors\"}";
        page.entries[i]=uint8_t(entry);
        if(!resolve_artwork(next,page,i,images[i] | ""))
          return R"({"error":"Page image is unavailable. Re-upload it before saving."})";
        if(flags[i] | false)page.artwork[i]|=ICON_ONLY;
      }
      const auto first=lead(page);
      if(first==NO_ENTRY)return R"({"error":"Place at least one action on each grid"})";
      if(!item["animation"].isNull()) {
        if(kind!=Kind::ACTION_GRID || !item["animation"].is<JsonObjectConst>())return R"({"error":"Icon animations are set on action grids"})";
        for(JsonPairConst fx:item["animation"].as<JsonObjectConst>()) {
          const int entry=configured(fx.key().c_str()),value=effect_parse(fx.value() | "");
          if(value<0)return R"({"error":"Choose none, pulse, blink or spin"})";
          if(entry>=0 && entry<int(onx_memory::ENTRY_COUNT))next_effects.fx[entry]=uint8_t(value);
        }
      }
      for(int p=0;p<next.count;++p)
        if(lead(next.pages[p])==first)return R"({"error":"An entity can lead only one page"})";
      snprintf(page.name,sizeof(page.name),"%s",name.c_str());snprintf(page.icon,sizeof(page.icon),"%s",icon.c_str());
      next.pages[next.count++]=page;
    }
    reconcile(next);
    if(!preference.save(&next) || !effects_preference.save(&next_effects) || !esphome::global_preferences->sync())
      return "{\"error\":\"Could not save pages\"}";
    stored=next;effects=next_effects;
  }
  reconcile(stored);
  return onx_entity_config::build_json([](JsonObject root) {
    encode(root["pages"].to<JsonArray>());
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
inline lv_obj_t *carousel_clock=nullptr;

inline void hide_carousel_clock() {
  if (carousel_clock) lv_obj_add_flag(carousel_clock, LV_OBJ_FLAG_HIDDEN);
}

inline void refresh_carousel_clock(int width) {
  const time_t now = std::time(nullptr);
  if (now <= 1600000000) {
    if (carousel_clock) lv_obj_add_flag(carousel_clock, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  using namespace onx_panel_detail;
  const auto *font = fonts.header_clock != nullptr ? fonts.header_clock : fonts.small;
  const auto value = clock(now);
  if (!carousel_clock) {
    carousel_clock = onx_panel_detail::text(layer, value, font, TEXT(), P(16),
                                            (onx_panel_layout::MAIN_TOP - line(font)) / 2, width / 4);
  } else {
    onx_render::label(carousel_clock, value.c_str());
    onx_render::style_text_color(carousel_clock, lv_color_hex(TEXT()), 0);
    lv_obj_remove_flag(carousel_clock, LV_OBJ_FLAG_HIDDEN);
  }
}

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
inline std::array<onx_panel_layout::Rect,MAX_CELLS> rects{};
inline std::array<std::string,MAX_CELLS> entities{};
inline int count=0;
inline bool showing=false,detail_view=false;
inline std::string parent_entity, page_lead;
inline bool action_grid=false;
inline std::function<void(const std::string &)> run_grid_action;
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
// Home Assistant "Page" select: one unique title per stored page (its name,
// or its first entity's title), and the page that shows a given entry.
inline std::vector<std::string> page_titles() {
  std::vector<std::string> out;
  for(int p=0;p<stored.count && p<int(MAX_PAGES);++p) {
    const auto &page=stored.pages[p];
    std::string title=page.name;
    if(title.empty()){const auto e=entry_entity(lead(page));title=custom_logic::ha_entity_title_from_registry_cache(e);if(title.empty())title=e;}
    if(title.empty())title="Page "+std::to_string(p+1);
    if(title.size()>60)title.resize(60);
    std::string unique=title;
    for(int n=2;std::find(out.begin(),out.end(),unique)!=out.end();++n)unique=title+" ("+std::to_string(n)+")";
    out.push_back(unique);
  }
  return out;
}
inline int page_of_entry(int entry) {
  if(entry<0)return -1;
  for(int p=0;p<stored.count && p<int(MAX_PAGES);++p)for(auto e:stored.pages[p].entries)if(e==entry)return p;
  return -1;
}
inline void hide() {
  hide_carousel_clock();
  onx_panel_detail::leave_main_page();
  onx_panel_detail::hide();
  showing=false;if(layer)lv_obj_add_flag(layer,LV_OBJ_FLAG_HIDDEN);
  if(suspended_page)restore_underlay();
}
inline void label(lv_obj_t *obj,const std::string &text,int x,int y,int w,const lv_font_t *font) {
  onx_render::style_text_font(obj,font,0);onx_render::label(obj,text.c_str());
  lv_obj_set_pos(obj,x,y);lv_obj_set_width(obj,w);
  if(text.empty())lv_obj_add_flag(obj,LV_OBJ_FLAG_HIDDEN);else lv_obj_remove_flag(obj,LV_OBJ_FLAG_HIDDEN);
}
inline std::vector<onx_panel_detail::Tile> main_tiles(const std::string &entity, const StoredPage *p, Kind kind,
                                                    const std::array<onx_panel_layout::Rect,MAX_CELLS> &areas) {
  std::vector<onx_panel_detail::Tile> tiles;
  const int cells = p ? size_of(*p) : 1;
  tiles.reserve(cells);
  for (int i = 0; i < cells; ++i) {
    const auto cell = p ? entry_entity(p->entries[i]) : entity;
    const auto v = cell.empty() ? Item{} : item(cell, false);
    const auto &r = areas[i];
    onx_panel_detail::Tile t;
    t.entity=cell;t.name=onx_media::display_text(v.name);t.value=v.value;t.context=v.context;
    t.on=v.on;t.ring=v.ring;t.r={r.x,r.y,r.w,r.h};t.compact=kind==Kind::CONTROL_SENSORS && i>0;
    t.key=kind==Kind::ACTION_GRID;
    t.icon_only=p && t.key && (p->artwork[i]&ICON_ONLY);
    t.artwork=p?artwork_id(stored,*p,i):0;
    t.effect=p && t.key ? effect_of(p->entries[i]) : 0;
    tiles.push_back(std::move(t));
  }
  return tiles;
}

inline onx_panel_detail::Tile settings_tile(int width, int height) {
  const auto r = onx_panel_layout::geometry(Kind::SINGLE, width, height)[0];
  onx_panel_detail::Tile t;
  t.entity = onx_panel_detail::SETTINGS;
  t.name = "Settings";
  t.r = {r.x, r.y, r.w, r.h};
  return t;
}

template<typename Bind> void render(const std::string &entity,int width,int height,bool detail,Bind bind) {
  if(!layer)return;
  if (detail) {
    hide_carousel_clock();
    onx_panel_detail::leave_main_page();
  }
  if (detail) suspend_underlay();
  else if (suspended_page) restore_underlay();
  onx_render::style_bg_color(layer,lv_color_hex(onx_display_settings::background()),0);
  if(detail || page_lead!=entity)onx_panel_detail::animated_key.clear();
  const auto *p=detail?nullptr:led_by(entity);
  const auto kind=p?Kind(p->kind):Kind::SINGLE;
  action_grid=kind==Kind::ACTION_GRID;
  page_lead=entity;
  rects=onx_panel_layout::geometry(kind,width,height,detail,p?p->rows:2,p?p->cols:2);
  count=p?size_of(*p):1;
  for(int i=0;i<count;++i)entities[i]=p?entry_entity(p->entries[i]):entity;
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
  if (!detail) onx_panel_detail::activate_main_page(entity);
  onx_panel_detail::render_main(layer, main_tiles(entity, p, kind, rects), width, height);
  if (!detail) refresh_carousel_clock(width);
}
// The carousel's Settings item, on the same base as the other main pages.
inline void render_settings(int width,int height) {
  if(!layer)return;
  if (suspended_page) restore_underlay();
  onx_render::style_bg_color(layer,lv_color_hex(onx_display_settings::background()),0);
  for(auto &c:cards){lv_obj_add_flag(c.box,LV_OBJ_FLAG_HIDDEN);c.initialized=false;}
  rects=onx_panel_layout::geometry(Kind::SINGLE,width,height,false);
  action_grid=false;
  entities[0]=onx_panel_detail::SETTINGS;count=1;
  lv_obj_set_size(layer,width,height);lv_obj_set_pos(layer,0,0);
  if(lv_obj_get_index(layer)!=0)lv_obj_move_background(layer);
  lv_obj_remove_flag(layer,LV_OBJ_FLAG_HIDDEN);showing=true;detail_view=false;
  onx_panel_detail::activate_main_page(onx_panel_detail::SETTINGS);
  onx_panel_detail::render_main(layer, {settings_tile(width, height)}, width, height);
  refresh_carousel_clock(width);
}
// The panel's own Assist timers: full screen, above the dock.
inline void show_timers(int width,int height) {
  if(!layer)return;
  hide_carousel_clock();
  onx_panel_detail::leave_main_page();
  suspend_underlay();
  onx_render::style_bg_color(layer,lv_color_hex(onx_display_settings::background()),0);
  for(auto &c:cards){lv_obj_add_flag(c.box,LV_OBJ_FLAG_HIDDEN);c.initialized=false;}
  lv_obj_set_size(layer,width,height);lv_obj_set_pos(layer,0,0);
  if(lv_obj_get_index(layer)!=int32_t(lv_obj_get_child_count(lv_obj_get_parent(layer)))-1)lv_obj_move_foreground(layer);
  lv_obj_remove_flag(layer,LV_OBJ_FLAG_HIDDEN);showing=true;detail_view=true;
  onx_panel_detail::render_timers(layer,width,height);
}
inline bool tap(int x,int y,bool held=false) {
  if(held && (!showing || detail_view || !action_grid))return false;
  if(showing && detail_view)return onx_panel_detail::active;  // Only its own controls act.
  if(!showing)return false;
  // Settings: the shared press action opens the menu.
  if(entities[0]==onx_panel_detail::SETTINGS)return false;
  for(int i=0;i<count;++i) {
    const auto r=rects[i];if(x>=r.x && x<r.x+r.w && y>=r.y && y<r.y+r.h) {
      const auto &entity=entities[i];
      if(configured(entity)<0)return true;
      onx_panel_ui::request_render();
      if(action_grid && !held) {
        if(run_grid_action){onx_panel_detail::animate_key(entity);run_grid_action(entity);}
        return true;
      }
      if(open){parent_entity=page_lead;open(entity);}
      return true;
    }
  }
  return action_grid && y>=onx_panel_layout::MAIN_TOP && y<onx_panel_ui::height-52;
}
}
