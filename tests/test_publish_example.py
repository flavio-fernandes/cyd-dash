"""The sample publisher gives every included page believable values."""
import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import cyd_build as build
import publish_example as pub


class PublishExampleTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(); self.addCleanup(self.tmp.cleanup)
        root = Path(self.tmp.name)
        self.site = root / 'site.yaml'
        self.site.write_text('substitutions: {usage_topic: test/usage}\n'
                             'cyd: {pages: [history, thermostat, usage, detail, hello, device]}\n')
        self.bind = root / 'bindings.yaml'
        self.bind.write_text(''.join((ROOT / 'firmware' / f'bindings{part}.example.yaml').read_text()
                                     for part in ['', '.thermostat', '.history']))

    def test_every_bound_page_gets_values(self):
        plan = build.plan(self.site, self.bind)
        sent = dict(pub.messages(plan, step=0, now=100000))
        self.assertEqual(sorted(sent), ['home/livingroom/climate', 'home/livingroom/history',
                                        'home/livingroom/temperature', 'test/usage'])
        usage = json.loads(sent['test/usage'])
        self.assertEqual(len(usage['gauges']), 2)
        for gauge in usage['gauges']:
            self.assertTrue(0 <= gauge['used_pct'] <= 100)

    def test_thermostat_values_look_like_a_room(self):
        plan = build.plan(self.site, self.bind)
        for step in range(0, 200, 7):
            climate = json.loads(dict(pub.messages(plan, step=step, now=100000))['home/livingroom/climate'])
            self.assertTrue(18 <= climate['temperature'] <= 25, climate)
            self.assertTrue(30 <= climate['humidity'] <= 70, climate)
            self.assertEqual(climate['updated_at'], 100000)

    def test_stat_values_look_like_a_home(self):
        site = Path(self.tmp.name) / 'stat-site.yaml'
        site.write_text('cyd: {pages: [stat]}\n')
        bind = ROOT / 'firmware' / 'bindings.stat.example.yaml'
        plan = build.plan(site, bind)
        for step in range(0, 200, 7):
            power = json.loads(dict(pub.messages(plan, step=step, now=100000))['home/energy/power'])
            self.assertTrue(500 <= power['watts'] <= 3200, power)
            self.assertEqual(power['updated_at'], 100000)

    def test_pages_without_inputs_are_named(self):
        plan = build.plan(self.site, self.bind)
        quiet = sorted(page['name'] for page in plan['pages'] if not page['slots'] and not page['requires'])
        self.assertEqual(quiet, ['device', 'hello'])


if __name__ == '__main__':
    unittest.main()
