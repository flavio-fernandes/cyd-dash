"""Optional usage inputs: typed bindings and the real shared C++ decisions."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import cyd_build as build
import publish_example as publisher

FIELDS = {'limit_reached': 'bool', 'measured_at': 'epoch', 'old_after': 'number',
          'weekly_cap_pct': 'number', 'weekly_base_cap_pct': 'number',
          'weekly_donated_pct': 'number', 'donated_until': 'epoch'}


class UsageInputTests(unittest.TestCase):
    def test_binding_types_defaults_and_optional_group(self):
        with tempfile.TemporaryDirectory() as d:
            site, bindings = Path(d) / 'site.yaml', Path(d) / 'bindings.yaml'
            site.write_text('cyd: {pages: [usage]}\n')
            base = 'usage:\n  gauge1_label: {value: Plan A}\n  gauge1_used_pct: {value: 38}\n  gauge1_resets_at: {value: 10000}\n'
            bindings.write_text(base)
            slots = {s['slot']: s for s in build.plan(site, bindings)['slots']}
            for n in (1, 2):
                for field, kind in FIELDS.items():
                    self.assertEqual(slots[f'gauge{n}_{field}']['type'], kind)
                    self.assertEqual(slots[f'gauge{n}_{field}']['source'], 'literal' if field == 'old_after' else 'unbound')
            for field in FIELDS:
                with self.subTest(field=field):
                    bindings.write_text(base + f'  gauge1_{field}: {{value: invalid}}\n')
                    with self.assertRaises(build.BuildError):
                        build.plan(site, bindings)
                    bindings.write_text(base + f'  gauge2_{field}: {{topic: home/example/usage}}\n')
                    with self.assertRaisesRegex(build.BuildError, 'optional group'):
                        build.plan(site, bindings)
                    bindings.write_text(base + f'  gauge1_{field}: {{topic: home/example/usage, json: "gauges[0].{field}"}}\n')
                    build.plan(site, bindings)

    def test_publisher_new_inputs(self):
        for name, kind in FIELDS.items():
            slot = {'page': 'usage', 'slot': 'gauge1_' + name, 'type': kind}
            value = publisher.sample(slot, 0, 1000)
            if name == 'measured_at': self.assertEqual(value, 1000)
            if name == 'old_after': self.assertEqual(value, 1800)
            if name == 'limit_reached': self.assertIs(value, False)
            if name == 'donated_until': self.assertGreater(value, 1000)

    def test_cpp_measurement_expiry_local_day_and_weekly_colour(self):
        source = (ROOT / 'firmware/pages/usage/usage.h').read_text()
        colour = source[source.index('inline uint32_t fill_colour('):source.index('inline float urgency(')]
        code = '''#include <cassert>
#include <cstdlib>
#include "model.h"
constexpr uint32_t GREY=1, GREEN=2, AMBER=3, RED=4;
''' + colour + r'''
int main() {
  assert(!cyd_measurement_old(-1,1800,10000));
  assert(!cyd_measurement_old(10000,1800,-1));
  assert(!cyd_measurement_old(10000,1800,9999));
  assert(!cyd_measurement_old(10000,1800,11800));
  assert(cyd_measurement_old(10000,1800,11801));
  assert(cyd_measurement_age(10000,17500)=="2h05 old");
  assert(cyd_measurement_age(10000,12100)=="35m old");
  assert(cyd_measurement_age(10000,13600)=="1h00 old");
  CydWindow w; w.used=70; w.cap=80; w.base=60; w.donated=20;
  assert(fill_colour(63.9,80)==GREEN && fill_colour(64,80)==AMBER);
  assert(fill_colour(w.used,w.cap)==AMBER && cyd_donated(w));
  assert(fill_colour(80,80)==RED && fill_colour(61,NAN)==GREEN);
  cyd_expire_donation(w,20000,-1); assert(w.cap==80);
  cyd_expire_donation(w,-1,20000); assert(w.cap==80);
  cyd_expire_donation(w,20000,19999); assert(w.cap==80);
  cyd_expire_donation(w,20000,20000);
  assert(w.cap==60 && w.donated==0 && !cyd_donated(w));
  assert(fill_colour(w.used,w.cap)==RED);
  w.base=NAN; w.cap=80; w.donated=20;
  cyd_expire_donation(w,20000,20001);
  assert(std::isnan(w.cap) && !cyd_donated(w));
  setenv("TZ","UTC0",1); tzset();
  assert(cyd_donation_end(64800,3600)=="until 18:00");
  assert(cyd_donation_end(172800,3600)=="donated until Sat");
  assert(cyd_donation_end(3600,3600).empty());
  assert(cyd_donation_end(3600,-1).empty());
  setenv("TZ","EST5",1); tzset();
  assert(cyd_donation_end(64800,21600)=="until 13:00");
  assert(cyd_donation_end(21600,3600)=="donated until Thu");
}
'''
        with tempfile.TemporaryDirectory() as d:
            cpp, exe = Path(d) / 'usage_inputs.cpp', Path(d) / 'usage_inputs'
            cpp.write_text(code)
            subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'firmware'), str(cpp), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_age_contrast_and_suppression_contracts(self):
        def linear(c):
            c /= 255
            return c / 12.92 if c <= .04045 else ((c + .055) / 1.055) ** 2.4
        def lum(rgb): return sum(w * linear(c) for w, c in zip((.2126,.7152,.0722), rgb))
        self.assertGreaterEqual((lum((170,183,196))+.05)/(lum((16,24,32))+.05),4.5)
        source = (ROOT / 'firmware/pages/usage/usage.h').read_text()
        self.assertIn('bool dim = !grey && (p.snoozed || p.old);', source)
        self.assertIn('set_hidden(age, !(data && !grey && p.old) || (at_limit && auth_bad));', source)
        self.assertIn('7 * 86400.0f), cw, dw, false}', source)
        self.assertIn('lv_obj_set_style_text_font(age, f16', source)

    def test_limit_in_words_and_cap_line_precedence(self):
        code = r'''#include <cassert>
#include "model.h"
int main() {
  CydPlan p; p.state=1;
  assert(cyd_cap_note(p)==CYD_CAP_NONE && cyd_cap_text(p,1000).empty() && !cyd_limit_chip(p));
  p.five.cap=90; p.five.base=60;
  assert(cyd_cap_note(p)==CYD_CAP_DONATED && cyd_cap_text(p,1000)=="donated cap");
  p.limit_reached=true;  // over the cap only: no words
  assert(cyd_cap_note(p)==CYD_CAP_DONATED && !cyd_limit_chip(p));
  p.hard_limit=true;
  assert(cyd_cap_note(p)==CYD_CAP_LIMIT && cyd_cap_text(p,1000)=="limit reached" && cyd_limit_chip(p));
  p.donated_until=5000;
  assert(cyd_cap_text(p,1000)=="limit reached");
  p.snoozed=true;
  assert(cyd_cap_note(p)==CYD_CAP_PAUSED && cyd_cap_text(p,1000)=="paused" && !cyd_limit_chip(p));
  p.snoozed=false; p.state=2; assert(!cyd_limit_chip(p) && cyd_cap_text(p,1000)=="limit reached");
  p.state=0; assert(!cyd_limit_chip(p));
  p.state=1; p.limit_reached=p.hard_limit=false; p.five=CydWindow(); p.weekly.donated=5;
  assert(cyd_cap_note(p)==CYD_CAP_DONATED);
}
'''
        with tempfile.TemporaryDirectory() as d:
            cpp, exe = Path(d) / 'cap_note.cpp', Path(d) / 'cap_note'
            cpp.write_text(code)
            subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'firmware'), str(cpp), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
        def linear(c):
            c /= 255
            return c / 12.92 if c <= .04045 else ((c + .055) / 1.055) ** 2.4
        def lum(rgb): return sum(w * linear(c) for w, c in zip((.2126,.7152,.0722), rgb))
        self.assertGreaterEqual((lum((0xF0,0x44,0x52))+.05)/(lum((16,24,32))+.05),4.5)
        usage = (ROOT / 'firmware/pages/usage/usage.h').read_text()
        self.assertIn('limit = mk_chip(chips, RED, RED, false);', usage)
        self.assertIn('lv_obj_set_style_text_font(limit, f16', usage)
        self.assertIn('set_hidden(limit, !at_limit);', usage)
        detail = (ROOT / 'firmware/pages/detail/detail.h').read_text()
        self.assertIn('cyd_cap_text(p, now)', detail)
        self.assertIn('note == CYD_CAP_LIMIT ? RED', detail)


if __name__ == '__main__':
    unittest.main()
