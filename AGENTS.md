# AGENTS.md

Notes for coding agents working on cyd-dash: firmware for the ESP32-2432S028 "CYD" (320x240
touch screen) built with ESPHome and LVGL, showing values from an MQTT broker.

The [docs](docs/README.md) are the source of truth. This file is the map: where things are,
the commands, how to check your work when no person is watching, and the rules that keep the
CYD smooth. When this file and a doc disagree, the doc wins: please fix this file.

## Repo map

```text
firmware/cyd-dash.yaml      the device build (the `marker` substitution names a build)
firmware/preview.yaml       the desktop preview build (ESPHome host platform + SDL)
firmware/core/              shared core: hardware, nav, status, commands, calibration,
                            slots.h (the slot API and page hooks), pages.h
firmware/pages/<name>/      one folder per page: manifest.yaml, page.yaml, <name>.h
firmware/pages/_template/   the page to copy
firmware/*.example.yaml     example site, bindings and secrets files
firmware/generated/         written by tools/cyd_build.py; never edit, never commit
tools/                      cyd_build.py, cyd_preview.py, cyd_mqtt.py, publish_example.py
tools/examples/             example feeds (JSON lists of MQTT messages)
docs/                       the user and page-author docs; docs/pages.md is generated
tests/                      python3 -m unittest discover -s tests -v
```

Your own `firmware/site.yaml`, `firmware/bindings.yaml` and `firmware/secrets.yaml` are
gitignored. Never commit them, or broker addresses, passwords or firmware images.

## Commands

| what | command | more |
|---|---|---|
| tests | `python3 -m unittest discover -s tests -v` | [Run the tests](CONTRIBUTING.md#run-the-tests) |
| plan + generate | `python3 tools/cyd_build.py prepare` | [The build plan](docs/configure.md#the-build-plan) |
| compile | `python3 tools/cyd_build.py compile` | [Install and flash](docs/install.md) |
| flash (USB, then Wi-Fi) | `python3 tools/cyd_build.py run --device /dev/ttyUSB0` | [First flash](docs/install.md#the-first-flash-over-usb), [over Wi-Fi](docs/install.md#updates-over-wi-fi) |
| logs | `esphome logs firmware/cyd-dash.yaml` | [Reading the logs](docs/install.md#reading-the-logs) |
| page reference | `python3 tools/cyd_build.py docs` | [Page inputs](docs/pages.md) |
| preview window | `python3 tools/cyd_preview.py run --feed tools/examples/usage.json` | [The desktop preview](docs/preview.md) |
| screenshot | `python3 tools/cyd_preview.py shot --page NAME --feed FEED --out NAME.png` | [Options](docs/preview.md#options) |
| send a command | `python3 tools/cyd_mqtt.py --broker BROKER cmd 'page history'` | [Commands](docs/mqtt.md#commands) |
| check status | `python3 tools/cyd_mqtt.py --broker BROKER status --expect page=history --wait 30` | [Checking from a script](docs/mqtt.md#checking-from-a-script) |
| sample data | `python3 tools/publish_example.py --broker BROKER --once` | [Publishing](docs/usage.md#publishing-to-an-mqtt-broker) |

`--site` and `--bindings` choose other files; a one-page preview site is shown in
[Select and preview your page](docs/add-a-page.md#4-select-and-preview-your-page).
`run --device` takes the CYD's serial port or, after the first flash, its network address
(`cyd-dash.local`); without it ESPHome asks in a menu, which an agent cannot answer. Never
flash a board you have not identified. No broker yet? Start from
[Try Hello World first](docs/install.md#try-hello-world-first-no-broker-needed).

## Check your work with no one watching

1. **Tests**: all pass. They check the generator, the manifests, `docs/pages.md`, the links
   and the tone of the docs.
2. **The build plan**: `prepare` prints it. Every slot you expect is bound (`←` or `=`), and
   the build did not stop ([When the build stops](docs/configure.md#when-the-build-stops)).
3. **Preview screenshots**: take them with an example feed, then open the PNGs and look at
   them. Check the waiting, fresh and stale states, clipping, and that the text is readable.
4. **Compile** for the CYD, with your page alone and with the others.
5. **On a device**, if you have one: change the `marker` in `firmware/cyd-dash.yaml`, flash,
   then send commands and wait for the status that proves them:
   `status --expect 'marker="4821"'`, then `cmd 'page NAME'` and
   `status --expect page=NAME`. Feed values with `tools/publish_example.py`.
   [Checking from a script](docs/mqtt.md#checking-from-a-script) has the details.

Say in your pull request which of these you ran.

## Rules that keep the CYD smooth

The numbers and the reasons are in
[Keep it smooth and readable](docs/ui-guidelines.md).

- Fonts: 48 px digits-only hero, 32 px main value, 20 px secondary, 16 px the floor.
  A page's own font goes in its `page.yaml`, never in the manifest.
- Text contrast at least 4.5:1 on the background `0x101820`; colour is never the only signal.
- Build widgets once in `build`; in `render`, update a widget only when its value changed.
- Animate only from the shared 100 ms clock (`tick10`), and only while the page is visible.
  No free-running `lv_anim`.
- Keep redrawn areas small: a style change redraws the whole object.
- Pages are not scrollable: clear `LV_OBJ_FLAG_SCROLLABLE` on the root and every container,
  and leave a 3 px margin for pixel drift.

## Never break these

- **Saved settings keep their id and type.** ESPHome restores saved values by id:
  `touch_cal` (`std::array<uint16_t, 5>`, the user's touch calibration) above all, then
  `boots`, `page_idx`, `auto_mode`, `rotation_interval`, `drift_enabled`, `saver_after`,
  `brightness`. Renaming or retyping one silently loses the user's value.
- **Every page header is in its manifest's `headers` list**, in dependency order, with a
  name unique across pages ([Describe the page](docs/add-a-page.md#2-describe-the-page)).
- **A slot is never silently zero.** A missing, null or invalid value shows as waiting; check
  `cyd_slots::valid(...)` or `std::isfinite(...)` before using it
  ([Value types](docs/configure.md#value-types)).
- **Commands are never retained** on the `cmd` topic.
- `docs/pages.md` is generated: change the manifest, then run `cyd_build.py docs`.

## Definition of done for a page pull request

- [ ] `firmware/pages/<name>/` with a manifest: title, one-line description, and a friendly
      `doc` for every slot.
- [ ] `docs/pages.md` regenerated with `python3 tools/cyd_build.py docs`.
- [ ] An example bindings block, for example `firmware/bindings.<name>.example.yaml`.
- [ ] An example feed, `tools/examples/<name>.json` (more feeds for the waiting and stale
      states help), and `tools/publish_example.py` sends sensible values for the page
      (a `<name>_sample` function; the generic values rarely fit).
- [ ] A preview image, `docs/images/<name>.png`, under about 100 KB (the preview saves
      about 230 KB: shrink it as shown in [Add a page](docs/add-a-page.md#7-regenerate-the-reference-and-propose-your-change)), looked at.
- [ ] Fields from a `status` hook described in [the MQTT reference](docs/mqtt.md#fields).
- [ ] The tests pass.
- [ ] A clean compile with the page alone and with other pages.
- [ ] On a device, if you have one: the page shown by command and confirmed by status.

The steps are in [Add a page](docs/add-a-page.md) and
[Propose a page](CONTRIBUTING.md#propose-a-page).

## Writing

Docs, comments and log messages are read by people new to ESPHome. Lead with an example, then
explain, in short steps and plain words. Avoid words that make a reader feel slow, such as
`simple` or `obviously`: the tests flag them. More in
[Writing style](CONTRIBUTING.md#writing-style).
