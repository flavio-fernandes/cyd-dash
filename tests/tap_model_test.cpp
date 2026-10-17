// Host test of firmware/core/tap.h (C7c): one physical tap is one tap, also when the XPT2046
// bounces. Sim stands in for core/ui.yaml and LVGL: LVGL presses whatever is under the finger at
// each touch-down (a nav button only while the bar shows) and clicks it at each release.
#include "tap.h"
#include <cassert>
#include <iostream>
using namespace cyd_tap;

struct Sim {
  Tap t;
  bool bar = false, on_button = false, auto_mode = true, old_rule = false, old_armed = false;
  uint32_t bar_since = 0;
  int toggles = 0, long_presses = 0;
  void down(uint32_t now) {
    bool bounce = t.touch(now, bar, bar_since);
    on_button = bar;  // LVGL: a new press lands on the Pin button only if the bar shows
    if (old_rule) old_armed = bar;  // before C7c: nav_armed = overlay_visible on every touch-down
    if (!bounce && !bar) { bar = true; bar_since = now; }
    if (old_rule && !bar) { bar = true; bar_since = now; }
  }
  void up(uint32_t now) {
    t.release(now);
    if (!on_button) return;
    bool act = old_rule ? old_armed : t.click(now);
    if (act) { auto_mode = !auto_mode; toggles++; t.nav(auto_mode ? "pin->auto" : "auto->pin", true, now); }
  }
  void hold(uint32_t now) { if (now - t.start >= 3000) t.long_press(); }
  void settle(uint32_t now) { if (t.settled(now) && t.released - t.start >= 5000) long_presses++; }
};

int main() {
  // The old rule, reproduced: a bounced tap on Pin toggles twice (the dot flips back), and a
  // bounced first tap presses the button the first tap was only meant to reveal.
  { Sim s; s.old_rule = true; s.bar = true; s.bar_since = 0;
    s.down(10000); s.up(10040); s.down(10080); s.up(10160);
    assert(s.toggles == 2 && s.auto_mode); }
  { Sim s; s.old_rule = true;
    s.down(10000); s.up(10040); s.down(10080); s.up(10160);
    assert(s.toggles == 1); }

  // A clean tap on Pin with the bar showing: one toggle, one touch, no bounce.
  { Sim s; s.bar = true; s.bar_since = 0;
    s.down(10000); s.up(10120);
    assert(s.toggles == 1 && !s.auto_mode && s.t.touches == 1 && s.t.bounces == 0 && s.t.navs == 1);
    assert(s.t.last == "auto->pin" && s.t.src == "touch" && s.t.last_at == 10120); }

  // A bounced tap on Pin (released for 40 ms mid-press): one toggle, the bounce merged.
  { Sim s; s.bar = true; s.bar_since = 0;
    s.down(10000); s.up(10040); s.down(10080); s.up(10160);
    assert(s.toggles == 1 && !s.auto_mode);
    assert(s.t.touches == 1 && s.t.bounces == 1 && s.t.ignored == 1 && s.t.gap_min == 40);
    // ... even when the finger stays down long after the bounce (past REPEAT_MS)
    s.settle(10400);
    s.down(20000); s.up(20030); s.down(20060); s.up(20900);
    assert(s.toggles == 2 && s.auto_mode && s.t.touches == 2 && s.t.bounces == 2); }

  // A bounced first tap (bar hidden): it only shows the bar, nothing is pressed.
  { Sim s;
    s.down(10000); s.up(10040); s.down(10080); s.up(10160);
    assert(s.bar && s.toggles == 0 && s.t.touches == 1 && s.t.bounces == 1 && s.t.unarmed == 1);
    // and a second tap right after a slow bounce (bar shown under ARM_MS ago) does not act either
    Sim r; r.down(10000); r.up(10100); r.down(10260); r.up(10330);
    assert(r.toggles == 0 && r.t.touches == 2 && r.t.bounces == 0 && r.t.unarmed == 1); }

  // A real double tap 600 ms apart: the first shows the bar, the second presses Pin.
  { Sim s;
    s.down(10000); s.up(10100); s.settle(10300);
    s.down(10600); s.up(10700);
    assert(s.toggles == 1 && s.t.touches == 2 && s.t.bounces == 0); }

  // Two deliberate taps on Pin 300 ms apart: the second is within REPEAT_MS and is ignored.
  { Sim s; s.bar = true;
    s.down(10000); s.up(10080); s.settle(10250);
    s.down(10380); s.up(10450);
    assert(s.toggles == 1 && s.t.touches == 2 && s.t.ignored == 1); }

  // A long press with a bounce in it: one touch, the hold keeps counting from the first
  // touch-down, it never presses Pin, and calibrate starts only once the final release settles.
  { Sim s; s.bar = true;
    s.down(10000); s.hold(13000); s.up(13500); s.settle(13550); s.down(13560);
    assert(s.t.touches == 1 && s.t.bounces == 1 && s.t.start == 10000 && s.long_presses == 0);
    s.hold(14000); s.up(16000);
    s.settle(16100); assert(s.long_presses == 0);
    s.settle(16150); assert(s.long_presses == 1);
    s.settle(16300); assert(s.long_presses == 1);
    assert(s.toggles == 0 && s.t.unarmed == 2); }
  // ... and a bounce just past 5 s does not start calibrate early.
  { Sim s;
    s.down(10000); s.hold(15000); s.up(15500); s.settle(15600); s.down(15560);
    assert(s.long_presses == 0 && s.t.bounces == 1);
    s.up(17000); s.settle(17200); assert(s.long_presses == 1); }

  // Commands are nav actions from `cmd`; other commands are not nav actions.
  { Tap t; t.nav(nav_name("auto on"), false, 500);
    assert(t.navs == 1 && t.last == "pin->auto" && t.src == "cmd");
    assert(nav_name("page history") == "page history" && nav_name("next") == "next");
    assert(nav_name("sleep") == "sleep" && nav_name("wake").empty() && nav_name("interval 20").empty()); }

  std::cout << "PASS: old rule reproduced (double toggle, armed bounce); clean tap, bounced Pin tap, "
               "bounced first tap, double tap 600 ms, repeat guard, long press with bounce\n";
}
