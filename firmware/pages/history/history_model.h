#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cctype>
#include <cstdio>

namespace cyd_history_model {
constexpr int CAPACITY = 241;
struct Point { int64_t time; float value; };
inline int64_t bucket(int64_t t, int period) {
  int64_t q = t / period;
  return (t < 0 && t % period ? q - 1 : q) * period;
}
struct Scale { float low, high, step; };
// Keep the large reading and its range labels consistent without unnecessary decimals.
inline void format_value(char *out, size_t size, float value) {
  if (!std::isfinite(value)) std::snprintf(out, size, "--.-");
  else std::snprintf(out, size, std::fabs(value) >= 1000 ? "%.0f" : "%.1f", double(value));
}
inline float nice(float v) {
  if (!(v > 0) || !std::isfinite(v)) return 1;
  float power = std::pow(10.0f, std::floor(std::log10(v)));
  float n = v / power;
  return (n <= 1 ? 1 : n <= 2 ? 2 : n <= 5 ? 5 : 10) * power;
}
inline Scale scale(float low, float high, float fixed_low = NAN, float fixed_high = NAN) {
  if (!std::isfinite(low) || !std::isfinite(high)) { low = 0; high = 1; }
  float pad = std::max((high - low) * 0.12f, std::max(std::fabs(high) * 0.01f, 0.1f));
  float step = nice((high - low + pad * 2) / 4);
  float a = std::floor((low - pad) / step) * step;
  float b = std::ceil((high + pad) / step) * step;
  if (std::isfinite(fixed_low) && std::fabs(fixed_low) <= 1000000) a = fixed_low;
  if (std::isfinite(fixed_high) && std::fabs(fixed_high) <= 1000000) b = fixed_high;
  if (!(b > a)) { a = std::floor((low - pad) / step) * step; b = std::ceil((high + pad) / step) * step; }
  return {a, b, step};
}
struct History {
  Point points[CAPACITY]{};
  int count = 0, period = 60, window = 7200;
  int64_t last = INT64_MIN;
  uint32_t revision = 0;
  bool configure(float w, float p) {
    int nw = std::isfinite(w) ? int(std::clamp(w, 60.0f, 86400.0f)) : 7200;
    int np = std::isfinite(p) ? int(std::clamp(p, 1.0f, float(nw))) : 60;
    np = std::max(np, (nw + CAPACITY - 2) / (CAPACITY - 1));
    if (window == nw && period == np) return false;
    window = nw; period = np; count = 0; last = INT64_MIN; ++revision; return true;
  }
  void expire(int64_t now) {
    int n = 0;
    while (n < count && points[n].time < bucket(now, period) - window) ++n;
    if (n) { std::move(points + n, points + count, points); count -= n; ++revision; }
  }
  void add(int64_t t, float value) {
    if (!std::isfinite(value) || std::fabs(value) > 1000000) return;
    int64_t b = bucket(t, period);
    if (count && b < points[count - 1].time) return;
    if (count && b == points[count - 1].time) {
      if (points[count - 1].value != value) { points[count - 1].value = value; ++revision; }
    } else {
      if (count == CAPACITY) { std::move(points + 1, points + count, points); --count; }
      points[count++] = {b, value}; ++revision;
    }
    last = t;
  }
  void range(float &low, float &high) const {
    low = high = count ? points[0].value : NAN;
    for (int i = 1; i < count; ++i) { low = std::min(low, points[i].value); high = std::max(high, points[i].value); }
  }
  // Estimate cadence around the edge ending at i, rather than letting a dense old seed
  // dominate slower live readings. The lower median rejects one or two isolated long gaps.
  // Five int32_t steps use 20 bytes of stack. Expired history spans at most one day; clamp
  // larger steps defensively for callers that have not expired their buffer yet.
  int64_t gap_limit(int i) const {
    if (i < 1 || i >= count) return int64_t(period) * 2;
    int32_t steps[5];
    int n = 0;
    for (int j = std::max(1, i - 2); j <= std::min(count - 1, i + 2); ++j)
      steps[n++] = int32_t(std::min<int64_t>(points[j].time - points[j - 1].time, INT32_MAX));
    auto middle = steps + (n - 1) / 2;
    std::nth_element(steps, middle, steps + n);
    return std::max<int64_t>(period, *middle) * 2;
  }
  // Cache once per revision, outside the draw callback. Entry i joins point i-1 to point i.
  void connections(bool (&joined)[CAPACITY]) const {
    std::fill(joined, joined + CAPACITY, false);
    for (int i = 1; i < count; ++i)
      joined[i] = points[i].time - points[i - 1].time <= gap_limit(i);
  }
};
// Strict bounded JSON number parser; no strings, nulls, booleans or partial acceptance.
struct Parser {
  const char *s;
  void ws() { while (*s && std::isspace(static_cast<unsigned char>(*s))) ++s; }
  bool take(char c) { ws(); if (*s != c) return false; ++s; return true; }
  bool number(double &n) {
    ws(); const char *start = s;
    if (*s == '-') ++s;
    if (*s == '0') ++s;
    else { if (*s < '1' || *s > '9') return false; while (std::isdigit(static_cast<unsigned char>(*s))) ++s; }
    if (*s == '.') { ++s; if (!std::isdigit(static_cast<unsigned char>(*s))) return false; while (std::isdigit(static_cast<unsigned char>(*s))) ++s; }
    if (*s == 'e' || *s == 'E') { ++s; if (*s == '+' || *s == '-') ++s; if (!std::isdigit(static_cast<unsigned char>(*s))) return false; while (std::isdigit(static_cast<unsigned char>(*s))) ++s; }
    char *end; n = std::strtod(start, &end);
    return end == s && std::isfinite(n);
  }
};
inline bool seed(History &h, const char *json, int64_t now, int64_t epoch) {
  Parser p{json}; Point input[CAPACITY]; int n = 0;
  if (!p.take('[')) return false;
  p.ws(); bool pairs = *p.s == '[';
  if (*p.s != ']') do {
    if (n == CAPACITY) return false;
    double t = 0, v = 0;
    if (pairs && (!p.take('[') || !p.number(t) || !p.take(','))) return false;
    if (!p.number(v) || std::fabs(v) > 1000000 || (pairs && !p.take(']'))) return false;
    if (pairs && (epoch < 0 || t < 0 || t > epoch || t != std::floor(t))) return false;
    input[n] = {pairs ? now - (epoch - int64_t(t)) : 0, float(v)};
    if (pairs && n && input[n].time < input[n - 1].time) return false;
    ++n;
  } while (p.take(','));
  if (!p.take(']')) return false;
  p.ws(); if (*p.s) return false;
  // Validation is complete before touching existing history.
  h.count = 0; h.last = INT64_MIN; ++h.revision;
  for (int i = 0; i < n; ++i) h.add(pairs ? input[i].time : now - int64_t(n - 1 - i) * h.period, input[i].value);
  h.expire(now); return true;
}
}  // namespace cyd_history_model
