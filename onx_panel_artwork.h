#pragma once
#include "onx_memory.h"
#include "lvgl.h"
#include "mbedtls/base64.h"
#include "nvs.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>

namespace onx_panel_artwork {
constexpr int LEGACY_SIDE=40, SIDE=96;
constexpr size_t LEGACY_BYTES=LEGACY_SIDE*LEGACY_SIDE*2, BYTES=SIDE*SIDE*2;
constexpr size_t CHUNK=4800;
inline bool valid_size(size_t size) { return size==LEGACY_BYTES || size==BYTES; }
inline size_t encoded_size(size_t size) { return (size+2)/3*4; }
constexpr size_t LIMIT=40;
constexpr char NAMESPACE[]="rc_art";

inline std::string key(uint64_t id) {
  if(!id)return {};
  char out[16];
  snprintf(out,sizeof(out),"%014llx",static_cast<unsigned long long>(id));
  return out;
}
inline uint64_t id(const std::string &key) {
  if(key.size()!=14)return 0;
  uint64_t value=0;
  for(const char c:key) {
    const int digit=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;
    if(digit<0)return 0;
    value=(value<<4)|digit;
  }
  return value;
}
inline uint64_t hash(const uint8_t *data,size_t size) {
  uint64_t value=14695981039346656037ULL;
  for(size_t i=0;i<size;++i)value=(value^data[i])*1099511628211ULL;
  return (value & 0x00FFFFFFFFFFFFFFULL) | 1;
}
inline size_t read(uint64_t id,uint8_t *data,size_t capacity=0) {
  if(!id)return false;
  nvs_handle_t handle;
  if(nvs_open(NAMESPACE,NVS_READONLY,&handle)!=ESP_OK)return false;
  size_t size=capacity;
  const bool ok=nvs_get_blob(handle,key(id).c_str(),data,&size)==ESP_OK && valid_size(size);
  nvs_close(handle);
  return ok?size:0;
}
inline size_t count() {
  size_t count=0;
  nvs_iterator_t it=nullptr;
  for(auto status=nvs_entry_find("nvs",NAMESPACE,NVS_TYPE_BLOB,&it);status==ESP_OK;status=nvs_entry_next(&it))++count;
  nvs_release_iterator(it);
  return count;
}
inline void remove_unused(const std::function<bool(uint64_t)> &keep) {
  std::array<uint64_t,LIMIT> removed{};
  size_t count=0;
  nvs_iterator_t it=nullptr;
  for(auto status=nvs_entry_find("nvs",NAMESPACE,NVS_TYPE_BLOB,&it);status==ESP_OK;status=nvs_entry_next(&it)) {
    nvs_entry_info_t info{};
    nvs_entry_info(it,&info);
    const auto value=id(info.key);
    if(value && !keep(value) && count<removed.size())removed[count++]=value;
  }
  nvs_release_iterator(it);
  if(!count)return;
  nvs_handle_t handle;
  if(nvs_open(NAMESPACE,NVS_READWRITE,&handle)!=ESP_OK)return;
  for(size_t i=0;i<count;++i)nvs_erase_key(handle,key(removed[i]).c_str());
  nvs_commit(handle);
  nvs_close(handle);
}
inline bool write(uint64_t value,const uint8_t *data,size_t size) {
  nvs_stats_t stats{};
  // Leave at least 64 KiB of NVS entries available for Wi-Fi, configuration and presets.
  if(nvs_get_stats("nvs",&stats)!=ESP_OK || stats.available_entries < (64*1024+size+4096)/32)return false;
  nvs_handle_t handle;
  if(nvs_open(NAMESPACE,NVS_READWRITE,&handle)!=ESP_OK)return false;
  auto result=nvs_set_blob(handle,key(value).c_str(),data,size);
  if(result==ESP_OK)result=nvs_commit(handle);
  nvs_close(handle);
  return result==ESP_OK;
}

struct PixelDeleter {
  void operator()(uint8_t *data) const { heap_caps_free(data); }
};
using Pixels=std::unique_ptr<uint8_t,PixelDeleter>;
struct Image {
  uint64_t id{0};
  Pixels pixels;
  size_t capacity{0};
  // Keep each resolution at its own descriptor address for LVGL's header cache.
  lv_image_dsc_t descriptors[2]{};
  const lv_image_dsc_t *source{nullptr};
  bool load(uint64_t value) {
    if(id==value && source)return true;
    const size_t size=read(value,nullptr);
    if(!size)return false;
    if(source)lv_image_cache_drop(source);
    source=nullptr;
    id=0;
    if(capacity<size) {
      Pixels larger(static_cast<uint8_t *>(heap_caps_malloc(size,MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
      if(!larger)return false;
      pixels=std::move(larger);
      capacity=size;
    }
    if(read(value,pixels.get(),capacity)!=size)return false;
    const bool large=size==BYTES;
    const int side=large?SIDE:LEGACY_SIDE;
    auto &descriptor=descriptors[large];
    descriptor.header.magic=LV_IMAGE_HEADER_MAGIC;
    descriptor.header.cf=LV_COLOR_FORMAT_RGB565;
    descriptor.header.w=descriptor.header.h=side;
    descriptor.header.stride=side*2;
    descriptor.data_size=size;
    descriptor.data=pixels.get();
    source=&descriptor;
    id=value;
    return true;
  }
};
}  // namespace onx_panel_artwork
