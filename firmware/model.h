#pragma once
#include <cmath>
#include <string>
#include <array>
#include <cstdint>
#include <cstdio>
#include <ctime>
// Unknown numeric fields stay NAN/-1; null is never silently converted to zero.
struct CydWindow {
  float used = NAN, cap = NAN, base = NAN, donated = NAN;
  int64_t resets = -1;
  bool inferred = false;
};
struct CydPlan {
  CydWindow five, weekly;
  std::string label, auth;
  bool snoozed = false, limit_reached = false, old = false;
  bool hard_limit = false;  // the feed said so (limit_reached also covers at or over the cap)
  int state = 0;
  int64_t age = -1, measured_at = -1, donated_until = -1;
  int64_t old_minute = -1;
  float old_after = 1800;
};
inline bool cyd_donated(const CydWindow &w) {
  return (!std::isnan(w.cap) && !std::isnan(w.base) && w.cap > w.base) ||
         (!std::isnan(w.donated) && w.donated > 0);
}
inline bool cyd_measurement_old(int64_t measured, float after, int64_t now) {
  return measured >= 0 && now >= measured && double(now - measured) > after;
}
inline void cyd_expire_donation(CydWindow &w, int64_t until, int64_t now) {
  if (until < 0 || now < 0 || now < until) return;
  w.cap = w.base;  // An unknown base stays unknown; never retain an expired allowance.
  w.donated = 0;
}
inline std::string cyd_measurement_age(int64_t measured, int64_t now) {
  if (measured < 0 || now < measured) return "";
  int64_t minutes = (now - measured) / 60;
  char b[40];
  if (minutes < 60) snprintf(b, sizeof b, "%lldm old", (long long) minutes);
  else snprintf(b, sizeof b, "%lldh%02d old", (long long) (minutes / 60), int(minutes % 60));
  return b;
}
inline std::string cyd_donation_end(int64_t until, int64_t now) {
  if (until < 0 || now < 0 || until <= now) return "";
  time_t end = time_t(until), current = time_t(now);
  struct tm e{}, n{};
  if (!localtime_r(&end, &e) || !localtime_r(&current, &n)) return "";
  char b[32];
  if (e.tm_year == n.tm_year && e.tm_yday == n.tm_yday) strftime(b, sizeof b, "until %H:%M", &e);
  else strftime(b, sizeof b, "donated until %a", &e);
  return b;
}
// Red is never the only signal: a fresh gauge at its hard limit also shows a `limit` chip.
// Over the cap needs no words: the cap tick and the breathing fill say it (owner, C7b).
// Paused gauges show `paused` instead, and stale ones (state 2) turn grey.
inline bool cyd_limit_chip(const CydPlan &p) {
  return p.hard_limit && !p.snoozed && p.state == 1;
}
// The detail page's cap line, in words: paused wins, then a hard limit, then a donation.
enum CydCapNote { CYD_CAP_NONE, CYD_CAP_PAUSED, CYD_CAP_LIMIT, CYD_CAP_DONATED };
inline CydCapNote cyd_cap_note(const CydPlan &p) {
  if (p.snoozed) return CYD_CAP_PAUSED;
  if (p.hard_limit) return CYD_CAP_LIMIT;
  if (cyd_donated(p.five) || cyd_donated(p.weekly)) return CYD_CAP_DONATED;
  return CYD_CAP_NONE;
}
inline std::string cyd_cap_text(const CydPlan &p, int64_t now) {
  switch (cyd_cap_note(p)) {
    case CYD_CAP_PAUSED: return "paused";
    case CYD_CAP_LIMIT: return "limit reached";
    case CYD_CAP_DONATED: {
      std::string end = cyd_donation_end(p.donated_until, now);
      return end.empty() ? "donated cap" : end;
    }
    default: return "";
  }
}
static std::array<CydPlan, 2> cyd_usage_plans;
