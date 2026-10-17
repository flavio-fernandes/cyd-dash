import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import cyd_build as build
import publish_example as pub


class HistoryPublishTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(); self.addCleanup(self.tmp.cleanup)
        root = Path(self.tmp.name)
        self.site = root / 'site.yaml'; self.site.write_text('cyd: {pages: [history]}\n')
        self.bind = root / 'bindings.yaml'
        self.bind.write_text('history:\n  value: {topic: test/history/value}\n'
                             '  seed: {topic: test/history/seed}\n  label: {value: Living room}\n')

    def test_seed_once_then_a_slowly_changing_value(self):
        plan = build.plan(self.site, self.bind)
        first = dict(pub.messages(plan, step=0, now=100000))
        seed = json.loads(first['test/history/seed'])
        self.assertEqual(len(seed), 120)
        self.assertTrue(all(18 < v < 25 for v in seed))
        self.assertEqual(seed[-1], float(first['test/history/value']))
        # Neighbouring minutes differ only a little: a realistic, slowly changing series.
        self.assertLess(max(abs(a - b) for a, b in zip(seed, seed[1:])), 0.3)
        later = dict(pub.messages(plan, step=1, now=100005))
        self.assertNotIn('test/history/seed', later)
        self.assertLess(abs(float(later['test/history/value']) - seed[-1]), 0.2)


if __name__ == '__main__':
    unittest.main()
