// Room Companion integration URLs (onx_companion.h). Host build: no NVS.
#include "../onx_companion.h"
#include <cassert>
int main() {
  using namespace onx_companion;
  assert(base().empty() && icon_url("mdi:lamp", 80).empty() && cover_url("media_player.a", "/p", 96, 96).empty());
  assert(!set("http://ha:8123/other") && !set("ftp://ha/api/room_companion/x") && !set("http://ha/api/room_companion/a b"));
  assert(set("http://ha:8123/api/room_companion/tok/"));
  assert(base() == "http://ha:8123/api/room_companion/tok");
  assert(icon_url("mdi:weather-sunny", 80) == "http://ha:8123/api/room_companion/tok/icon/weather-sunny.svg?size=80");
  assert(icon_url("lamp", 80).empty());
  assert(emoji_url("1f600") == "http://ha:8123/api/room_companion/tok/emoji/1f600.svg");
  const auto c1 = cover_url("media_player.a", "/api/media_player_proxy/a?token=1", 320, 320);
  const auto c2 = cover_url("media_player.a", "/api/media_player_proxy/a?token=2", 320, 320);
  assert(c1.rfind("http://ha:8123/api/room_companion/tok/cover/media_player.a?w=320&h=320&fmt=jpeg&v=", 0) == 0 && c1 != c2);
  assert(cover_url("media_player.a", "/p", 10, 20, "&mode=blur").find("&mode=blur&v=") != std::string::npos);
  assert(cover_url("camera.a", "/p", 96, 96).empty() && cover_url("media_player.A", "/p", 96, 96).empty());
  assert(cover_url("media_player.a", "None", 96, 96).empty());
  assert(owns(c1) && !owns("http://other/x"));
  fail(); assert(base().empty() && cover_url("media_player.a", "/p", 96, 96).empty() && owns(c1));
  assert(set("http://ha:8123/api/room_companion/tok") && !base().empty());  // A new announcement clears the failure.
  assert(set("") && base().empty());
}
