"""docs/mqtt.md documents every status field the firmware publishes and every command it
accepts, so the MQTT reference cannot drift from the code."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
FIRMWARE = ROOT / 'firmware'
DOC = (ROOT / 'docs' / 'mqtt.md').read_text()


def sources():
    # The core's YAML and headers, and every page's headers. Never the private site files.
    files = sorted((FIRMWARE / 'core').glob('*.yaml')) + sorted((FIRMWARE / 'core').glob('*.h'))
    files += sorted(p for p in (FIRMWARE / 'pages').glob('*/*.h') if not p.parent.name.startswith('_'))
    return '\n'.join(p.read_text() for p in files)


CODE = sources()


def status_fields():
    return set(re.findall(r'root\["([a-z_]+)"\]', CODE))


def nested_fields():
    return set(re.findall(r'root\["[a-z_]+"\]\["([a-z_]+)"\]', CODE))


def commands():
    # x == "next", x.rfind("page ", 0), sscanf(x.c_str(), "interval %d"): the first word of each.
    words = set(re.findall(r'x == "([a-z_]+)', CODE))
    words |= set(re.findall(r'x\.rfind\("([a-z_]+) ', CODE))
    words |= set(re.findall(r'sscanf\(x\.c_str\(\), "([a-z_]+) ', CODE))
    return words


class MqttDocTests(unittest.TestCase):
    def test_the_scan_finds_the_code(self):
        self.assertIn('marker', status_fields())
        self.assertIn('motion', nested_fields())
        self.assertTrue({'next', 'page', 'interval', 'calibrate'} <= commands())

    def test_every_status_field_is_documented(self):
        for field in sorted(status_fields() | nested_fields()):
            with self.subTest(field=field):
                self.assertTrue(re.search(r'`%s`' % re.escape(field), DOC), 'docs/mqtt.md does not mention `%s`' % field)

    def test_every_command_is_documented(self):
        for command in sorted(commands()):
            with self.subTest(command=command):
                self.assertTrue(re.search(r'`%s[` ]' % re.escape(command), DOC), 'docs/mqtt.md does not list `%s`' % command)

    def test_status_is_retained_with_an_offline_message(self):
        mqtt = (FIRMWARE / 'core' / 'mqtt.yaml').read_text()
        self.assertIn('{"online":false}', mqtt)
        self.assertIn('{"online": false}', DOC)


if __name__ == '__main__':
    unittest.main()
