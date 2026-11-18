"""Keep the rectangular carousel independent of the retired page renderer."""
from pathlib import Path
import re
import unittest

class CarouselParenting(unittest.TestCase):
    def test_single_native_host_has_no_legacy_body_widgets(self):
        root = Path(__file__).resolve().parents[1]
        source = (root / 'packages/features/normal_mode/normal_mode_pages.yaml').read_text()
        blocks = re.split(r'\n    - id: ', source.split('  pages:', 1)[1])
        host = next(block for block in blocks if block.startswith('normal_panel_page\n'))
        self.assertNotIn('widgets:', host)
        self.assertNotRegex(source, r'id: device_(?:[0-9]+|settings)\b')
        self.assertNotRegex(source, r'id: normal_device_(?:[0-9]+|settings)_(?:title|value|context|icon|pagination)')

    def test_shared_carousel_updates_navigation_without_legacy_rendering(self):
        root = Path(__file__).resolve().parents[1]
        source = (root / 'packages/features/normal_mode/normal_mode_scripts.yaml').read_text()
        refresh = source.split('  - id: refresh_normal_device_page\n', 1)[1].split('  - id:', 1)[0]
        self.assertIn('onx_entity_config::carousel_selection', refresh)
        self.assertIn('onx_navigation::render', refresh)
        self.assertNotIn('render_entry', refresh)
        self.assertNotRegex(source, r'id\(device_[0-9]+\)')

if __name__ == '__main__': unittest.main()
