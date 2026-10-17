"""Page author examples, generated reference and comfort calculations."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import yaml
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import cyd_build as build

class PageTests(unittest.TestCase):
    def test_reference_current(self):
        self.assertEqual((ROOT / 'docs/pages.md').read_text(), build.pages_document())

    def test_walkthrough_reproduces_hello(self):
        for name in ('manifest.yaml', 'page.yaml', 'example.h'):
            template = (ROOT / 'firmware/pages/_template' / name).read_text()
            self.assertEqual(template.replace('example', 'hello'),
                             (ROOT / 'firmware/pages/hello' / name.replace('example', 'hello')).read_text())
        self.assertNotIn('_template', build.available_pages(ROOT / 'firmware'))

    def plan(self, pages, bindings):
        with tempfile.TemporaryDirectory() as folder:
            site, bind = Path(folder)/'site.yaml', Path(folder)/'bindings.yaml'
            site.write_text(yaml.safe_dump({'cyd': {'pages': pages}}))
            bind.write_text(bindings)
            return build.plan(site, bind)

    def test_hello_no_inputs(self):
        p = self.plan(['hello'], '{}')
        self.assertEqual(p['slots'], [])
        self.assertEqual(p['topics'], [])

    def test_thermostat_minimal_and_missing(self):
        with self.assertRaisesRegex(build.BuildError, 'needs a value for "temperature"'):
            self.plan(['thermostat'], '{}')
        p = self.plan(['thermostat'], 'thermostat: {temperature: {topic: home/room/temperature}}')
        self.assertEqual([x['slot'] for x in p['slots'] if x['required']], ['temperature'])
        self.assertEqual(next(x for x in p['slots'] if x['slot']=='unit')['literal'], '°C')
        self.assertEqual(next(x for x in p['slots'] if x['slot']=='humidity')['source'], 'unbound')

    def test_detail_dependency_and_order(self):
        with self.assertRaisesRegex(build.BuildError, 'needs page "usage"'):
            self.plan(['detail'], '{}')
        binding = (ROOT/'firmware/bindings.example.yaml').read_text()
        p = self.plan(['detail', 'usage'], binding)
        self.assertEqual([x['name'] for x in p['pages']], ['detail', 'usage'])
        header = (ROOT/'firmware/pages/detail/detail.h').read_text()
        self.assertIn('lv_obj_get_child(root, 2)', header)

    def test_comfort_math(self):
        code = r'''
#include "firmware/pages/thermostat/climate.h"
#include <cassert>
#include <cstring>
int main() {
  using namespace cyd_climate;
  assert(std::fabs(celsius(72.5, true) - 22.5) < .001);
  assert(colour(16) == 0x70BFFF && colour(21) == 0x32D583);
  assert(colour(26) == 0xFFB547 && colour(31) == 0xF04452);
  assert(!strcmp(band(18), "Cool") && !strcmp(band(22), "Comfortable"));
  assert(!strcmp(band(27), "Warm") && !strcmp(band(32), "Hot"));
  assert(arc(-50) == 0 && arc(60) == 270);
  float v=16; int ticks=0;
  while (v != 30 && ticks++ < 30) { float n=ease(v,30); assert(n>v && n<=30); v=n; }
  assert(v == 30 && ticks < 30);
}
'''
        with tempfile.TemporaryDirectory() as folder:
            source, binary = Path(folder)/'climate.cpp', Path(folder)/'climate'
            source.write_text(code)
            subprocess.run(['g++', '-std=c++17', '-I'+str(ROOT), str(source), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

if __name__ == '__main__':
    unittest.main()
