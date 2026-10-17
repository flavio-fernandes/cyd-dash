#pragma once
// Read the same typed slots for usage, detail and the saver. No payload shape lives here.
namespace cyd_usage_data {
struct Slots { int label, used, resets, weekly, weekly_resets, cap, base, donated, snoozed, auth, inferred, updated, limit, measured, old_after, weekly_cap, weekly_base, weekly_donated, donated_until; };
#define CYD_GAUGE_SLOTS(n) {CYD_SLOT_usage_gauge##n##_label, CYD_SLOT_usage_gauge##n##_used_pct, CYD_SLOT_usage_gauge##n##_resets_at, CYD_SLOT_usage_gauge##n##_weekly_pct, CYD_SLOT_usage_gauge##n##_weekly_resets_at, CYD_SLOT_usage_gauge##n##_cap_pct, CYD_SLOT_usage_gauge##n##_base_cap_pct, CYD_SLOT_usage_gauge##n##_donated_pct, CYD_SLOT_usage_gauge##n##_snoozed, CYD_SLOT_usage_gauge##n##_auth, CYD_SLOT_usage_gauge##n##_inferred, CYD_SLOT_usage_gauge##n##_updated_at, CYD_SLOT_usage_gauge##n##_limit_reached, CYD_SLOT_usage_gauge##n##_measured_at, CYD_SLOT_usage_gauge##n##_old_after, CYD_SLOT_usage_gauge##n##_weekly_cap_pct, CYD_SLOT_usage_gauge##n##_weekly_base_cap_pct, CYD_SLOT_usage_gauge##n##_weekly_donated_pct, CYD_SLOT_usage_gauge##n##_donated_until}
static const Slots slots[] = {CYD_GAUGE_SLOTS(1), CYD_GAUGE_SLOTS(2)};
#undef CYD_GAUGE_SLOTS
inline int count() { return CYD_SLOT_DEFS[slots[1].label].source == cyd_slots::UNBOUND ? 1 : 2; }
inline void refresh(int64_t now) {
  using namespace cyd_slots;
  for (int i = 0; i < count(); ++i) {
    const auto &s = slots[i]; auto &p = cyd_usage_plans[i];
    p.label = text(s.label); p.auth = text(s.auth);
    p.five.used = number(s.used); p.five.resets = epoch(s.resets);
    p.five.cap = number(s.cap); p.five.base = number(s.base); p.five.donated = number(s.donated);
    p.five.inferred = flag(s.inferred); p.snoozed = flag(s.snoozed);
    p.weekly.used = number(s.weekly); p.weekly.resets = epoch(s.weekly_resets);
    p.weekly.cap = number(s.weekly_cap); p.weekly.base = number(s.weekly_base);
    p.weekly.donated = number(s.weekly_donated); p.donated_until = epoch(s.donated_until);
    cyd_expire_donation(p.five, p.donated_until, now);
    cyd_expire_donation(p.weekly, p.donated_until, now);
    p.hard_limit = flag(s.limit);
    p.limit_reached = p.hard_limit || (!std::isnan(p.five.cap) && p.five.used >= p.five.cap);
    int64_t measured = epoch(s.measured);
    float after = number(s.old_after);
    if (!std::isfinite(after) || after < 0) after = 1800;
    if (p.old_minute != now / 60 || p.measured_at != measured || p.old_after != after) {
      p.old_minute = now / 60; p.measured_at = measured; p.old_after = after;
      p.old = cyd_measurement_old(measured, after, now);
    }
    p.age = std::max(age(s.used), age(s.resets));
    if (CYD_SLOT_DEFS[s.label].source != LITERAL) p.age = std::max(p.age, age(s.label));
    if (valid(s.updated) && now >= 0) p.age = std::max(p.age, age_of(s.used, s.updated, now));
    p.state = !(valid(s.label) && valid(s.used) && valid(s.resets)) ? 0 : (p.age > 1200 ? 2 : 1);
  }
}
inline int state() {
  int st = 1;
  for (int i = 0; i < count(); ++i) {
    if (cyd_usage_plans[i].state == 0) return 0;
    if (cyd_usage_plans[i].state == 2) st = 2;
  }
  return st;
}
inline uint32_t age() {
  int64_t a = 0;
  for (int i = 0; i < count(); ++i) a = std::max(a, cyd_usage_plans[i].age);
  return uint32_t(a);
}
}
