# The desktop preview

See your pages on your computer before (or without) flashing a CYD, and save true-colour PNG
screenshots of them.

```bash
python3 tools/cyd_preview.py run --feed tools/examples/usage.json
```

This example feeds the usage page, which `site.example.yaml` lists. If your `site.yaml` lists
other pages, send their feeds instead: `tools/examples/` has one for every page, for example
`history-fresh.json` or `thermostat-comfortable.json`.

A 320x240 window opens with the same firmware the CYD runs: the same core, page registry and
pages, fed with the example messages. Mouse clicks act as touches, so the nav bar, the arrows
and the long press all work.

To save screenshots instead, with no window at all:

```bash
python3 tools/cyd_preview.py shot --page usage --feed tools/examples/usage.json --out usage.png
python3 tools/cyd_preview.py shot --all --feed tools/examples/usage.json --out-dir shots
```

Each page becomes a 320x240 PNG with the colours the firmware asks for: amber stays amber,
whatever a camera or a panel would make of it.

## What you need

- **ESPHome** 2026.9 or newer, with its `esphome` command on your PATH
  (`pip install esphome`).
- **SDL2** with its development files:
  - Debian or Ubuntu: `sudo apt install libsdl2-dev`
  - Fedora: `sudo dnf install SDL2-devel`
  - macOS (Homebrew): `brew install sdl2`
- **Python 3**. Pillow is optional: with it, `--frames` also writes an animated GIF.

The first build takes a few minutes; later ones reuse most of it.

## Or use a container

If you would rather not install SDL2, the ESPHome image plus SDL2 works too. From the
repository's top folder:

```bash
podman build -t cyd-preview -f tools/preview/Containerfile tools/preview
podman run --rm -v "$PWD:/repo" -w /repo --entrypoint python3 cyd-preview \
  tools/cyd_preview.py shot --all --feed tools/examples/usage.json --out-dir shots
```

`docker` takes the same commands. Screenshots work in the container because they need no
screen. For the window (`run`), use your own ESPHome and SDL2.

## Feeding values

A feed is a JSON list of messages. Each one goes through the same code as an MQTT message on
the device (the generated `cyd_deliver` script), so your bindings work the same way in both:

```json
[
  {"topic": "home/livingroom/climate", "payload": {"temperature": 21.5, "humidity": 48}},
  {"topic": "home/livingroom/heating", "payload": "on"},
  {"topic": "${usage_topic}", "payload": {"gauges": [{"label": "Plan A", "used_pct": 38, "resets_at": "now+3600"}]}}
]
```

- A payload is text, or JSON (sent in its compact form).
- `"now"`, `"now+3600"` and `"now-60"` anywhere in a payload become the current Unix time,
  plus or minus that many seconds. Countdowns and "updated 0m ago" then look right.
- `${name}` in a topic is a substitution from your `site.yaml`, such as `${usage_topic}`.
- You need no MQTT broker for the preview.
- All the messages of a feed arrive together, at start-up, so a page sees the last value
  for each topic. A change from one value to the next (an easing bar, a trend arrow)
  cannot be captured from a feed: check it on a device.
- A feed cannot make a value arrive in the past. To show a stale state, give the page an
  `updated_at` slot (type `epoch`) and send `"updated_at": "now-1500"`, as
  `tools/examples/thermostat-stale.json` does.

`tools/examples/usage.json` is a full example for the usage and detail pages.

## Options

| option | what it does |
|---|---|
| `--page NAME` | the page to show (`run`) or to capture (`shot`) |
| `--all` | `shot`: every page of the build, as `<page>.png` in `--out-dir` |
| `--feed FILE` | the messages to send after start-up |
| `--cmd "drift off; brightness 50"` | commands after the feed, the same ones the device takes on its `cmd` topic |
| `--frames N` | `shot`: N frames 100 ms apart (`usage-000.png`, ...), plus a GIF with Pillow |
| `--delay MS` | `shot`: wait this long after the feed before the first frame (1500 by default) |
| `--site FILE`, `--bindings FILE` | another `site.yaml` or `bindings.yaml` |
| `--no-build` | reuse the last preview build |
| `-v` | print the preview's log |

Without a `firmware/site.yaml`, the preview uses `site.example.yaml`, so it works on a fresh
clone.

## How it works

`firmware/preview.yaml` is the preview's entry point, next to `firmware/cyd-dash.yaml`. It uses
ESPHome's `host` platform with an `sdl` display, and swaps three device parts for
`core/preview.yaml`:

| on the CYD | in the preview |
|---|---|
| `core/hardware.yaml`: ILI9341 panel, XPT2046 touch, backlight | an SDL window; mouse clicks are touches; the backlight does nothing |
| `core/net.yaml`: Wi-Fi, clock from the network | no Wi-Fi; the clock is your computer's |
| `core/mqtt.yaml` and `generated/mqtt.yaml`: the broker | no broker; the feed file sends the messages |

Everything else is shared: `core/boot.yaml`, `ui.yaml`, `status.yaml`, `saver.yaml`,
`calib.yaml`, the registry and every page. `tools/cyd_build.py prepare` writes
`generated/site_preview.yaml` for it: your `site.yaml` substitutions without the network and
secret ones.

The screenshot is taken inside the preview (`core/preview.h`): it reads the window's pixels,
writes a PNG and exits. `tools/cyd_preview.py` sets it up through environment variables, which
you can also set yourself: `CYD_FEED`, `CYD_PAGE`, `CYD_CMDS`, `CYD_SHOT`, `CYD_SHOT_DELAY` and
`CYD_FRAMES` (the top of `core/preview.h` explains each one).

## What looks different from the device

- The device page shows `heap n/a (preview)` and `MQTT preview feed`.
- Its uptime counts from the preview program's start-up.
- The touch calibration is not applied: a click is already a screen position.
- Settings the device keeps across restarts (the page, brightness, intervals) are kept by
  ESPHome on your computer too.

## When something goes wrong

- **`sdl2-config: not found`** or **`SDL.h: No such file`**: SDL2's development files are
  missing (see What you need).
- **`the topic uses ${...}, which your site does not define`**: add that substitution to your
  `site.yaml`, or write the topic out in the feed.
- **`No page reads the topic ...`** in the log: no slot is bound to that topic and no page
  lists it. Check the topic in your feed against `bindings.yaml`.
- **A blank or half-drawn screenshot**: give the page more time, for example `--delay 3000`.
