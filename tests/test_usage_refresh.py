"""Run slot refresh unchanged, with a typed source adapter and a controlled clock."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class UsageRefreshTests(unittest.TestCase):
    def test_clock_only_changes_and_missing_inputs(self):
        source = (ROOT / 'firmware/pages/usage/usage_slots.h').read_text()
        fields = re.findall(r'CYD_SLOT_usage_gauge##n##_(\w+)', source)
        names = [f'CYD_SLOT_usage_gauge{n}_{field}' for n in (1, 2) for field in fields]
        code = '''#include <cassert>
#include <algorithm>
#include "model.h"
namespace cyd_slots { enum {UNBOUND, LITERAL}; }
struct Def { int source=cyd_slots::LITERAL; };
enum {''' + ','.join(names) + ''', SLOT_COUNT};
Def CYD_SLOT_DEFS[SLOT_COUNT];
float values[SLOT_COUNT];
int64_t arrival_age=0;
namespace cyd_slots {
float number(int i) { return values[i]; }
int64_t epoch(int i) { return std::isfinite(values[i]) ? int64_t(values[i]) : -1; }
bool flag(int i) { return std::isfinite(values[i]) && values[i]!=0; }
bool valid(int i) { return std::isfinite(values[i]); }
std::string text(int) { return "Plan A"; }
int64_t age(int) { return arrival_age; }
int64_t age_of(int, int updated, int64_t now) { return std::max<int64_t>(0, now-epoch(updated)); }
}
''' + source.replace('#pragma once', '') + r'''
int main() {
  std::fill(values,values+SLOT_COUNT,NAN);
  CYD_SLOT_DEFS[CYD_SLOT_usage_gauge2_label].source=cyd_slots::UNBOUND;
  assert(cyd_usage_data::count()==1);
  const auto s=cyd_usage_data::slots[0];
  values[s.label]=1; values[s.used]=38; values[s.resets]=20000;
  values[s.cap]=100; values[s.base]=60; values[s.donated]=40;
  values[s.weekly]=70; values[s.weekly_cap]=80; values[s.weekly_base]=50; values[s.weekly_donated]=30;
  values[s.donated_until]=10100;
  cyd_usage_data::refresh(10000);
  auto &p=cyd_usage_plans[0];
  assert(p.state==1 && !p.old && !p.limit_reached && p.old_after==1800);
  values[s.limit]=1; cyd_usage_data::refresh(10000); assert(p.limit_reached);
  values[s.limit]=0; cyd_usage_data::refresh(10000); assert(!p.limit_reached);
  values[s.measured]=8200; cyd_usage_data::refresh(10000); assert(!p.old);
  cyd_usage_data::refresh(10001); assert(!p.old); // same minute, no extra check
  cyd_usage_data::refresh(10020); assert(p.old && p.state==1);
  cyd_usage_data::refresh(10100); // no new message: both donations expire
  assert(p.five.cap==60 && p.weekly.cap==50);
  assert(!cyd_donated(p.five) && !cyd_donated(p.weekly));
  cyd_usage_data::refresh(10101); assert(p.five.cap==60); // source still has the old donation
  values[s.measured]=10101; cyd_usage_data::refresh(10101); assert(!p.old);
  values[s.old_after]=0; cyd_usage_data::refresh(10140); assert(p.old);
  values[s.old_after]=-1; cyd_usage_data::refresh(10140); assert(!p.old && p.old_after==1800);
  values[s.measured]=NAN; cyd_usage_data::refresh(10140); assert(!p.old);
  values[s.measured]=1; arrival_age=1201; cyd_usage_data::refresh(10140); assert(p.old && p.state==2);
  values[s.used]=NAN; cyd_usage_data::refresh(10140); assert(p.state==0);
  arrival_age=0; values[s.used]=65; values[s.donated_until]=NAN;
  cyd_usage_data::refresh(10140); assert(p.five.cap==100 && cyd_donated(p.five));
  CYD_SLOT_DEFS[CYD_SLOT_usage_gauge2_label].source=cyd_slots::LITERAL;
  assert(cyd_usage_data::count()==2);
  auto s2=cyd_usage_data::slots[1];
  values[s2.label]=1; values[s2.used]=90; values[s2.resets]=20000; values[s2.cap]=90;
  cyd_usage_data::refresh(10140); assert(cyd_usage_plans[1].limit_reached);
}
'''
        with tempfile.TemporaryDirectory() as d:
            cpp, exe = Path(d) / 'refresh.cpp', Path(d) / 'refresh'
            cpp.write_text(code)
            subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'firmware'), str(cpp), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    unittest.main()
