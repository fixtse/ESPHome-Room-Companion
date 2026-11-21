// SPDX-License-Identifier: GPL-3.0-only
#include "online_image.h"
#include "image_limits.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#ifdef USE_RUNTIME_IMAGE_JPEG
#include "esphome/components/runtime_image/jpeg_decoder.h"
#endif
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <memory>
#include <new>

namespace esphome::online_image {
static const char *const TAG="online_image";
static constexpr size_t MAX_BYTES=512*1024;
static constexpr uint32_t WORKER_STACK_BYTES=12*1024;
static constexpr size_t CACHE_MIN_FREE_PSRAM=1536*1024;
#ifdef USE_RUNTIME_IMAGE_JPEG
// JPEGDEC embeds its MCU/Huffman workspaces in the decoder object. Keep that
// large object out of the fragmented internal heap; default delete must match.
class PsramJpegDecoder final : public runtime_image::JpegDecoder {
 public:
  using runtime_image::JpegDecoder::JpegDecoder;
  // ESPHome 2026.11 keeps JPEGDEC in PSRAM and decodes directly to RGB565,
  // so the former grayscale workaround is no longer needed.
  static void *operator new(size_t size,const std::nothrow_t &) noexcept {
    return heap_caps_malloc(size,MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
  static void operator delete(void *p) noexcept { heap_caps_free(p); }
  static void operator delete(void *p,const std::nothrow_t &) noexcept { heap_caps_free(p); }
 private:
  static int draw_grayscale_(JPEGDRAW *block) {
    auto *decoder=static_cast<PsramJpegDecoder *>(block->pUser);
    if(!decoder || block->iBpp!=16)return 0;
    App.feed_wdt();
    // iWidth is the row pitch; iWidthUsed excludes padding on edge blocks.
    for(int y=0;y<block->iHeight;++y)for(int x=0;x<block->iWidthUsed;++x) {
      const uint16_t pixel=block->pPixels[y*block->iWidth+x];
      const uint8_t r=(pixel>>11)&31,g=(pixel>>5)&63,b=pixel&31;
      decoder->draw(block->x+x,block->y+y,1,1,
          Color((r<<3)|(r>>2),(g<<2)|(g>>4),(b<<3)|(b>>2),255));
    }
    return 1;
  }
};
#endif
bool OnlineImage::memory_ready() {
  // The 12 KB worker stack falls back to PSRAM, so only require headroom for
  // lwIP/Wi-Fi. Compressed data, decoded pixels, JPEG workspace and TLS use PSRAM.
  return heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)>=32*1024 &&
      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)>=8*1024;
}
OnlineImage::OnlineImage(const std::string &url,int width,int height,runtime_image::ImageFormat format,
                         image::ImageType type,image::Transparency transparency,image::Image *placeholder,
                         uint32_t buffer_size,bool is_big_endian)
    : RuntimeImage(format,type,transparency,placeholder,is_big_endian,width,height) {
  (void) buffer_size;
  set_url(url);
}
OnlineImage::~OnlineImage() {
  for (auto &entry : cache_) heap_caps_free(entry.data);
  cache_.clear();
  cache_bytes_ = 0;
}
const OnlineImage::CacheEntry *OnlineImage::cache_find_(const std::string &url) {
  for (auto &entry : cache_) {
    if (entry.url == url && entry.data && entry.size > 0) {
      entry.used = ++cache_clock_;
      return &entry;
    }
  }
  return nullptr;
}
void OnlineImage::cache_store_(const std::string &url, const uint8_t *data, size_t size) {
  if (cache_max_entries_ == 0 || !data || size == 0 || size > cache_max_bytes_) return;
  if (heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) < CACHE_MIN_FREE_PSRAM + size) return;
  for (auto &entry : cache_) {
    if (entry.url == url) {
      entry.used = ++cache_clock_;
      return;
    }
  }
  while (!cache_.empty() && (cache_.size() >= cache_max_entries_ || cache_bytes_ + size > cache_max_bytes_)) {
    auto oldest = std::min_element(cache_.begin(), cache_.end(),
                                   [](const CacheEntry &a, const CacheEntry &b) { return a.used < b.used; });
    cache_bytes_ -= oldest->size;
    heap_caps_free(oldest->data);
    cache_.erase(oldest);
  }
  auto *copy = static_cast<uint8_t *>(heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!copy) return;
  std::memcpy(copy, data, size);
  cache_.push_back({url, copy, size, ++cache_clock_});
  cache_bytes_ += size;
}
bool OnlineImage::validate_url_(const std::string &url) {
  return url.size()<=2048 && (url.rfind("http://",0)==0 || url.rfind("https://",0)==0);
}
bool OnlineImage::cancelled_(uint32_t generation) {
  LockGuard lock(mutex_);
  return generation!=generation_;
}
void OnlineImage::update() {
  reap_worker_();
  if(busy_ || is_decoding()) {last_error_="Waiting for artwork downloader";failed_.call();return;}
  if(worker_) {last_error_="Previous artwork worker still exiting";failed_.call();return;}
  last_error_.clear();
  if(!validate_url_(url_)) { last_error_="Invalid artwork URL";failed_.call(); return; }
  const bool is_https=url_.rfind("https://",0)==0;
  std::string host;
  {
    const size_t begin=url_.find("://")+3;
    const size_t end=url_.find_first_of(":/?",begin);
    host=url_.substr(begin,std::min<size_t>(end==std::string::npos?url_.size()-begin:end-begin,64));
  }
  if (const auto *hit = cache_find_(url_)) {
    auto *copy = static_cast<uint8_t *>(heap_caps_malloc(hit->size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (copy) {
      std::memcpy(copy, hit->data, hit->size);
      FetchStats st{};
      st.bytes = hit->size;
      st.https = is_https;
      st.cached = true;
      st.host = host;
      st.heap_free_start = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
      st.heap_largest_start = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
      st.heap_free_after_open = st.heap_free_start;
      busy_ = true;
      {
        LockGuard lock(mutex_);
        completed_generation_ = generation_;
        completed_data_ = copy;
        completed_size_ = hit->size;
        completed_url_ = url_;
        completed_error_.clear();
        completed_stats_ = std::move(st);
        completed_cached_ = true;
        completed_ = true;
      }
      return;
    }
  }
  if(!memory_ready()) {last_error_="Waiting for artwork worker memory";failed_.call();return;}
  const uint32_t open_timeout=is_https?5500:3500;
  const uint32_t read_timeout=2500;
  const uint32_t budget=is_https?10000:7000;
  auto *work=new(std::nothrow) Work{this,url_,{},generation_,open_timeout,read_timeout,budget,allow_redirects_};
  if(!work) { last_error_="Request allocation failed";failed_.call(); return; }
  if(parent_) {
    work->open_timeout_ms=std::min<uint32_t>(work->open_timeout_ms,parent_->get_timeout());
    work->read_timeout_ms=std::min<uint32_t>(work->read_timeout_ms,parent_->get_timeout());
  }
  work->stats.heap_free_start=heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  work->stats.heap_largest_start=heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  work->stats.https=is_https;
  work->stats.host=std::move(host);
  for(auto &header:request_headers_) {
    auto value=header.second.value();
    if(!value.empty()) work->headers.push_back({header.first,std::move(value)});
  }
  busy_=true;
  // All DNS, TCP, TLS, redirects and body reads happen off the ESPHome/UI task.
  // Prefer an internal stack (TLS is slower on a PSRAM stack). Fall back to PSRAM
  // only when internal RAM is too fragmented, where the fetch used to just wait.
  const bool want_internal=
      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)>=WORKER_STACK_BYTES+2048;
  TaskHandle_t handle=nullptr;
  BaseType_t created=pdFAIL;
  work->stats.stack_psram=false;  // set before the task can read it
  if(want_internal)
    created=xTaskCreatePinnedToCoreWithCaps(fetch_task_,"onx_artwork",WORKER_STACK_BYTES,work,1,&handle,tskNO_AFFINITY,
                                            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if(created!=pdPASS) {
    work->stats.stack_psram=true;
    created=xTaskCreatePinnedToCoreWithCaps(fetch_task_,"onx_artwork",WORKER_STACK_BYTES,work,1,&handle,tskNO_AFFINITY,
                                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
  if(created!=pdPASS) {
    busy_=false;delete work;last_error_="Worker allocation failed";failed_.call();
  } else worker_=handle;
}
void OnlineImage::fetch_task_(void *arg) {
  std::unique_ptr<Work> work(static_cast<Work *>(arg));
  uint8_t *data=nullptr;
  size_t used=0,capacity=0;
  std::string error;
  esp_http_client_config_t config{};
  config.url=work->url.c_str();
  config.timeout_ms=work->open_timeout_ms;
  config.crt_bundle_attach=esp_crt_bundle_attach;
  config.disable_auto_redirect=true;
  config.buffer_size=4096;
  config.user_agent="RoomCompanion/artwork";
  auto client=esp_http_client_init(&config);
  if(!client) error="HTTP allocation failed";
  const uint32_t started=millis();
  const uint32_t budget=work->budget_ms;
  auto remaining_ms=[&]() -> uint32_t {
    const uint32_t elapsed=millis()-started;
    return elapsed<budget?budget-elapsed:0;
  };
  bool okay=false;
  if(client) {
    esp_http_client_set_header(client,"Accept","image/jpeg,image/png,image/bmp");
    esp_http_client_set_header(client,"Accept-Encoding","identity");
    for(const auto &header:work->headers) esp_http_client_set_header(client,header.name.c_str(),header.value.c_str());
    bool opened=false;
    int64_t length=-1;
    for(unsigned redirects=0;redirects<=3;++redirects) {
      if(work->owner->cancelled_(work->generation) || remaining_ms()==0) { error="Cancelled or timed out";break; }
      // Retry once only on a fast transient connect failure (e.g. DNS/SYN hiccup);
      // never stack slow timeouts into a 20+ s hang.
      esp_err_t opened_status=ESP_FAIL;
      for(unsigned attempt=0;attempt<2;++attempt) {
        const uint32_t rem=remaining_ms();
        if(work->owner->cancelled_(work->generation) || rem<500) break;
        if(attempt>0) {
          esp_http_client_close(client);
          vTaskDelay(pdMS_TO_TICKS(150));
          if(work->owner->cancelled_(work->generation) || remaining_ms()<2500) break;
        }
        esp_http_client_set_timeout_ms(client,static_cast<int>(std::min(work->open_timeout_ms,remaining_ms())));
        const uint32_t try_start=millis();
        work->stats.attempts++;
        opened_status=esp_http_client_open(client,0);
        if(opened_status==ESP_OK) break;
        // Slow failure (already hit connect/TLS timeout): let outer backoff handle retry.
        if(millis()-try_start>=2000 || remaining_ms()<4500) break;
      }
      work->stats.open_ms=millis()-started;
      work->stats.heap_free_after_open=heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
      if(opened_status!=ESP_OK) {error=std::string("Connection failed: ")+esp_err_to_name(opened_status);break;}
      const uint32_t hdr_rem=remaining_ms();
      if(work->owner->cancelled_(work->generation) || hdr_rem<250) {error="Cancelled or timed out";break;}
      esp_http_client_set_timeout_ms(client,static_cast<int>(std::min(work->read_timeout_ms,hdr_rem)));
      length=esp_http_client_fetch_headers(client);
      if(length<0) {error="Header fetch failed";break;}
      const int status=esp_http_client_get_status_code(client);
      if(status==200) {opened=true;break;}
      if(work->allow_redirects && redirects<3 && (status==301 || status==302 || status==303 || status==307 || status==308)) {
        if(esp_http_client_set_redirection(client)!=ESP_OK) {error="Invalid redirect";break;}
        esp_http_client_close(client);continue;
      }
      error="HTTP "+std::to_string(status);break;
    }
    if(opened) {
      if(length>static_cast<int64_t>(MAX_BYTES)) error="Artwork exceeds 512 KB";
      else {
        capacity=length>0?static_cast<size_t>(length):MAX_BYTES;
        data=static_cast<uint8_t *>(heap_caps_malloc(capacity,MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if(!data) error="Artwork PSRAM allocation failed";
        else {
          uint32_t last_progress_ms=millis();
          size_t since_yield=0;
          unsigned zero_reads=0;
          while(remaining_ms()>0 && !work->owner->cancelled_(work->generation)) {
            if(used==capacity) {
              if(esp_http_client_is_complete_data_received(client)) okay=true;
              else error="Artwork exceeds response bound";
              break;
            }
            const uint32_t rem=remaining_ms();
            esp_http_client_set_timeout_ms(client,static_cast<int>(std::min(work->read_timeout_ms,rem)));
            const int count=esp_http_client_read(client,reinterpret_cast<char *>(data+used),std::min<size_t>(8192,capacity-used));
            if(count<0) {error="Artwork read failed";break;}
            if(count>0) {
              used+=static_cast<size_t>(count);
              since_yield+=static_cast<size_t>(count);
              last_progress_ms=millis();
              zero_reads=0;
              if(esp_http_client_is_complete_data_received(client)) {okay=used>0;break;}
              if(since_yield>=32768) { since_yield=0; vTaskDelay(1); }
            } else {
              if(esp_http_client_is_complete_data_received(client)) {okay=used>0;break;}
              const int err=esp_http_client_get_errno(client);
              ++zero_reads;
              // Abort fast on peer EOF (err==0 repeatedly), socket error, or stalled stream.
              if((err!=0 && err!=EAGAIN && err!=EWOULDBLOCK) ||
                 (err==0 && zero_reads>=3) ||
                 (millis()-last_progress_ms>=work->read_timeout_ms)) {
                error=err==0?"Artwork response truncated":"Artwork read stalled";
                break;
              }
              vTaskDelay(1);
            }
          }
          if(!okay && error.empty()) error="Artwork cancelled or timed out";
          work->stats.read_ms=millis()-started-work->stats.open_ms;
          work->stats.bytes=used;
        }
      }
    }
    // Destroy the connection before handing bytes or any callback to the UI.
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
  }
  if(!okay) {heap_caps_free(data);data=nullptr;used=0;}
  {
    LockGuard lock(work->owner->mutex_);
    work->owner->completed_generation_=work->generation;
    work->owner->completed_data_=data;
    work->owner->completed_size_=used;
    work->owner->completed_url_=std::move(work->url);
    work->owner->completed_error_=std::move(error);
    work->owner->completed_stats_=work->stats;
    work->owner->completed_cached_=false;
    work->owner->completed_=true;
  }
  work.reset();
  // A WithCaps task cannot free its own stack; loop() deletes it once suspended.
  vTaskSuspend(nullptr);
}
void OnlineImage::reap_worker_() {
  if(!worker_) return;
  auto handle=static_cast<TaskHandle_t>(worker_);
  if(eTaskGetState(handle)!=eSuspended) return;
  vTaskDeleteWithCaps(handle);
  worker_=nullptr;
}
void OnlineImage::loop() {
  reap_worker_();
  // Never clear busy_ or fire callbacks while the previous worker stack still exists.
  if(worker_!=nullptr) return;
  uint8_t *data;
  size_t size;
  uint32_t generation;
  std::string url, error;
  bool cached;
  {
    LockGuard lock(mutex_);
    if(!completed_) return;
    data=completed_data_;size=completed_size_;generation=completed_generation_;
    url=std::move(completed_url_);
    error=std::move(completed_error_);
    stats_=completed_stats_;
    cached=completed_cached_;
    completed_data_=nullptr;completed_=false;completed_cached_=false;
  }
  busy_=false;
  if(generation!=generation_) {heap_caps_free(data);return;}
  if(!data) {last_error_=error;ESP_LOGW(TAG,"Artwork unavailable: %s",error.c_str());failed_.call();return;}
  if(!source_pixels_within_limit(data,size,source_pixel_limit_)) {
    heap_caps_free(data);last_error_="Image source dimensions exceed limit or are invalid";
    failed_.call();return;
  }
  runtime_image::ImageFormat format=get_format();
  if(format==runtime_image::AUTO) {
    if(size>=3 && data[0]==0xFF && data[1]==0xD8 && data[2]==0xFF) format=runtime_image::JPEG;
    else if(size>=8 && std::memcmp(data,"\x89PNG\r\n\x1a\n",8)==0) format=runtime_image::PNG;
    else if(size>=2 && data[0]=='B' && data[1]=='M') format=runtime_image::BMP;
  }
#ifdef USE_RUNTIME_IMAGE_JPEG
  if(format==runtime_image::JPEG && (!decoder_ || decoder_->get_format()!=runtime_image::JPEG)) {
    auto *jpeg=new(std::nothrow) PsramJpegDecoder(this);
    if(!jpeg) {
      heap_caps_free(data);last_error_="JPEG workspace PSRAM allocation failed";
      failed_.call();return;
    }
    decoder_.reset(jpeg);
  }
#endif
  bool okay=false;
  if(format!=runtime_image::AUTO && begin_decode(size,format)) {
    size_t offset=0;
    while(offset<size) {
      int consumed=feed_data(data+offset,size-offset);
      if(consumed<=0 || static_cast<size_t>(consumed)>size-offset) break;
      offset+=static_cast<size_t>(consumed);
      App.feed_wdt();
      if(is_decode_finished()) break;
    }
    okay=is_decode_finished() && end_decode();
  }
  if(okay && !cached) cache_store_(url, data, size);
  heap_caps_free(data);
  if(okay) {ESP_LOGI(TAG,"Artwork loaded (%u bytes%s)",static_cast<unsigned>(size),cached?", cached":"");finished_.call(cached);}
  else {last_error_="Artwork decode failed (unsupported or invalid image)";RuntimeImage::release();ESP_LOGW(TAG,"Artwork decode failed");failed_.call();}
}
void OnlineImage::release() {
  {
    LockGuard lock(mutex_);
    ++generation_;
  }
  // Worker keeps its own request snapshot and discards cancelled generations.
  // Only the UI task owns the decoder, pixels, and LVGL descriptors.
  RuntimeImage::release();
}
}
