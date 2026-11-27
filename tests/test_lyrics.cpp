#include "../onx_lyrics.h"
#include <cassert>
int main() {
  using namespace onx_lyrics;
  assert(!set("media_player.a", "T", "plain words\nno tags") && count() == 0);
  assert(set("media_player.a", "T", "[ar:x]\r\n[00:01.50] one \r\n[00:04]two\n[00:07.123][00:20.00]three\n[00:10.00]\n"));
  assert(count() == 5 && available("media_player.a", "T") && !available("media_player.b", "T") && !available("media_player.a", "U"));
  assert(index_at(0) == -1 && line(index_at(1.6f)) == "one" && line(index_at(4)) == "two");
  assert(line(index_at(8)) == "three" && line(index_at(12)).empty() && line(index_at(25)) == "three");
  assert(shown("media_player.a", "T")); toggle(); assert(!shown("media_player.a", "T"));
  const auto v = store().version;
  assert(set("media_player.a", "T2", "[01:00.0]x") && store().version != v && shown("media_player.a", "T2") && index_at(59.9f) == -1);
  // Live offset: positive shows lines earlier; the lyrics stay.
  set_offset(500); assert(index_at(59.6f) == 0 && count() == 1);
  set_offset(-1000); assert(index_at(60.5f) == -1 && index_at(61.0f) == 0);
  set_offset(0);
  // Mid-song requests: once per track, retried while the request cannot be sent.
  int sent = 0; bool online = false;
  request_hook = [&](const std::string &) { if (!online) return false; ++sent; return true; };
  request("media_player.b", "S"); assert(sent == 0);
  online = true; request("media_player.b", "S"); request("media_player.b", "S"); assert(sent == 1);
  request("media_player.b", "S2"); assert(sent == 2);
  clear(); assert(count() == 0 && !available("media_player.a", "T2"));
}
