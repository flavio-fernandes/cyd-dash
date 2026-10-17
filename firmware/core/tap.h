#pragma once
// One tap is one tap (C7c). The XPT2046 reports a touch while its pressure reading is over the
// threshold; one sample under it makes ESPHome fire a release and then a fresh touch, so a single
// finger press can arrive as two. Pure decisions, no ESPHome or LVGL, so tests/tap_model_test.cpp
// runs the same code on the host:
//  - a touch-down within BOUNCE_MS of the last release continues that touch (a bounce): it does
//    not re-arm the nav bar, restart the long press or count as a new touch;
//  - the bar's buttons arm only if the bar had been showing for ARM_MS when the touch started;
//  - one touch presses at most one nav button, and a nav click within REPEAT_MS of the last one
//    is ignored, so Pin never toggles twice for one tap.
#include <cstdint>
#include <string>

namespace cyd_tap {

// The touchscreen samples every 50 ms (ESPHome's default update_interval), so a bounce is a gap of
// one or two samples; 150 ms is three. A deliberate second tap needs the finger to lift and come
// back, which takes longer than that (a quick double tap is ~250-300 ms between taps).
static const uint32_t BOUNCE_MS = 150;
static const uint32_t ARM_MS = 300;
static const uint32_t REPEAT_MS = 400;

struct Tap {
  uint32_t touches = 0;   // touches that started (bounces excluded)
  uint32_t bounces = 0;   // touch-downs merged into the touch before
  uint32_t navs = 0;      // nav actions done, from a touch or a command
  uint32_t ignored = 0;   // nav clicks dropped: a second one in the same touch or within REPEAT_MS
  uint32_t unarmed = 0;   // nav clicks on a bar that was not armed (the first tap only shows it)
  uint32_t gap_min = 0;   // shortest release-to-touch gap seen, ms (0 = none yet)
  std::string last = "none", src = "none";  // the last nav action and where it came from
  uint32_t last_at = 0;   // millis() of that action

  bool down = false, pending = false;  // finger on; released and the bounce window still open
  bool armed = false, acted = false;   // this touch may press a nav button; it already did
  uint32_t start = 0, released = 0;    // millis() of this touch's first touch-down, of the last release
  bool clicked = false, lifted = false;  // a nav click acted before; a release happened before
  uint32_t click_at = 0;               // millis() of the last nav click that acted

  // A touch-down. Returns true when it is a bounce (the caller continues the touch it had).
  bool touch(uint32_t now, bool bar_visible, uint32_t bar_since) {
    uint32_t gap = now - released;
    if (lifted && !down && (gap_min == 0 || gap < gap_min)) gap_min = gap ? gap : 1;
    if (pending && gap < BOUNCE_MS) {
      pending = false; down = true; bounces++;
      return true;
    }
    pending = false; down = true; touches++; start = now; acted = false;
    armed = bar_visible && now - bar_since >= ARM_MS;
    return false;
  }
  // The finger lifted (maybe only for a bounce: settled() says when it is final).
  void release(uint32_t now) { down = false; pending = lifted = true; released = now; }
  // True once, when the last release is BOUNCE_MS old with no touch since: the touch has ended.
  bool settled(uint32_t now) {
    if (!pending || now - released < BOUNCE_MS) return false;
    pending = false;
    return true;
  }
  // A long press never presses a nav button.
  void long_press() { armed = false; }
  // A nav button's click (LVGL, on release). True when it should act.
  bool click(uint32_t now) {
    if (!armed) { unarmed++; return false; }
    if (acted || (clicked && now - click_at < REPEAT_MS)) { ignored++; return false; }
    acted = clicked = true; click_at = now;
    return true;
  }
  // A nav action happened (from_touch: a nav button; otherwise a command).
  void nav(const std::string &what, bool from_touch, uint32_t now) {
    navs++; last = what; src = from_touch ? "touch" : "cmd"; last_at = now;
  }
};

// The nav action a command is, or "" when it is none (for the NAV log line and `touch.last`).
inline std::string nav_name(const std::string &cmd) {
  if (cmd == "auto on") return "pin->auto";
  if (cmd == "auto off") return "auto->pin";
  if (cmd == "next" || cmd == "prev" || cmd == "sleep" || cmd.rfind("page ", 0) == 0) return cmd;
  return "";
}

inline Tap state;

}  // namespace cyd_tap
