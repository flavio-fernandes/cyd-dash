"""Execute the firmware's C++ colour function; WCAG sRGB contrast on quantized RGB."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
CODE = r'''
#include "pages/usage/countdown_colour.h"
#include <cassert>
#include <cstdio>
int main() {
  using namespace cyd_countdown;
  assert(rainbow(-1) == rainbow(0));
  assert(rainbow(2) == rainbow(1));
  assert(colour(18000,0,true,false,false) == rainbow(0));
  assert(colour(0,0,true,false,false) == rainbow(1));
  assert(colour(0,1,true,false,false) == rainbow(1));
  assert(colour(36000,0,true,false,false) == rainbow(0));
  for (int fresh=0; fresh<2; ++fresh)
    for (int inferred=0; inferred<2; ++inferred)
      for (int snoozed=0; snoozed<2; ++snoozed) {
        auto c = colour(9000,0,fresh,inferred,snoozed);
        assert(c == ((fresh && !inferred && !snoozed) ? rainbow(0.5) : 0x788695));
      }
  assert(colour(-1,0,true,false,false) == 0x788695);
  for (int i=0; i<=4; ++i) printf("SAMPLE %.2f %06X\n", i/4.0, rainbow(i/4.0f));
  // A dense sweep includes every quantized colour, plus the exact blue minimum.
  for (int i=0; i<=275000; ++i) printf("SCALE %d %06X\n", i, rainbow(i/275000.0f));
  printf("BLUE %06X\n", rainbow(240.0f/275.0f));
}
'''
def luminance(rgb):
    v = [(rgb >> n & 255) / 255 for n in (16, 8, 0)]
    v = [c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4 for c in v]
    return sum(c * w for c, w in zip(v, (0.2126, 0.7152, 0.0722)))
def contrast(rgb):
    return (luminance(rgb) + 0.05) / (luminance(0x101820) + 0.05)
with tempfile.TemporaryDirectory() as directory:
    src, exe = Path(directory) / 'cyd_colour.cpp', Path(directory) / 'cyd_colour'
    src.write_text(CODE)
    subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I',
                    str(ROOT / 'firmware'), str(src), '-o', str(exe)], check=True)
    lines = subprocess.check_output([str(exe)], text=True).splitlines()
worst = (100.0, '')
for line in lines:
    rgb = int(line.split()[-1], 16)
    ratio = contrast(rgb)
    if ratio < worst[0]:
        worst = (ratio, line)
    if line.startswith(('SAMPLE', 'BLUE')):
        print(f'{line} contrast={ratio:.6f}:1')
print(f'WORST {worst[1]} contrast={worst[0]:.6f}:1')
assert worst[0] >= 4.5
usage = (ROOT / 'firmware/pages/usage/usage.h').read_text()
assert 'sub = mk_label(p, f16, GREY, cx, 146, "5h ")' in usage
assert 'set_text_color(sub_value, c_sub, cyd_countdown::colour(' in usage
assert 'if (cache == c) return;' in usage
assert 'st == FRESH, p.five.inferred, p.snoozed' in usage
assert 'minutes <= 300' in usage
print('PASS: countdown RGB, clamp, now, suppression, contrast and cached split-label contracts')
