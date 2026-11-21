#pragma once
#include <array>
#include <cstddef>
#include <new>
#include <type_traits>

#ifdef USE_ESP32
#include <cstdlib>
#include <new>
#include "esp_heap_caps.h"
#include "esphome/core/log.h"
#endif

namespace onx_memory {
// Configuration entries (page-placed entities). Per-slot runtime state is
// allocated lazily in PSRAM chunks (PagedArray) so unused slots cost 0 bytes.
constexpr std::size_t ENTRY_COUNT = 250;
#ifdef USE_ESP32
// Large per-entity records belong in the board's PSRAM. Allocate on first use,
// after PSRAM initialization, rather than consuming internal .bss at boot.
// These containers are initialized and accessed by the main UI/API loop.
template<typename T, size_t N> class StateArray {
 public:
  StateArray() = default;
  StateArray(const StateArray &) = delete;
  StateArray &operator=(const StateArray &) = delete;
  ~StateArray() {
    if (storage_ != nullptr) {
      storage_->~Storage();
      heap_caps_free(storage_);
    }
  }
  constexpr size_t size() const { return N; }
  T &operator[](size_t index) { return storage()[index]; }
  const T &operator[](size_t index) const { return storage()[index]; }
  auto begin() { return storage().begin(); }
  auto end() { return storage().end(); }
  auto begin() const { return storage().cbegin(); }
  auto end() const { return storage().cend(); }

 private:
  using Storage = std::array<T, N>;
  Storage &storage() const {
    if (storage_ == nullptr) {
      void *memory = heap_caps_malloc(sizeof(Storage), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      if (memory == nullptr) {
        ESP_LOGE("onx_memory", "Cannot allocate %zu bytes of entity state in PSRAM", sizeof(Storage));
        std::abort();
      }
      storage_ = new (memory) Storage{};
    }
    return *storage_;
  }
  mutable Storage *storage_{nullptr};
};
#else
template<typename T, size_t N> using StateArray = std::array<T, N>;
#endif

// Fixed-capacity, lazily-paged PSRAM container: allocates small chunks on first
// access so 20 configured entities consume a fraction of the 250-entry ceiling,
// while pointers/references to elements remain stable for the lifetime of the
// container and internal SRAM usage stays near zero.
template<typename T, size_t N, size_t PageSize = 16> class PagedArray {
 public:
  static constexpr size_t PAGE_SIZE = PageSize;
  static constexpr size_t PAGE_COUNT = (N + PageSize - 1) / PageSize;

  PagedArray() = default;
  PagedArray(const PagedArray &) = delete;
  PagedArray &operator=(const PagedArray &) = delete;
  ~PagedArray() {
    for (auto *page : pages_) {
      if (page == nullptr) continue;
      page->~Page();
#ifdef USE_ESP32
      heap_caps_free(page);
#else
      ::operator delete(page);
#endif
    }
  }

  constexpr size_t size() const { return N; }
  bool has(size_t index) const { return index < N && pages_[index / PageSize] != nullptr; }
  size_t allocated_limit() const { return allocated_limit_; }
  size_t allocated_pages() const {
    size_t count = 0;
    for (const auto *page : pages_) if (page != nullptr) ++count;
    return count;
  }

  T &operator[](size_t index) { return (*ensure_page(index / PageSize))[index % PageSize]; }
  const T &operator[](size_t index) const {
    if (index < N && pages_[index / PageSize] != nullptr) return (*pages_[index / PageSize])[index % PageSize];
    static const T empty{};
    return empty;
  }
  T *find(size_t index) {
    return index < N && pages_[index / PageSize] != nullptr ? &(*pages_[index / PageSize])[index % PageSize] : nullptr;
  }
  const T *find(size_t index) const {
    return index < N && pages_[index / PageSize] != nullptr ? &(*pages_[index / PageSize])[index % PageSize] : nullptr;
  }
  void fill(const T &value) {
    for (size_t p = 0; p < PAGE_COUNT; ++p)
      if (pages_[p] != nullptr) pages_[p]->fill(value);
  }

  // Sparse forward iterator over allocated pages only (never allocates new pages).
  template<bool Const> class Iter {
   public:
    using Owner = const PagedArray *;
    using Ref = typename std::conditional<Const, const T &, T &>::type;
    using Ptr = typename std::conditional<Const, const T *, T *>::type;
    Iter(Owner owner, size_t page, size_t offset) : owner_(owner), page_(page), offset_(offset) { skip(); }
    Ref operator*() const { return (*owner_->pages_[page_])[offset_]; }
    Ptr operator->() const { return &(*owner_->pages_[page_])[offset_]; }
    Iter &operator++() {
      ++offset_;
      skip();
      return *this;
    }
    bool operator==(const Iter &o) const { return page_ == o.page_ && offset_ == o.offset_; }
    bool operator!=(const Iter &o) const { return !(*this == o); }

   private:
    static constexpr size_t page_len(size_t p) {
      return (N - p * PageSize) < PageSize ? (N - p * PageSize) : PageSize;
    }
    void skip() {
      while (page_ < PAGE_COUNT && (owner_->pages_[page_] == nullptr || offset_ >= page_len(page_))) {
        ++page_;
        offset_ = 0;
      }
    }
    Owner owner_;
    size_t page_;
    size_t offset_;
  };
  Iter<false> begin() { return Iter<false>(this, 0, 0); }
  Iter<false> end() { return Iter<false>(this, PAGE_COUNT, 0); }
  Iter<true> begin() const { return Iter<true>(this, 0, 0); }
  Iter<true> end() const { return Iter<true>(this, PAGE_COUNT, 0); }

 private:
  using Page = std::array<T, PageSize>;
  Page *ensure_page(size_t page_index) const {
    if (pages_[page_index] == nullptr) {
#ifdef USE_ESP32
      void *memory = heap_caps_malloc(sizeof(Page), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      if (memory == nullptr) {
        ESP_LOGE("onx_memory", "Cannot allocate %zu bytes of paged entity state in PSRAM", sizeof(Page));
        std::abort();
      }
#else
      void *memory = ::operator new(sizeof(Page));
#endif
      pages_[page_index] = new (memory) Page{};
      const size_t end = (page_index + 1) * PageSize;
      if (end > allocated_limit_) allocated_limit_ = end < N ? end : N;
    }
    return pages_[page_index];
  }
  mutable std::array<Page *, PAGE_COUNT> pages_{};
  mutable size_t allocated_limit_{0};
};

// One value-initialized object in PSRAM for large tables that would otherwise
// sit in internal .bss/.data (docs/room-companion/memory-budget.md). Bind it to a
// reference at namespace or function scope; PSRAM is ready during static
// initialization. The object lives for the whole program.
template<typename T> T &psram_object() {
#ifdef USE_ESP32
  if (void *memory = heap_caps_malloc(sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT))
    return *new (memory) T{};
#endif
  return *new T{};
}
}  // namespace onx_memory
