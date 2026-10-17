#pragma once
// The desktop preview's driver (core/preview.yaml): options from the environment, the feed
// file, and PNG screenshots of the SDL window. tools/cyd_preview.py sets the options:
//
//   CYD_FEED=feed.txt        one message per line: topic, a tab, the payload
//   CYD_PAGE=usage           show this page (the `page usage` command)
//   CYD_CMDS="drift off;..." more commands, as on the device's cmd topic
//   CYD_SHOT=usage.png       save a screenshot, then exit
//   CYD_SHOT_DELAY=1500      milliseconds between the feed and the first screenshot
//   CYD_FRAMES=1             screenshots 100 ms apart (usage-000.png, usage-001.png, ...)
#include "esphome.h"
#include <SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace cyd_preview {

struct Options {
  std::string feed, shot;
  std::vector<std::string> commands;
  int delay_ms = 1500;
  int frames = 1;
};

inline std::string env(const char *name) {
  const char *v = getenv(name);
  return v ? v : "";
}

inline const Options &options() {
  static Options o = [] {
    Options o;
    o.feed = env("CYD_FEED");
    o.shot = env("CYD_SHOT");
    if (!env("CYD_PAGE").empty()) o.commands.push_back("page " + env("CYD_PAGE"));
    std::string cmds = env("CYD_CMDS");
    size_t at = 0;
    while (at <= cmds.size()) {
      size_t end = cmds.find(';', at);
      if (end == std::string::npos) end = cmds.size();
      std::string c = cmds.substr(at, end - at);
      c.erase(0, c.find_first_not_of(' '));
      c.erase(c.find_last_not_of(' ') + 1);
      if (!c.empty()) o.commands.push_back(c);
      at = end + 1;
    }
    if (!env("CYD_SHOT_DELAY").empty()) o.delay_ms = std::max(0, atoi(env("CYD_SHOT_DELAY").c_str()));
    if (!env("CYD_FRAMES").empty()) o.frames = std::min(200, std::max(1, atoi(env("CYD_FRAMES").c_str())));
    return o;
  }();
  return o;
}

// The feed: "topic<TAB>payload" per line.
inline std::vector<std::pair<std::string, std::string>> read_feed(const std::string &path) {
  std::vector<std::pair<std::string, std::string>> out;
  if (path.empty()) return out;
  std::ifstream in(path);
  if (!in) {
    ESP_LOGE("preview", "cannot read the feed %s", path.c_str());
    return out;
  }
  std::string line;
  while (std::getline(in, line)) {
    size_t tab = line.find('\t');
    if (tab == std::string::npos || tab == 0) continue;
    out.emplace_back(line.substr(0, tab), line.substr(tab + 1));
  }
  ESP_LOGI("preview", "FEED %u messages from %s", unsigned(out.size()), path.c_str());
  return out;
}

inline std::string frame_path(const std::string &path, int frame, int frames) {
  if (frames <= 1) return path;
  char n[8];
  snprintf(n, sizeof n, "-%03d", frame);
  size_t dot = path.rfind('.');
  if (dot == std::string::npos || path.find('/', dot) != std::string::npos) return path + n;
  return path.substr(0, dot) + n + path.substr(dot);
}

// A PNG with no compression library: zlib "stored" blocks, which every PNG reader accepts.
inline uint32_t crc32(const std::string &s) {
  uint32_t c = 0xFFFFFFFFu;
  for (unsigned char ch : s) {
    c ^= ch;
    for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
  }
  return c ^ 0xFFFFFFFFu;
}
inline void put32(std::string &s, uint32_t v) {
  s += char(v >> 24);
  s += char(v >> 16);
  s += char(v >> 8);
  s += char(v);
}
inline void chunk(std::string &png, const char *type, const std::string &data) {
  put32(png, data.size());
  std::string body = std::string(type, 4) + data;
  png += body;
  put32(png, crc32(body));
}
inline bool write_png(const std::string &path, const uint8_t *rgb, int w, int h) {
  std::string raw;
  for (int y = 0; y < h; y++) {
    raw += char(0);  // filter: none
    raw.append(reinterpret_cast<const char *>(rgb) + size_t(y) * w * 3, size_t(w) * 3);
  }
  std::string z("\x78\x01", 2);
  uint32_t a = 1, b = 0;
  for (unsigned char ch : raw) {
    a = (a + ch) % 65521;
    b = (b + a) % 65521;
  }
  for (size_t at = 0;;) {
    size_t n = std::min<size_t>(65535, raw.size() - at);
    bool last = at + n >= raw.size();
    z += char(last ? 1 : 0);
    z += char(n & 0xFF);
    z += char(n >> 8);
    z += char(~n & 0xFF);
    z += char((~n >> 8) & 0xFF);
    z.append(raw, at, n);
    at += n;
    if (last) break;
  }
  put32(z, (b << 16) | a);
  std::string ihdr;
  put32(ihdr, w);
  put32(ihdr, h);
  ihdr += std::string("\x08\x02\x00\x00\x00", 5);  // 8 bits, RGB
  std::string png("\x89PNG\r\n\x1a\n", 8);
  chunk(png, "IHDR", ihdr);
  chunk(png, "IDAT", z);
  chunk(png, "IEND", "");
  FILE *f = fopen(path.c_str(), "wb");
  if (!f) return false;
  bool ok = fwrite(png.data(), 1, png.size(), f) == png.size();
  return fclose(f) == 0 && ok;
}

// What the window shows right now, as a PNG.
inline bool save_png(const std::string &path) {
  SDL_Window *win = nullptr;
  for (uint32_t id = 1; id < 16 && win == nullptr; id++) win = SDL_GetWindowFromID(id);
  SDL_Renderer *r = win ? SDL_GetRenderer(win) : nullptr;
  if (r == nullptr) {
    ESP_LOGE("preview", "no SDL window to take a screenshot of");
    return false;
  }
  int w = 0, h = 0;
  if (SDL_GetRendererOutputSize(r, &w, &h) != 0 || w <= 0 || h <= 0) return false;
  std::vector<uint8_t> rgb(size_t(w) * h * 3);
  if (SDL_RenderReadPixels(r, nullptr, SDL_PIXELFORMAT_RGB24, rgb.data(), w * 3) != 0) {
    ESP_LOGE("preview", "SDL_RenderReadPixels: %s", SDL_GetError());
    return false;
  }
  if (!write_png(path, rgb.data(), w, h)) {
    ESP_LOGE("preview", "cannot write %s", path.c_str());
    return false;
  }
  return true;
}

}  // namespace cyd_preview
