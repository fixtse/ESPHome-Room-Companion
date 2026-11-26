"""Check that the config page's save filter accepts every domain the firmware does."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]


def regex_domains(source, marker):
    start = source.index(marker)
    match = re.search(r'\^\(((?:[a-z_]+\|)+[a-z_]+)\)', source[start:])
    return set(match.group(1).split('|'))


class EntityDomains(unittest.TestCase):
    def test_web_filters_match_firmware(self):
        logic = (ROOT / 'custom_logic.h').read_text()
        start = logic.index('inline bool is_supported_normal_device_entity_id(const char *entity_id) {')
        body = logic[start:logic.index('\n}', start)]
        firmware = {d for d, n in re.findall(r'strncmp\(entity_id, "([a-z_]+)\.", (\d+)\)', body)}
        for d, n in re.findall(r'strncmp\(entity_id, "([a-z_]+)\.", (\d+)\)', body):
            self.assertEqual(len(d) + 1, int(n), d)
        app = (ROOT / 'my_components/onx_config_web/config_app.h').read_text()
        browser = (ROOT / 'my_components/onx_config_web/ha_browser.h').read_text()
        for name, domains in {
            'isSupported': regex_domains(app, 'function isSupported('),
            'isManualEntity': regex_domains(app, 'function isManualEntity('),
            'ha_browser': regex_domains(browser, 'const supported='),
        }.items():
            self.assertEqual(domains, firmware, name)


if __name__ == '__main__':
    unittest.main()
