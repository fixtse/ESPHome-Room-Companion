#pragma once
// Reference-counted Home Assistant state subscriptions.
//
// ESPHome's APIServer only appends to its subscription table: every
// subscribe_home_assistant_state() call keeps its strings and callback until the
// device reboots. Changing a slot, a sensor secondary, a gesture feedback entity
// or the standby weather entity therefore used to leak the previous entries, and
// their callbacks kept running for entities that were no longer shown.
//
// This registry owns one APIServer entry per unique (entity, attribute) pair and
// fans each update out to the listeners that currently need it. Listeners are
// grouped by owner (a slot role), so replacing a slot releases exactly what that
// slot used. Unused entries are removed from the APIServer table from the main
// loop once the Native API connection is settled, which returns their memory
// without a reboot. Entries are revived instead of re-created when a released
// pair is needed again before compaction (for example while a preset switches
// several slots in a row).
//
// Home Assistant has no "unsubscribe" message. A pair added while HA is connected
// is announced to the live connection immediately, so new controls work without
// a reconnect. HA keeps forwarding released pairs until the connection ends; the
// device ignores them, and a bounded number of them triggers a reconnect that
// drops HA's trackers too.
#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#ifndef ONX_SUBSCRIPTIONS_TEST
#include "onx_runtime_diagnostics.h"
#define ONX_SUBS_TRACE(name, entity) onx_runtime::record(name, entity)
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/core/string_ref.h"
#include "esphome/components/api/api_server.h"
#endif
#ifndef ONX_SUBS_TRACE
#define ONX_SUBS_TRACE(name, entity) ((void) 0)
#endif

namespace onx_subs {
// Bumped on Home Assistant state updates for entities the screen shows (any
// attribute), so the panel redraws at once. `shown` is set by the panel; other
// entities are picked up by its once-a-second refresh.
inline uint32_t state_revision = 0;
inline uint32_t any_revision = 0;  // Every update, shown or not (main-page backstop key).
inline bool (*shown)(const std::string &entity) = nullptr;

enum class Kind : uint8_t { PRIMARY = 1, SECONDARY = 2, GESTURE = 3, WEATHER = 4 };
using Owner = uint16_t;
constexpr Owner NO_OWNER = 0;
constexpr Owner owner(Kind kind, size_t slot) { return static_cast<Owner>((static_cast<uint16_t>(kind) << 8) | (slot & 0xFF)); }

using Callback = std::function<void(esphome::StringRef)>;

struct Listener {
  Owner owner{NO_OWNER};
  Callback callback;
};

struct Entry {
  std::string entity, attribute;
  std::vector<Listener> listeners;
  // Identity of this pair's APIServer row: the heap string that row owns.
  const char *token{nullptr};
  uint32_t idle_since_ms{0};
  bool live() const {
    for (const auto &listener : listeners)
      if (listener.owner != NO_OWNER) return true;
    return false;
  }
};

struct Pending {
  Owner owner;
  std::string entity, attribute;
  Callback callback;
};

struct Announcement {
  std::string entity, attribute;
  bool once;
};

enum class Result : uint8_t { REJECTED, ADDED, EXISTING };

// Debounce before unused rows are erased, so a preset that moves an entity from
// one slot to another reuses the row instead of re-announcing it to HA.
constexpr uint32_t COMPACT_IDLE_MS = 3000;
// A new HA connection replays the table by index; do not erase rows under it.
constexpr uint32_t REPLAY_SETTLE_MS = 10000;
// Released pairs HA still forwards before a reconnect is requested to drop them.
constexpr size_t STALE_RECONNECT_LIMIT = 48;
// Live-session announcements sent per tick; the rest wait for transport room.
constexpr size_t ANNOUNCE_BUDGET = 6;

struct Stats {
  size_t entries{0}, listeners{0}, idle{0}, api_rows{0}, stale{0};
  uint32_t released{0}, compacted{0};
};

class Registry {
 public:
  template<typename Api>
  Result acquire(Api *api, Owner who, const std::string &entity, const std::string &attribute, Callback callback) {
    if (who == NO_OWNER || entity.empty() || !callback) return Result::REJECTED;
    if (this->dispatching_ > 0) {
      // ESPHome is iterating its subscription table; appending to it now would
      // invalidate that iteration. Register from the next tick instead.
      for (const auto &pending : this->pending_)
        if (pending.owner == who && pending.entity == entity && pending.attribute == attribute) return Result::EXISTING;
      this->pending_.push_back(Pending{who, entity, attribute, std::move(callback)});
      return Result::ADDED;
    }
    Entry *entry = this->find_(entity, attribute);
    bool created = false;
    if (entry == nullptr) {
      this->entries_.push_back(std::make_unique<Entry>());
      entry = this->entries_.back().get();
      entry->entity = entity;
      entry->attribute = attribute;
      created = true;
    }
    for (auto &listener : entry->listeners) {
      if (listener.owner == who) return Result::EXISTING;
    }
    entry->listeners.push_back(Listener{who, std::move(callback)});
    if (created || entry->token == nullptr) {
      this->register_(api, *entry);
    } else {
      // HA already forwards this pair, but its last value went to other owners.
      this->announce_(entity, attribute, true);
    }
    return Result::ADDED;
  }

  // Releases every listener of an owner, or only those not on keep_entity.
  void release(Owner who, const std::string *keep_entity = nullptr) {
    for (auto it = this->pending_.begin(); it != this->pending_.end();) {
      if (it->owner == who && (keep_entity == nullptr || it->entity != *keep_entity)) it = this->pending_.erase(it);
      else ++it;
    }
    for (auto &entry : this->entries_) {
      if (keep_entity != nullptr && entry->entity == *keep_entity) continue;
      auto &listeners = entry->listeners;
      bool changed = false;
      if (this->dispatching_ > 0) {
        // A callback may be running from this vector; mark only.
        for (auto &listener : listeners)
          if (listener.owner == who) { listener.owner = NO_OWNER; changed = true; }
      } else {
        const auto before = listeners.size();
        listeners.erase(std::remove_if(listeners.begin(), listeners.end(),
                                       [who](const Listener &l) { return l.owner == who || l.owner == NO_OWNER; }),
                        listeners.end());
        changed = listeners.size() != before;
        if (listeners.empty()) std::vector<Listener>().swap(listeners);
      }
      if (changed) {
        ++this->stats_.released;
        if (!entry->live()) { entry->idle_since_ms = this->now_; this->dirty_ = true; }
      }
    }
  }

  bool owns(Owner who, const std::string &entity) const {
    for (const auto &entry : this->entries_) {
      if (entry->entity != entity) continue;
      for (const auto &listener : entry->listeners)
        if (listener.owner == who) return true;
    }
    for (const auto &pending : this->pending_)
      if (pending.owner == who && pending.entity == entity) return true;
    return false;
  }

  void dispatch(Entry &entry, esphome::StringRef value) {
    ++any_revision;
    if (shown == nullptr || shown(entry.entity)) ++state_revision;  // Redraw only for what is on screen.
    ++this->dispatching_;
    // Index loop: the vector is not resized while dispatching_ > 0.
    for (size_t i = 0; i < entry.listeners.size(); ++i) {
      auto &listener = entry.listeners[i];
      if (listener.owner != NO_OWNER && listener.callback) listener.callback(value);
    }
    --this->dispatching_;
  }

  // Ask the live HA session for the current value of a pair (sent from tick()).
  void refresh(const std::string &entity, const std::string &attribute) { this->announce_(entity, attribute, true); }

  // Main-loop housekeeping: flush deferred registrations, track the HA session
  // and erase rows nobody listens to. Returns true when a reconnect is advised
  // to drop pairs HA still forwards.
  template<typename Api> bool tick(Api *api, uint32_t now) {
    this->now_ = now;
    if (this->dispatching_ > 0) return false;
    bool subscribed = false;
    uintptr_t fingerprint = 0;
    if (api != nullptr) {
      for (const auto &client : api->active_clients()) {
        if (client == nullptr) continue;
        fingerprint = fingerprint * 31 + reinterpret_cast<uintptr_t>(&*client);
      }
      subscribed = api->is_connected_with_state_subscription();
    }
    if (fingerprint != this->fingerprint_ || subscribed != this->subscribed_) {
      // A new or lost session: the next replay covers every row that exists, and
      // HA no longer tracks anything from the previous connection.
      this->fingerprint_ = fingerprint;
      this->subscribed_ = subscribed;
      this->session_since_ms_ = now;
      std::vector<std::string>().swap(this->stale_);
      std::vector<Announcement>().swap(this->announce_queue_);
    }
    if (!this->pending_.empty()) {
      auto pending = std::move(this->pending_);
      this->pending_.clear();
      for (auto &item : pending) this->acquire(api, item.owner, item.entity, item.attribute, std::move(item.callback));
    }
    if (subscribed && api != nullptr) this->flush_announcements_(api);
    if (this->dirty_) {
      const bool settled = !subscribed || now - this->session_since_ms_ >= REPLAY_SETTLE_MS;
      if (settled) this->compact_(api, now, false);
    }
    if (this->stale_.size() >= STALE_RECONNECT_LIMIT && !this->reconnect_requested_) {
      this->reconnect_requested_ = true;
      return true;
    }
    if (!subscribed) this->reconnect_requested_ = false;
    return false;
  }

  // Erase every idle row now. Use right before the HA connection is dropped,
  // when a replay in progress no longer matters.
  template<typename Api> void compact_now(Api *api) { this->compact_(api, this->now_, true); }

  // Re-register rows whose APIServer entry was never created (API unavailable).
  template<typename Api> void ensure_registered(Api *api) {
    if (this->dispatching_ > 0) return;
    for (auto &entry : this->entries_)
      if (entry->token == nullptr && entry->live()) this->register_(api, *entry);
  }

  template<typename Api> Stats stats(Api *api) const {
    Stats out = this->stats_;
    out.entries = this->entries_.size();
    out.stale = this->stale_.size();
    for (const auto &entry : this->entries_) {
      if (!entry->live()) ++out.idle;
      for (const auto &listener : entry->listeners)
        if (listener.owner != NO_OWNER) ++out.listeners;
    }
    if (api != nullptr) out.api_rows = api->get_state_subs().size();
    return out;
  }

  size_t entry_count() const { return this->entries_.size(); }
  size_t stale_count() const { return this->stale_.size(); }

 protected:
  Entry *find_(const std::string &entity, const std::string &attribute) {
    for (auto &entry : this->entries_)
      if (entry->entity == entity && entry->attribute == attribute) return entry.get();
    return nullptr;
  }

  bool forget_stale_(const std::string &entity, const std::string &attribute) {
    const auto key = entity + '\n' + attribute;
    auto it = std::find(this->stale_.begin(), this->stale_.end(), key);
    if (it == this->stale_.end()) return false;
    this->stale_.erase(it);
    return true;
  }

  template<typename Api> void register_(Api *api, Entry &entry) {
#ifdef USE_API_HOMEASSISTANT_STATES
    if (api == nullptr) return;
    ONX_SUBS_TRACE("subs_register", entry.entity);
    Entry *target = &entry;
    Registry *self = this;
    api->subscribe_home_assistant_state(entry.entity, esphome::optional<std::string>(entry.attribute),
                                        Callback([self, target](esphome::StringRef value) { self->dispatch(*target, value); }));
    entry.token = api->get_state_subs().back().entity_id;
    // Announce the pair to the live session, as a new connection would. When HA
    // still tracks it from earlier in this connection, only fetch the value.
    const bool was_stale = this->forget_stale_(entry.entity, entry.attribute);
    this->announce_(entry.entity, entry.attribute, was_stale);
#endif
  }

  void announce_(const std::string &entity, const std::string &attribute, bool once) {
    if (!this->subscribed_) return;  // The next connection replays the table.
    for (auto &item : this->announce_queue_) {
      if (item.entity == entity && item.attribute == attribute) { item.once = item.once && once; return; }
    }
    this->announce_queue_.push_back(Announcement{entity, attribute, once});
  }

  template<typename Api> void flush_announcements_(Api *api) {
#ifdef USE_API_HOMEASSISTANT_STATES
    size_t sent = 0;
    while (!this->announce_queue_.empty() && sent < ANNOUNCE_BUDGET) {
      const auto &item = this->announce_queue_.front();
      ONX_SUBS_TRACE("subs_announce", item.entity);
      esphome::api::SubscribeHomeAssistantStateResponse request;
      request.entity_id = esphome::StringRef(item.entity);
      request.attribute = esphome::StringRef(item.attribute);
      request.once = item.once;
      bool delivered = false;
      for (const auto &client : api->active_clients())
        if (client != nullptr && client->is_connection_setup()) delivered = client->send_message(request) || delivered;
      if (!delivered) return;  // Transport is full; retry on the next tick.
      this->announce_queue_.erase(this->announce_queue_.begin());
      ++sent;
    }
    if (this->announce_queue_.empty() && this->announce_queue_.capacity() > 16) {
      std::vector<Announcement>().swap(this->announce_queue_);
    }
#endif
  }

  template<typename Api> void compact_(Api *api, uint32_t now, bool force) {
    ONX_SUBS_TRACE("subs_compact", std::string());
    std::vector<const char *> tokens;
    bool still_dirty = false;
    for (auto it = this->entries_.begin(); it != this->entries_.end();) {
      Entry &entry = **it;
      if (entry.live()) {
        // Drop listeners marked during a dispatch.
        entry.listeners.erase(std::remove_if(entry.listeners.begin(), entry.listeners.end(),
                                             [](const Listener &l) { return l.owner == NO_OWNER; }),
                              entry.listeners.end());
        ++it;
        continue;
      }
      if (!force && now - entry.idle_since_ms < COMPACT_IDLE_MS) { still_dirty = true; ++it; continue; }
      auto &queue = this->announce_queue_;
      queue.erase(std::remove_if(queue.begin(), queue.end(),
                                 [&entry](const Announcement &a) {
                                   return a.entity == entry.entity && a.attribute == entry.attribute;
                                 }),
                  queue.end());
      if (entry.token != nullptr) {
        if (api == nullptr) { still_dirty = true; ++it; continue; }
        tokens.push_back(entry.token);
        if (this->subscribed_ && this->stale_.size() < 4 * STALE_RECONNECT_LIMIT)
          this->stale_.push_back(entry.entity + '\n' + entry.attribute);
      }
      it = this->entries_.erase(it);
      ++this->stats_.compacted;
    }
    if (!tokens.empty()) this->erase_rows_(api, tokens);
    if (this->entries_.capacity() > 2 * this->entries_.size() + 16) this->entries_.shrink_to_fit();
    this->dirty_ = still_dirty;
  }

  template<typename Api> void erase_rows_(Api *api, const std::vector<const char *> &tokens) {
#ifdef USE_API_HOMEASSISTANT_STATES
    // APIServer exposes its table read-only; the vector itself is a mutable
    // member, so modifying it here is well-defined. This runs from the main loop,
    // never while ESPHome iterates the table.
    ONX_SUBS_TRACE("subs_erase", std::string());
    using Rows = typename std::remove_const<typename std::remove_reference<decltype(api->get_state_subs())>::type>::type;
    auto &rows = const_cast<Rows &>(api->get_state_subs());
    rows.erase(std::remove_if(rows.begin(), rows.end(),
                              [&tokens](const auto &row) {
                                return std::find(tokens.begin(), tokens.end(), row.entity_id) != tokens.end();
                              }),
               rows.end());
    if (rows.capacity() > rows.size() + 32) rows.shrink_to_fit();
#endif
  }

  std::vector<std::unique_ptr<Entry>> entries_;
  std::vector<Pending> pending_;
  std::vector<std::string> stale_;
  std::vector<Announcement> announce_queue_;
  Stats stats_;
  uint32_t now_{0}, session_since_ms_{0};
  uintptr_t fingerprint_{0};
  int dispatching_{0};
  bool dirty_{false}, subscribed_{false}, reconnect_requested_{false};
};

inline Registry registry;

}  // namespace onx_subs
