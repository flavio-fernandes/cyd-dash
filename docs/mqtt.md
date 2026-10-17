# MQTT reference

The CYD has three topics of its own under `topic_prefix` from your `site.yaml` (`cyd/dash` in
the examples):

| topic | direction | retained |
|---|---|---|
| `cyd/dash/status` | the CYD tells you how it is | yes |
| `cyd/dash/cmd` | you tell the CYD what to do | never |
| `cyd/dash/calib` | touch calibration readings and results | no |

`tools/cyd_mqtt.py` talks to all three:

```bash
python3 tools/cyd_mqtt.py --broker mqtt.example.com status
python3 tools/cyd_mqtt.py --broker mqtt.example.com cmd 'page thermostat'
python3 tools/cyd_mqtt.py --broker mqtt.example.com status --expect page=thermostat --wait 10
```

Use `--prefix` for another `topic_prefix`, `--port` and `--tls` for another port or TLS, and
the `MQTT_USER` and `MQTT_PASSWORD` environment variables for a login.
[Usage](usage.md#publishing-to-an-mqtt-broker) has more examples.

## Status

The CYD publishes a JSON status, retained, when it connects, once a minute, and after every
command, page change or change of data state:

```json
{"online": true, "marker": "4821", "state": "fresh", "page": "thermostat", "mode": "pin",
 "screen": "on", "interval": 20, "uptime": 5321, "rssi": -58, "...": "..."}
```

When the CYD connects, it first publishes `{"online":true,"marker":"…","state":"waiting"}`. If
it drops off the network, the broker publishes its last will, also retained:

```json
{"online": false}
```

### Fields

| field | meaning |
|---|---|
| `online` | `true` while connected; `false` in the last-will message |
| `marker` | the `marker` substitution from `firmware/cyd-dash.yaml`, to recognise a build |
| `git` | the git revision the firmware was built from (`-dirty` with local changes) |
| `reset_reason` | why the CYD last started, for example after power-on or an update |
| `boots` | how many times it has started |
| `state` | `waiting` (no value yet), `fresh` or `stale` (see [Using your CYD](using-your-cyd.md#waiting-and-stale-values)) |
| `seq` | counts the messages received on bound topics |
| `msg_age` | seconds since the last value arrived |
| `page`, `page_idx` | the current page's name and its position in `cyd.pages` (from 0) |
| `pages` | the build's page list |
| `mode` | `auto` (rotating) or `pin` |
| `interval` | seconds between pages in Auto mode |
| `screen` | `on`, `saver` or `sleep` |
| `idle` | seconds since the last touch or command |
| `overlay` | `true` while the nav bar is showing |
| `drift`, `drift_x`, `drift_y` | pixel drift on or off, and the current offset in pixels |
| `saver_after` | seconds without a touch before the screensaver starts (when pinned) |
| `saver_steps` | how many steps the screensaver has moved |
| `calib_step` | `-1` outside calibration, `0`–`3` for the cross, `4` on the test screen |
| `calib`, `calib_flags` | the touch calibration in use: raw `[x_min, x_max, y_min, y_max]` and the axis flags |
| `calib_result` | the last calibration result (`none`, `pending`, `ok`, or why it was not saved) |
| `heap`, `heap_min` | free memory now and the lowest since start-up, in bytes |
| `uptime` | seconds since start-up |
| `reconnects` | how many times the broker connection came back after a drop |
| `rssi` | Wi-Fi signal strength in dBm |
| `refr_n`, `refr_ms`, `refr_max_ms` | screen redraws in the last window, their average and longest time in ms |
| `busy` | the share of time spent drawing, in percent |
| `inv_px` | the average number of pixels redrawn per refresh |
| `touch` | touch and nav evidence, an object: `touches` (taps and presses since start-up), `bounces` (touch-downs within 150 ms of a release, merged into the touch before), `gap_min` (the shortest release-to-touch gap seen, in ms; 0 = none yet), `navs` (nav actions: page, Pin, Sleep, from the bar or a command), `ignored` (a second nav click in one touch or within 400 ms of the last, dropped), `unarmed` (clicks on a bar that had been showing for under 300 ms, dropped), `last` (the last nav action: `next`, `prev`, `page <name>`, `pin->auto`, `auto->pin`, `sleep` or `none`), `src` (`touch` or `cmd`) and `last_at` (its uptime in seconds) |

Some pages add fields of their own while they are in your build:

| field | page | meaning |
|---|---|---|
| `history` | history | an object: `state`, `samples` (readings kept), `buffer_bytes`, `motion` (`sweep`, `pulse`, `slide`, `waiting` or `still`) and `motion_frames` |
| `thermostat` | thermostat | an object: `state` |
| `stat` | stat | an object: `state` (`waiting`, `fresh` or `stale`) and `level` (`none`, `normal`, `warn` or `alert`) |
| `glow_steps`, `glow_opa`, `band_steps` | usage | animation counters of the gauges |
| `pulse`, `pulse_steps` | usage | over the cap: each gauge's breathing fill opacity (0 when not breathing), and the breath steps so far |

## Commands

Send a command as the plain-text payload of `<prefix>/cmd`. Each one acts exactly like its
button, and counts as a touch for the screensaver.

| command | what it does |
|---|---|
| `next`, `prev` | the next or previous page; pins it |
| `page NAME` | show that page, for example `page history`; pins it |
| `auto on`, `auto off` | rotate the pages, or pin the current one |
| `interval N` | seconds between pages in Auto mode, 5 to 600 |
| `overlay` | show the nav bar for 6 seconds |
| `sleep`, `wake` | screen off; screen on (also leaves the screensaver and ends calibration) |
| `saver` | start the screensaver now |
| `saver_after N` | seconds without a touch before the screensaver, 30 to 86400 |
| `brightness N` | backlight in percent, 10 to 100 |
| `drift on`, `drift off` | pixel drift on or off |
| `drift_period N` | seconds between drift steps, 10 to 300 |
| `calibrate` | start touch calibration |
| `selftest` | run the pages' and the calibration's self-tests; results go to the log |

These help when testing calibration without a finger on the screen; the results go to the log
and to `<prefix>/calib`:

| command | what it does |
|---|---|
| `press_sim N` | a touch held for N ms (100 to 20000), as a long press |
| `calib_sim X Y` | where a raw touch reading `X Y` lands with the calibration in use |
| `calib_tap X Y` | a calibration tap with the raw reading `X Y` |

An unknown command or a number out of range changes nothing; the log shows
`Unsupported/invalid command: …`.

## Calibration messages

During calibration the CYD publishes to `<prefix>/calib`: one message per cross
(`corner`, `x`, `y`, `raw_x`, `raw_y`), then the result (`result`, `calib`, `flags`).
`calib_sim` publishes `sim`, `raw_x`, `raw_y`, `x` and `y`.

## Checking from a script

Send a command, then wait for a status that shows it worked:

```bash
python3 tools/cyd_mqtt.py --broker BROKER cmd 'page history'
python3 tools/cyd_mqtt.py --broker BROKER status --expect page=history --wait 30
```

`status --expect` exits with 0 as soon as a status matches every `--expect`, and with an error
when none does within `--wait` seconds.

- Each value is read as JSON when it can be: `interval=20` is a number, `drift=true` a
  boolean, `page=history` text.
- `marker` is text in the status, so quote it: `--expect 'marker="4821"'`. Without the
  quotes, `4821` is a number and never matches.
- Dots reach into objects: `--expect history.state=fresh`.
- The status is retained, so the first one you receive can be old. Expect a field your command
  changes, and after an update, expect the new `marker`.
- Commands are never retained. `cyd_mqtt.py` sends them that way; with another MQTT client,
  leave its retain flag off, or the CYD would repeat the command after every reconnect.

### Testing calibration without changing it

This walks through calibration and the **Done** button while keeping the saved calibration:

```bash
python3 tools/cyd_mqtt.py --broker BROKER status                     # note "calib"
python3 tools/cyd_mqtt.py --broker BROKER cmd calibrate
python3 tools/cyd_mqtt.py --broker BROKER cmd 'calib_tap 1000 1000'  # send this four times
python3 tools/cyd_mqtt.py --broker BROKER cmd 'calib_sim 3400 2000'  # where does it land?
python3 tools/cyd_mqtt.py --broker BROKER cmd 'calib_tap 3400 2000'  # a reading that lands on Done
python3 tools/cyd_mqtt.py --broker BROKER status --expect calib_step=-1 --wait 10
```

Four taps on the same spot are rejected (`rejected: spread x=0 y=0`), so the previous
calibration stays and the test screen opens. `calib_sim` shows where a raw reading lands
(on `<prefix>/calib` and in the log); change it until it lands on **Done**, bottom centre,
around `x=160 y=210`. Then tap it with `calib_tap`. At the end, `calib` must equal the value
you noted.

- `press_sim 5200` acts as a 5.2 s press: the two hints, then calibration starts. `wake`
  ends it with `aborted (wake)`.
- `selftest` runs each page's self-test and the calibration solver's; look for `PASS` in the
  log.
