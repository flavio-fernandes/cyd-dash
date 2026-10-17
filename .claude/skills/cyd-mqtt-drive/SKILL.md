---
name: cyd-mqtt-drive
description: Use when driving or checking a running CYD from a script with no finger on the screen - switching pages, asserting status fields, simulating touches and long presses, or testing calibration without changing the saved one.
---

# Drive a CYD over MQTT

The topics, every status field and every command: [MQTT reference](../../../docs/mqtt.md).

## The loop: command, then expected status

```bash
python3 tools/cyd_mqtt.py --broker BROKER cmd 'page history'
python3 tools/cyd_mqtt.py --broker BROKER status --expect page=history --wait 30
```

Exit code 0 means a status matched. The rules
([Checking from a script](../../../docs/mqtt.md#checking-from-a-script)):

- values are read as JSON: `interval=20` is a number, `page=history` text;
- `marker` is text, so quote it: `--expect 'marker="1397"'`;
- dots reach into objects: `--expect history.state=fresh`;
- the status is retained: expect a field your command changes;
- **never retain a command**. `cyd_mqtt.py` never does; with other clients, leave retain off.

Add `--prefix` when the site's `topic_prefix` is not `cyd/dash`, and `MQTT_USER` /
`MQTT_PASSWORD` for a login. `tools/publish_example.py --once` feeds sample values.

## Test-only commands

| command | use |
|---|---|
| `press_sim N` | a touch held N ms; `press_sim 5200` starts calibration like a 5 s press |
| `calib_sim X Y` | where a raw reading lands, without touching anything |
| `calib_tap X Y` | a calibration tap with that raw reading |
| `selftest` | page and calibration self-tests; look for `PASS` in the log |

Their details are under [Commands](../../../docs/mqtt.md#commands).

## Calibration's Done, keeping the user's calibration

Note `calib` from the status, send `calibrate`, then `calib_tap 1000 1000` four times (it is
rejected, so nothing is saved). Find a raw reading that lands on Done with `calib_sim`, tap it
with `calib_tap`, then expect `calib_step=-1` and the same `calib` as before
([Testing calibration without changing it](../../../docs/mqtt.md#testing-calibration-without-changing-it)).
`wake` ends calibration at any step.
