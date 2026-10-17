#include "history_model.h"
#include <cassert>
#include <iostream>
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
  std::cout << "PASS: history bucketing, gaps, expiry, capacity, strict transactional seed parsing, scale and friendly ticks; buffer=" << sizeof h.points << " model=" << sizeof h << "\n";
}
