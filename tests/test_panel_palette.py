"""Exercise the production colour mapper across saved background changes."""
from pathlib import Path
import subprocess
import tempfile
import unittest

class PanelPalette(unittest.TestCase):
    def test_page_colours_survive_background_changes(self):
        root=Path(__file__).resolve().parents[1]
        source=(root/'onx_render.h').read_text()
        mapper=source[source.index('struct PalettePair'):source.index('inline uint32_t panel_property_color')]
        harness='''#include "onx_display_settings.h"
#include <array>
#include <algorithm>
#include <cassert>
namespace onx_render {
MAPPER
}
int main(){
 using namespace onx_render;
 remember_panel_colors();
 const auto old_page=panel_colors();
 for(unsigned i=0;i<12;++i){
  onx_display_settings::background_settings.color=(i%2)?0xF4F4F4:0x14283D;
  remember_panel_colors();
  const auto now=panel_colors();
  assert(now.bg!=now.text && now.surface!=now.bg);
  assert(panel_color(now.bg,false)==now.bg);
  assert(panel_color(now.text,true)==now.text);
  panel_source=&old_page;
  assert(panel_color(old_page.bg,false)==now.bg);
  assert(panel_color(old_page.text,true)==now.text);
  panel_source=nullptr;
  auto colour=panel_color(0x1A1A1A,false);
  for(int repeat=0;repeat<5;++repeat)assert(panel_color(colour,false)==colour);
  assert(panel_color(0xF97316,false)==0xF97316); // Accent is preserved.
 }
}
'''.replace('MAPPER',mapper)
        with tempfile.TemporaryDirectory() as directory:
            cpp=Path(directory)/'palette.cpp';binary=Path(directory)/'palette'
            cpp.write_text(harness)
            subprocess.run(['c++','-std=c++17','-I'+str(root),str(cpp),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)

if __name__=='__main__':unittest.main()
