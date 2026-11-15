// Reference-counted HA subscriptions: shared rows, release, deferred compaction,
// live-session announcements and re-entrant dispatch, against a model of the
// ESPHome 2026.10 APIServer subscription table.
#define ONX_SUBSCRIPTIONS_TEST
#define USE_API_HOMEASSISTANT_STATES
#include <cassert>
#include <cstdio>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace esphome {
struct StringRef {
  std::string value;
  StringRef() = default;
  explicit StringRef(const char *s) : value(s) {}
  explicit StringRef(const std::string &s) : value(s) {}
  std::string str() const { return value; }
};
template<typename T> using optional = std::optional<T>;
namespace api {
struct SubscribeHomeAssistantStateResponse {
  StringRef entity_id, attribute;
  bool once{false};
};
struct Client {
  std::vector<SubscribeHomeAssistantStateResponse> sent;
  size_t room{1000};
  bool is_connection_setup() { return true; }
  bool send_message(const SubscribeHomeAssistantStateResponse &message) {
    if (room == 0) return false;
    --room;
    sent.push_back(message);
    return true;
  }
};
struct APIServer {
  struct HomeAssistantStateSubscription {
    const char *entity_id;
    const char *attribute;
    std::function<void(StringRef)> callback;
    bool once;
    std::unique_ptr<std::string> entity_id_dynamic_storage;
    std::unique_ptr<std::string> attribute_dynamic_storage;
  };
  std::vector<HomeAssistantStateSubscription> state_subs_;
  std::vector<std::unique_ptr<Client>> clients;
  bool subscribed{false};
  void subscribe_home_assistant_state(std::string entity_id, optional<std::string> attribute,
                                      std::function<void(StringRef)> &&f) {
    HomeAssistantStateSubscription sub;
    sub.entity_id_dynamic_storage = std::make_unique<std::string>(std::move(entity_id));
    sub.entity_id = sub.entity_id_dynamic_storage->c_str();
    if (attribute.has_value()) {
      sub.attribute_dynamic_storage = std::make_unique<std::string>(std::move(*attribute));
      sub.attribute = sub.attribute_dynamic_storage->c_str();
    } else {
      sub.attribute = nullptr;
    }
    sub.callback = std::move(f);
    sub.once = false;
    state_subs_.push_back(std::move(sub));
  }
  const std::vector<HomeAssistantStateSubscription> &get_state_subs() const { return state_subs_; }
  const std::vector<std::unique_ptr<Client>> &active_clients() const { return clients; }
  bool is_connected_with_state_subscription() const { return subscribed; }
  // Mirrors APIConnection::on_home_assistant_state_response.
  void deliver(const std::string &entity, const std::string &attribute, const std::string &state) {
    for (auto &it : state_subs_) {
      if (entity != it.entity_id) continue;
      if (it.attribute != nullptr ? attribute != it.attribute : !attribute.empty()) continue;
      it.callback(StringRef(state));
    }
  }
};
}  // namespace api
}  // namespace esphome

#include "../onx_subscriptions.h"

using onx_subs::Kind;
using onx_subs::owner;
using onx_subs::Registry;
using onx_subs::Result;

static size_t rows_for(const esphome::api::APIServer &api, const std::string &entity) {
  size_t count = 0;
  for (const auto &row : api.state_subs_)
    if (entity == row.entity_id) ++count;
  return count;
}

int main() {
  // Shared pairs, fan-out and release without HA connected.
  {
    esphome::api::APIServer api;
    Registry reg;
    std::string a, b;
    const auto s0 = owner(Kind::PRIMARY, 0), s1 = owner(Kind::PRIMARY, 1);
    assert(reg.acquire(&api, s0, "light.desk", "", [&](esphome::StringRef v) { a = v.str(); }) == Result::ADDED);
    assert(reg.acquire(&api, s0, "light.desk", "", [&](esphome::StringRef) { a = "dup"; }) == Result::EXISTING);
    assert(reg.acquire(&api, s1, "light.desk", "", [&](esphome::StringRef v) { b = v.str(); }) == Result::ADDED);
    assert(rows_for(api, "light.desk") == 1);
    api.deliver("light.desk", "", "on");
    assert(a == "on" && b == "on");

    // Another component's row must survive compaction.
    api.subscribe_home_assistant_state("sun.sun", std::nullopt, [](esphome::StringRef) {});

    reg.release(s0);
    api.deliver("light.desk", "", "off");
    assert(a == "on" && b == "off");
    reg.release(s1);
    reg.tick(&api, 1000);
    assert(rows_for(api, "light.desk") == 1);  // Debounced.
    // Revived before compaction: same row, no new subscription.
    assert(reg.acquire(&api, s1, "light.desk", "", [&](esphome::StringRef v) { b = "revived:" + v.str(); }) == Result::ADDED);
    assert(rows_for(api, "light.desk") == 1);
    api.deliver("light.desk", "", "on");
    assert(b == "revived:on");
    reg.release(s1);
    reg.tick(&api, 2000);
    reg.tick(&api, 5500);
    assert(rows_for(api, "light.desk") == 0);
    assert(rows_for(api, "sun.sun") == 1);
    assert(reg.entry_count() == 0);
    api.deliver("light.desk", "", "on");  // No listener, no crash.
  }

  // keep_entity: replacing a slot keeps listeners on the entity it still shows.
  {
    esphome::api::APIServer api;
    Registry reg;
    const auto s = owner(Kind::PRIMARY, 3);
    for (const char *attr : {"", "brightness"}) reg.acquire(&api, s, "light.a", attr, [](esphome::StringRef) {});
    reg.acquire(&api, s, "sensor.old", "", [](esphome::StringRef) {});
    const std::string keep = "light.a";
    reg.release(s, &keep);
    assert(reg.owns(s, "light.a") && !reg.owns(s, "sensor.old"));
    reg.tick(&api, 10000);
    assert(rows_for(api, "light.a") == 2 && rows_for(api, "sensor.old") == 0);
  }

  // Live HA session: announcements, settle delay, stale pairs.
  {
    esphome::api::APIServer api;
    api.clients.push_back(std::make_unique<esphome::api::Client>());
    auto &client = *api.clients[0];
    api.subscribed = true;
    Registry reg;
    reg.tick(&api, 0);  // Session starts.
    const auto s0 = owner(Kind::PRIMARY, 0), s1 = owner(Kind::SECONDARY, 0);
    reg.acquire(&api, s0, "sensor.t", "", [](esphome::StringRef) {});
    reg.acquire(&api, s0, "sensor.t", "unit_of_measurement", [](esphome::StringRef) {});
    assert(client.sent.empty());  // Sent from the main-loop tick.
    reg.tick(&api, 100);
    assert(client.sent.size() == 2 && !client.sent[0].once && client.sent[0].entity_id.value == "sensor.t");
    // A second owner of a tracked pair needs the current value only.
    reg.acquire(&api, s1, "sensor.t", "", [](esphome::StringRef) {});
    reg.tick(&api, 200);
    assert(client.sent.size() == 3 && client.sent[2].once);

    // Transport backpressure keeps the queue for the next tick.
    for (int i = 0; i < 10; ++i)
      reg.acquire(&api, owner(Kind::PRIMARY, 5), "sensor.n" + std::to_string(i), "", [](esphome::StringRef) {});
    client.room = 2;
    reg.tick(&api, 300);
    assert(client.sent.size() == 5);
    client.room = 1000;
    reg.tick(&api, 400);
    assert(client.sent.size() == 5 + onx_subs::ANNOUNCE_BUDGET);
    reg.tick(&api, 500);
    assert(client.sent.size() == 13);

    // Released while the session replays: compaction waits for it to settle.
    reg.release(s0);
    reg.release(s1);
    reg.tick(&api, 4000);
    assert(rows_for(api, "sensor.t") == 2);
    reg.tick(&api, 10000);
    assert(rows_for(api, "sensor.t") == 0);
    assert(reg.stale_count() == 2);  // HA still forwards them.
    // Re-acquired in the same session: new row, but only a value request.
    const auto before = client.sent.size();
    reg.acquire(&api, s0, "sensor.t", "", [](esphome::StringRef) {});
    reg.tick(&api, 10100);
    assert(rows_for(api, "sensor.t") == 1 && client.sent.size() == before + 1 && client.sent.back().once);
    assert(reg.stale_count() == 1);
    // A new connection forgets stale pairs and announcements.
    api.subscribed = false;
    reg.tick(&api, 10200);
    assert(reg.stale_count() == 0);
  }

  // Too many stale pairs advise one reconnect; compact_now ignores the debounce.
  {
    esphome::api::APIServer api;
    api.clients.push_back(std::make_unique<esphome::api::Client>());
    api.subscribed = true;
    Registry reg;
    reg.tick(&api, 0);
    const auto s = owner(Kind::PRIMARY, 9);
    for (size_t i = 0; i < onx_subs::STALE_RECONNECT_LIMIT; ++i)
      reg.acquire(&api, s, "sensor.x" + std::to_string(i), "", [](esphome::StringRef) {});
    reg.tick(&api, 100);
    reg.release(s);
    reg.compact_now(&api);
    assert(api.state_subs_.empty());
    assert(reg.tick(&api, 20000));
    assert(!reg.tick(&api, 20100));
  }

  // Re-entrant dispatch: a callback that replaces its own subscription.
  {
    esphome::api::APIServer api;
    Registry reg;
    const auto s = owner(Kind::GESTURE, 2);
    int hits = 0, next_hits = 0;
    reg.acquire(&api, s, "input_number.a", "", [&](esphome::StringRef) {
      ++hits;
      reg.release(s);
      reg.acquire(&api, s, "input_number.b", "", [&](esphome::StringRef) { ++next_hits; });
    });
    api.deliver("input_number.a", "", "1");
    assert(hits == 1 && api.state_subs_.size() == 1);  // Deferred while ESPHome iterates.
    api.deliver("input_number.a", "", "2");
    assert(hits == 1);  // Released listener is skipped.
    reg.tick(&api, 100);
    assert(rows_for(api, "input_number.b") == 1);
    api.deliver("input_number.b", "", "3");
    assert(next_hits == 1);
    reg.tick(&api, 5000);
    assert(rows_for(api, "input_number.a") == 0 && reg.entry_count() == 1);
  }

  std::puts("Subscriptions passed: shared rows, release, revive, compaction, live announcements, backpressure, stale reconnect, re-entrancy");
}
