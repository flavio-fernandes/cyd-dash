#pragma once
// Value bindings and the page API.
//
// Every page declares named slots in its manifest.yaml, and the site's bindings.yaml says where
// each slot's value comes from: a JSON path in an MQTT payload, the raw payload, or a literal.
// tools/cyd_build.py turns that into the tables in generated/cyd_slots.h and one MQTT
// subscription per topic, which calls cyd_slots::on_message() below. A page reads its slots by
// id, with no string lookups:
//
//   if (cyd_slots::valid(CYD_SLOT_thermostat_temperature))
//     show(cyd_slots::number(CYD_SLOT_thermostat_temperature));
//
// A slot that is unbound, missing from the payload or null stays invalid: it is never silently
// zero. Unknown numbers read as NAN, unknown epochs as -1.
#include "esphome.h"
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>

namespace cyd_page {

// What a page's render hook receives.
struct Ctx {
  int64_t now;     // wall-clock seconds (Unix epoch), or -1 until the clock is set
  bool visible;    // the page is on screen right now
  bool connected;  // the MQTT broker is connected
};

// One page in the registry (generated/cyd_registry.h). Every hook is optional (nullptr).
struct Page {
  const char *name, *title;
  bool rotate;                                // takes part in Auto rotation
  void (*build)(lv_obj_t *root);              // once at boot: create the widgets in root
  void (*render)(const Ctx &ctx);             // a slot changed, the page came on screen, or 1 s passed while shown
  void (*tick10)(bool visible);               // every 100 ms: step animations (only while visible)
  void (*status)(JsonObject root);            // add page fields to the status JSON
  void (*selftest)();                         // the `selftest` command: log measurements
};

}  // namespace cyd_page

namespace cyd_slots {

enum Type : uint8_t { NUMBER, TEXT, BOOL, EPOCH };
enum Source : uint8_t { UNBOUND, LITERAL, RAW, JSON };

// One slot, as generated from its page's manifest and its binding.
struct Def {
  const char *page, *name;
  uint8_t page_idx;
  Type type;
  Source source;
  int8_t topic;         // index into the topic table; -1 for literals and unbound slots
  const char *path;     // JSON path such as "a.b[0].c" (JSON only)
  const char *literal;  // the literal or the manifest default, as text (LITERAL only)
};

struct Value {
  bool valid = false;
  float number = NAN;
  int64_t epoch = -1;
  bool flag = false;
  std::string text;
  uint32_t at = 0;  // millis() when a valid value last arrived (0 = never)
};

struct Store {
  const Def *defs = nullptr;
  Value *values = nullptr;
  int count = 0;
  const char *const *topics = nullptr;
  int topic_count = 0;
  uint32_t dirty = 0;  // bit i: page i has a changed slot since its last render
};
static Store store;

// ---- reading slots (what pages use) ----
inline bool valid(int s) { return s >= 0 && s < store.count && store.values[s].valid; }
inline float number(int s) { return valid(s) ? store.values[s].number : NAN; }
inline int64_t epoch(int s) { return valid(s) ? store.values[s].epoch : -1; }
inline bool flag(int s, bool fallback = false) { return valid(s) ? store.values[s].flag : fallback; }
inline const std::string &text(int s) {
  static const std::string empty;
  return valid(s) ? store.values[s].text : empty;
}
// Seconds since the slot's value last arrived, or -1 if it never did. A literal counts from boot.
inline int64_t age(int s) {
  if (!valid(s) || !store.values[s].at) return -1;
  return (millis() - store.values[s].at) / 1000;
}
// How old the page's data is, in seconds (-1 = unknown). With an `updated_at` slot (type epoch,
// the time the publisher measured the value) and a set clock, that wins; otherwise the arrival
// time of `slot` counts.
inline int64_t age_of(int slot, int updated_at, int64_t now) {
  if (updated_at >= 0 && valid(updated_at) && now >= 0) {
    int64_t a = now - epoch(updated_at);
    return a < 0 ? 0 : a;
  }
  return age(slot);
}

// A short duration for people: "42m", "3h05", "2d4h"; "--" when unknown.
inline std::string dur(int64_t seconds) {
  if (seconds < 0) return "--";
  char b[24];
  if (seconds < 3600) snprintf(b, sizeof b, "%dm", int(seconds / 60));
  else if (seconds < 86400) snprintf(b, sizeof b, "%dh%02d", int(seconds / 3600), int(seconds % 3600 / 60));
  else snprintf(b, sizeof b, "%dd%dh", int(seconds / 86400), int(seconds % 86400 / 3600));
  return b;
}

// ---- conversion ----
inline bool parse_number(const char *s, double &out) {
  while (isspace((unsigned char) *s)) s++;
  if (!*s) return false;
  char *end;
  double v = strtod(s, &end);
  while (isspace((unsigned char) *end)) end++;
  if (*end || std::isnan(v)) return false;
  out = v;
  return true;
}
inline bool parse_bool(const char *s, bool &out) {
  std::string t;
  for (; *s; s++)
    if (!isspace((unsigned char) *s)) t += char(tolower((unsigned char) *s));
  if (t == "true" || t == "on" || t == "yes" || t == "1") { out = true; return true; }
  if (t == "false" || t == "off" || t == "no" || t == "0") { out = false; return true; }
  return false;
}
// Text (a raw payload, a JSON string or a literal) into a slot of type t.
inline bool from_text(Type t, const char *s, Value &v) {
  double d;
  switch (t) {
    case TEXT: v.text = s; return true;
    case NUMBER: if (!parse_number(s, d)) return false; v.number = float(d); return true;
    case EPOCH: if (!parse_number(s, d)) return false; v.epoch = int64_t(llround(d)); return true;
    case BOOL: return parse_bool(s, v.flag);
  }
  return false;
}
inline bool from_json(Type t, JsonVariantConst j, Value &v) {
  if (j.isNull()) return false;
  if (j.is<const char *>()) return from_text(t, j.as<const char *>(), v);
  if (j.is<bool>()) {
    bool b = j.as<bool>();
    switch (t) {
      case BOOL: v.flag = b; return true;
      case TEXT: v.text = b ? "true" : "false"; return true;
      case NUMBER: v.number = b ? 1.0f : 0.0f; return true;
      case EPOCH: return false;
    }
  }
  if (j.is<double>()) {
    double d = j.as<double>();
    switch (t) {
      case NUMBER: v.number = float(d); return true;
      case EPOCH: v.epoch = int64_t(llround(d)); return true;
      case BOOL: v.flag = d != 0; return true;
      case TEXT: serializeJson(j, v.text); return true;
    }
  }
  if (t == TEXT) { serializeJson(j, v.text); return true; }  // an object or a list, as JSON
  return false;
}
// Follow "a.b[0].c" from the payload's root. The build checked the syntax.
inline JsonVariantConst walk(JsonVariantConst v, const char *p) {
  std::string key;
  while (*p) {
    if (*p == '.') { p++; continue; }
    if (*p == '[') {
      size_t n = strtoul(p + 1, nullptr, 10);
      while (*p && *p != ']') p++;
      if (*p) p++;
      v = v[n];
      continue;
    }
    key.clear();
    while (*p && *p != '.' && *p != '[') key += *p++;
    v = v[key];
  }
  return v;
}
inline bool same(Type t, const Value &a, const Value &b) {
  if (a.valid != b.valid) return false;
  if (!a.valid) return true;
  switch (t) {
    case NUMBER: return a.number == b.number;
    case EPOCH: return a.epoch == b.epoch;
    case BOOL: return a.flag == b.flag;
    case TEXT: return a.text == b.text;
  }
  return false;
}
inline std::string show(Type t, const Value &v) {
  if (!v.valid) return "(none)";
  char b[32];
  switch (t) {
    case NUMBER: snprintf(b, sizeof b, "%g", v.number); return b;
    case EPOCH: snprintf(b, sizeof b, "%lld", (long long) v.epoch); return b;
    case BOOL: return v.flag ? "true" : "false";
    case TEXT: return "\"" + v.text + "\"";
  }
  return "";
}
// Store a new value; mark its page dirty only when the value really changed.
inline bool put(int s, Value &nv) {
  const Def &d = store.defs[s];
  Value &cur = store.values[s];
  if (nv.valid) nv.at = millis();
  else nv.at = cur.at;
  bool changed = !same(d.type, cur, nv);
  if (changed) {
    ESP_LOGD("cyd", "SLOT %s.%s = %s", d.page, d.name, show(d.type, nv).c_str());
    store.dirty |= 1u << d.page_idx;
  }
  cur = nv;
  return changed;
}

// ---- the feed: when data last arrived (bound topics and a page's own subscription alike) ----
}  // namespace cyd_slots

namespace cyd_feed {
enum State { WAITING = 0, FRESH = 1, STALE = 2 };
static bool received = false;
static uint32_t seq = 0;  // the payload's sequence number, when a page reports one
static int64_t ts = -1;   // the payload's own timestamp (epoch s), or -1 to use the arrival time
static uint32_t at = 0;   // millis() of the last arrival
inline void note(int64_t payload_ts) {
  received = true;
  ts = payload_ts;
  at = millis();
}
inline int state_of(const std::string &data_state) {
  return data_state == "waiting" ? WAITING : (data_state == "stale" ? STALE : FRESH);
}
}  // namespace cyd_feed

namespace cyd_slots {

// At boot, from generated/cyd_slots.h: literals (and manifest defaults) are valid from here on.
inline void begin(const Def *defs, Value *values, int count, const char *const *topics, int topic_count) {
  store.defs = defs;
  store.values = values;
  store.count = count;
  store.topics = topics;
  store.topic_count = topic_count;
  for (int s = 0; s < count; s++) {
    if (defs[s].source != LITERAL) continue;
    Value v;
    v.valid = from_text(defs[s].type, defs[s].literal, v);
    put(s, v);
  }
}

// One MQTT message on bound topic number `topic`: parse once, fill every slot bound to it.
inline void on_message(int topic, const std::string &payload) {
  if (topic < 0 || topic >= store.topic_count) return;
  bool want_json = false;
  int bound = 0, filled = 0;
  for (int s = 0; s < store.count; s++)
    if (store.defs[s].topic == topic) { bound++; want_json |= store.defs[s].source == JSON; }
  JsonDocument doc;
  bool parsed = false;
  if (want_json) {
    DeserializationError err = deserializeJson(doc, payload);
    parsed = !err;
    if (err) ESP_LOGW("cyd", "SLOTS topic=%s: the payload is not JSON (%s)", store.topics[topic], err.c_str());
  }
  JsonVariantConst root = doc.as<JsonVariantConst>();
  for (int s = 0; s < store.count; s++) {
    const Def &d = store.defs[s];
    if (d.topic != topic) continue;
    Value nv;
    if (d.source == RAW) nv.valid = from_text(d.type, payload.c_str(), nv);
    else nv.valid = parsed && from_json(d.type, walk(root, d.path), nv);
    put(s, nv);
    if (nv.valid) filled++;
  }
  cyd_feed::note(-1);
  ESP_LOGI("cyd", "SLOTS topic=%s filled=%d/%d", store.topics[topic], filled, bound);
}

// Pages with a changed slot since the last call (bit i = page i).
inline uint32_t take_dirty() {
  uint32_t d = store.dirty;
  store.dirty = 0;
  return d;
}

}  // namespace cyd_slots
