"""Over the cap, the red fill breathes: run the C++ breath and turn logic from the source."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / 'firmware/pages/usage/usage.h').read_text()


def between(start, end):
    i = SOURCE.index(start)
    return SOURCE[i:SOURCE.index(end, i)]


class UsagePulseTests(unittest.TestCase):
    def test_breath_cycle_turns_and_colour_rule(self):
        colour = between('inline uint32_t fill_colour(', 'inline float urgency(')
        breathe = between('  void breathe(uint32_t k, bool turn) {', '\n};')
        turns = between('  int reds = 0;', '  if (state == STALE)')
        code = r'''
#include <cassert>
#include <cmath>
#include <cstdint>
#include <set>
constexpr uint32_t GREY=1, GREEN=2, AMBER=3, RED=4;
constexpr int LV_PART_INDICATOR=0;
static uint32_t pulse_steps=0;
static int redraws=0;
void set_arc_opa(int, int &cache, int opa, int) { if (cache != opa) { cache=opa; ++redraws; } }
''' + colour + r'''
struct Ring {
  bool pulse_on=false; int fill=0, fill_opa=255;
''' + breathe + r'''
};
struct Gauge { Ring five; int steps=0; void step(uint32_t k, bool turn) { five.breathe(k, turn); } };
static Gauge gauges[2];
namespace cyd_usage_data { int count() { return 2; } }
void tick(uint32_t k) {
''' + turns + r'''
}
int main() {
  assert(fill_colour(11.9f,15)==GREEN && fill_colour(12,15)==AMBER && fill_colour(14.9f,15)==AMBER);
  assert(fill_colour(15,15)==RED && fill_colour(19,15)==RED);
  assert(fill_colour(19,NAN)==GREEN && fill_colour(19,0)==GREEN && fill_colour(NAN,15)==GREY);
  // No gauge red: nothing breathes, nothing is redrawn.
  for (uint32_t k=1; k<=40; ++k) tick(k);
  assert(pulse_steps==0 && redraws==0 && gauges[0].five.fill_opa==255);
  // One red gauge: 10 steps per 2 s (20 ticks), full -> ~58 % -> full, the other never moves.
  gauges[1].five.pulse_on=true;
  std::set<int> seen; int lo=255, hi=0;
  for (uint32_t k=41; k<=80; ++k) {
    int before=redraws; tick(k);
    assert(redraws-before<=1);
    int o=gauges[1].five.fill_opa; seen.insert(o); lo=o<lo?o:lo; hi=o>hi?o:hi;
  }
  assert(pulse_steps==20 && gauges[0].five.fill_opa==255);
  assert(hi==255 && lo>=140 && lo<=155 && seen.size()>=5);
  // Two red gauges: they alternate, so at most one fill arc is redrawn per tick and per 200 ms.
  gauges[0].five.pulse_on=true; pulse_steps=0;
  int last=-10;
  for (uint32_t k=81; k<=120; ++k) {
    int before=redraws; uint32_t s=pulse_steps; tick(k);
    assert(pulse_steps-s<=1 && redraws-before<=1);
    if (pulse_steps!=s) { assert((int)k-last>=2); last=(int)k; }
  }
  assert(pulse_steps==20);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            cpp, exe = Path(directory) / 'usage_pulse.cpp', Path(directory) / 'usage_pulse'
            cpp.write_text(code)
            subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_only_the_red_five_hour_fill_breathes_and_replaces_its_glow(self):
        update = between('  void update(const RingView &v) {', '  // Called at 10 Hz;')
        self.assertIn('bool pulse = show && u > 0 && !v.grey && !v.dim && v.colour == RED && v.breathe;', update)
        self.assertIn('if (!pulse) set_arc_opa(fill, fill_opa, v.dim ? 90 : 255, LV_PART_INDICATOR);', update)
        self.assertIn('bool on = show && v.glow && u > 0 && !pulse;', update)
        self.assertIn('five.breathe(k, turn);', SOURCE)
        self.assertNotIn('week.breathe', SOURCE)
        breathe = between('  void breathe(uint32_t k, bool turn) {', '\n  }')
        for part in ('track', 'tick', 'glow'):
            self.assertNotIn(part + ',', breathe)

    def test_status_reports_the_pulse(self):
        self.assertIn('root["pulse"]', SOURCE)
        self.assertIn('root["pulse_steps"]', SOURCE)
        self.assertIn('`pulse`, `pulse_steps`', (ROOT / 'docs/mqtt.md').read_text())


if __name__ == '__main__':
    unittest.main()
