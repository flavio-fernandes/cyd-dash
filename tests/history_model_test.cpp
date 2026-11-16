#include "history_model.h"
#include <cassert>
#include <iostream>
#include <cstring>
using namespace cyd_history_model;
int main() {
  History h;
  h.add(0, 20); h.add(59, 21); assert(h.count == 1 && h.points[0].value == 21);
  h.add(60, 22); assert(h.count == 2);
  h.add(61, NAN); h.add(62, INFINITY); assert(h.last == 60);
  h.add(0, 99); assert(h.last == 60);
  assert(bucket(-1, 60) == -60 && bucket(-60, 60) == -60);
  h.expire(7260); assert(h.count == 1); h.expire(7320); assert(h.count == 0);
  h.configure(86400, 1); assert(h.period == 360);
  for (int i=0; i<500; ++i) h.add(i*360, i); assert(h.count == CAPACITY);
  h = History{};
  assert(seed(h, "[20,21,22]", 0, -1)); assert(h.count == 3 && h.points[0].time == -120);
  assert(seed(h, "[[940,20],[1000,21]]", 0, 1000)); assert(h.count == 2 && h.last == 0);
  assert(seed(h, "[[100,20],[160,21]]", 0, 1000)); assert(h.last == -840);
  for (const char *bad : {"null", "[null]", "[true]", "[1,]", "[01]", "[.1]", "[NaN]", "[1e999]", "[1000001]", "[1]x", "[[1001,1]]", "[[2,1],[1,2]]", "[[1.5,2]]", "[1,[2,3]]"}) {
    int n = h.count; assert(!seed(h, bad, 0, 1000)); assert(h.count == n);
  }
  assert(!seed(h, "[[100,1]]", 0, -1));
  assert(seed(h, "[]", 0, 1000)); assert(h.count == 0);
  assert(seed(h, "[ -1.5, 2e1 ]", 0, -1));
  auto a = scale(21, 21); assert(a.low < 21 && a.high > 21);
  a = scale(-2, 4); assert(a.low < -2 && a.high > 4);
  a = scale(21, 22, 0, 30); assert(a.low == 0 && a.high == 30);
  a = scale(21, 22, 30, 0); assert(a.high > a.low);
  assert(nice(0.21f) == 0.5f && nice(1.1f) == 2 && nice(2.1f) == 5 && nice(5.1f) == 10);
  // Reproduce a dense initial history ageing out under a slow live feed. Four days are
  // simulated without sleeping; every remaining neighbouring pair must still be drawable.
  for (int spacing : {60, 180, 300, 600}) {
    History slow;
    for (int t = -7200; t <= 0; t += 60) slow.add(t, 100);
    for (int t = spacing; t <= 4 * 86400; t += spacing) {
      slow.add(t, 100 + t / 60); slow.expire(t);
    }
    assert(slow.count > 1 && slow.count <= CAPACITY);
    bool joined[CAPACITY]; slow.connections(joined);
    for (int i = 1; i < slow.count; ++i)
      assert(joined[i] && slow.gap_limit(i) == spacing * 2);
  }
  // Check the transition BEFORE the dense seed expires, not only the four-day end state.
  for (int spacing : {180, 300, 600}) {
    History mixed;
    for (int s = -7140; s <= 0; s += 60) mixed.add(s, 100);
    for (int s = spacing; s <= 3600; s += spacing) {
      mixed.add(s, 100 + s / 60); mixed.expire(s);
      if (s == 1800 || s == 3600) {
        bool joined[CAPACITY]; mixed.connections(joined);
        int live_segments = 0;
        for (int i = 1; i < mixed.count; ++i) {
          if (mixed.points[i].time <= 0) continue;
          assert(joined[i]); ++live_segments;
        }
        assert(live_segments == s / spacing);
      }
    }
  }
  // A five-minute feed with jitter and then a long outage: only the outage breaks the line.
  History slow;
  int64_t t = 0;
  for (int i = 0; i < 50; ++i) {
    t += 300 + (i % 3 - 1) * 17;
    slow.add(t, 100 + i); slow.expire(t);
  }
  bool joined[CAPACITY]; slow.connections(joined);
  for (int i = 1; i < slow.count; ++i)
    assert(joined[i] && slow.gap_limit(i) == 600);
  t += 2400; slow.add(t, 200); slow.expire(t);
  slow.connections(joined);
  int breaks = 0;
  for (int i = 1; i < slow.count; ++i)
    breaks += !joined[i];
  assert(breaks == 1);
  // One reading during an outage must not bridge either side after the normal feed resumes.
  History isolated;
  for (int s = 0; s <= 1800; s += 60) isolated.add(s, s);
  isolated.add(3600, 3600);
  for (int s = 5400; s <= 7200; s += 60) isolated.add(s, s);
  isolated.connections(joined); breaks = 0;
  for (int i = 1; i < isolated.count; ++i) {
    bool gap = isolated.points[i].time - isolated.points[i - 1].time > 120;
    assert(joined[i] != gap); breaks += !joined[i];
  }
  assert(breaks == 2);
  // Fast arrivals coalesce into minute buckets and retain the old two-period limit.
  History fast;
  assert(fast.gap_limit(0) == 120);
  for (int i = 0; i <= 3600; i += 5) fast.add(i, i);
  fast.connections(joined);
  for (int i = 1; i < fast.count; ++i) assert(joined[i] && fast.gap_limit(i) == 120);
  fast.add(4200, 4200); fast.connections(joined);
  assert(!joined[fast.count - 1] && fast.gap_limit(fast.count - 1) == 120);
  History even;
  even.add(0, 0); even.add(60, 1); even.add(660, 2);
  assert(even.gap_limit(2) == 120);  // lower median, not the average of cadence and outage
  even.connections(joined); assert(joined[1] && !joined[2]);
  char text[16];
  for (auto example : {std::pair<float, const char *>{21.5f, "21.5"}, {999.0f, "999.0"},
                       {1000.0f, "1000"}, {20000.0f, "20000"}, {-20000.0f, "-20000"},
                       {1000000.0f, "1000000"}, {NAN, "--.-"}, {INFINITY, "--.-"}}) {
    format_value(text, sizeof text, example.first);
    assert(!std::strcmp(text, example.second));
  }
  std::cout << "PASS: history bucketing, adaptive gaps, outage preservation, expiry, capacity, strict transactional seed parsing, scale and reading format; buffer=" << sizeof h.points << " model=" << sizeof h << "\n";
}
