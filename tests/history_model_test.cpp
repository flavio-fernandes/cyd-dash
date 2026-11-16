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
    assert(slow.gap_limit() == spacing * 2);
    for (int i = 1; i < slow.count; ++i)
      assert(slow.points[i].time - slow.points[i - 1].time <= slow.gap_limit());
  }
  // A five-minute feed with jitter and then a long outage: only the outage breaks the line.
  History slow;
  int64_t t = 0;
  for (int i = 0; i < 50; ++i) {
    t += 300 + (i % 3 - 1) * 17;
    slow.add(t, 100 + i); slow.expire(t);
  }
  assert(slow.gap_limit() == 600);
  for (int i = 1; i < slow.count; ++i)
    assert(slow.points[i].time - slow.points[i - 1].time <= slow.gap_limit());
  t += 2400; slow.add(t, 200); slow.expire(t);
  assert(slow.gap_limit() == 600);
  int breaks = 0;
  for (int i = 1; i < slow.count; ++i)
    breaks += slow.points[i].time - slow.points[i - 1].time > slow.gap_limit();
  assert(breaks == 1);
  // Fast arrivals coalesce into minute buckets and retain the old two-period limit.
  History fast;
  assert(fast.gap_limit() == 120);
  for (int i = 0; i <= 3600; i += 5) fast.add(i, i);
  assert(fast.gap_limit() == 120);
  fast.add(4200, 4200); assert(fast.gap_limit() == 120);
  History even;
  even.add(0, 0); even.add(60, 1); even.add(660, 2);
  assert(even.gap_limit() == 120);  // lower median, not the average of cadence and outage
  char text[16];
  for (auto example : {std::pair<float, const char *>{21.5f, "21.5"}, {999.0f, "999.0"},
                       {1000.0f, "1000"}, {20000.0f, "20000"}, {-20000.0f, "-20000"},
                       {1000000.0f, "1000000"}, {NAN, "--.-"}, {INFINITY, "--.-"}}) {
    format_value(text, sizeof text, example.first);
    assert(!std::strcmp(text, example.second));
  }
  std::cout << "PASS: history bucketing, adaptive gaps, outage preservation, expiry, capacity, strict transactional seed parsing, scale and reading format; buffer=" << sizeof h.points << " model=" << sizeof h << "\n";
}
