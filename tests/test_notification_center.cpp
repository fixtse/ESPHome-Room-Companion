#include "../onx_notification.h"
#include <cassert>
int main() {
  onx_notification::Center center;
  auto first=center.add("Abraham","Amigo",60,"https://example.com/avatar",100);
  center.mark_read(first);
  center.add("Other user","Hello",60,"https://example.com/other",200);
  auto latest=center.add("Abraham","Amigo\nentiendo. Voy",60,"https://example.com/avatar",300);
  assert(center.items.size()==2);
  assert(center.items.front().id==latest && center.find(first)==nullptr);
  assert(center.items.front().body=="Amigo\nentiendo. Voy");
  assert(center.items.front().received_ms==300 && !center.items.front().read);
  assert(center.toast_id==latest && center.unread()==2);
  center.add("Abraham","Different pictured sender",60,"https://example.com/different",400);
  assert(center.items.size()==3);
  center.add("Text sender","First",60,"",500);
  center.add("Text sender","Latest",60,"",600);
  assert(center.items.size()==4 && center.items.front().body=="Latest");
  center.add("","Untitled one",60,"",700);
  center.add("","Untitled two",60,"",800);
  assert(center.items.size()==6);
  assert(center.dismiss(latest));
  assert(center.find(latest)==nullptr);
}
