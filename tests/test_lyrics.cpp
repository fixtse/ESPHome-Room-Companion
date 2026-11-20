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
  clear(); assert(count() == 0 && !available("media_player.a", "T2"));
}
