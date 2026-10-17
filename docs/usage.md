# Usage gauges and example messages

Start with `firmware/bindings.example.yaml` and `tools/examples/usage.json`:

```bash
python3 tools/cyd_preview.py shot --page usage --feed tools/examples/usage.json --bindings firmware/bindings.example.yaml --out usage.png
```

The page displays one or two labelled gauges. Bind `gauge1_label`, `gauge1_used_pct`
and `gauge1_resets_at` to your own JSON paths, raw topics or fixed values. The
`gauges` array in the example is a suggested payload, not a required format.

For one centred gauge, remove every `gauge2_` binding. If you bind any second-gauge
field, its label, used percentage and reset epoch must all be bound. An incomplete
second gauge is rejected before compiling; missing runtime values show waiting.

## What the colours mean

With a positive cap, the five-hour gauge is green below 80% of that cap,
amber from 80% up to the cap, and red at or above it. For example, **19% used
with a 15% cap is red**: usage is over the cap, even though it is below 100%.
The white tick on the ring marks the cap.

Over the cap, the whole red fill breathes: its opacity eases down to about 58%
and back in a calm 2 s cycle, in place of the bright glow at the end of the fill.
`limit_reached` breathes too. Amber and green gauges never breathe, and the
track and the cap tick stay still. Over the cap, the cap tick and the breath say
why the gauge is red; a hard limit below the cap says it in words (see
[Hard limit](#hard-limit)).

A donated allowance (a cap above its base, or a positive donation) makes the cap
zone and tick violet; the colours follow that effective cap. With no cap bound,
there is no tick and usage stays green; a zero cap applies no colour thresholds.
Stale readings turn grey and never breathe, and neither do snoozed (dimmed) gauges.

## Fields

Each gauge has these slots, prefixed with `gauge1_` or `gauge2_`:

| field | meaning |
|---|---|
| `label` | required heading; long headings are shortened with an ellipsis |
| `used_pct` | required five-hour usage percentage |
| `resets_at` | required reset time, Unix epoch seconds |
| `weekly_pct`, `weekly_resets_at` | optional weekly ring and reset time |
| `cap_pct` | effective five-hour cap; white tick, amber at 80% of cap, red at cap |
| `base_cap_pct`, `donated_pct` | a higher effective cap or positive donation highlights the cap in violet |
| `snoozed` | paused chip and dim rings |
| `auth` | `ok`, or another value for the AUTH warning; omit if not applicable |
| `inferred` | dashed five-hour ring and `0%*` |
| `updated_at` | optional feed update epoch; older updates make data stale |

Weekly usage is green when no weekly cap is bound. Missing optional values are
not converted to zero. Without a known reset or a valid clock, countdowns show `--`.
Required slots determine freshness per gauge: after 20 minutes without refreshed
values the gauge turns grey. Unrelated messages cannot make a gauge fresh.
`updated_at` can make data older but cannot hide old required-slot arrivals.

Detail reuses the usage slots; include both pages in either order in `cyd.pages`.
Detail owns its font holders, so it does not depend on which page builds first.
A detail-only build is rejected with a message asking you to include usage.
The device page has no input slots. The saver shows the selected gauge count when
usage is included, or a clock when it is not. Both run from the shared animation clock.

## Hard limit

```yaml
usage:
  gauge1_limit_reached: {topic: home/example/usage, json: 'gauges[0].limit_reached'}
```

A true value makes the five-hour fill and saver ring red and breathing even below
its cap. False or unbound keeps the usual cap rule. Paused, old and stale gauges
never breathe. The weekly ring never breathes.

A hard limit always comes with words, so you can tell why a gauge at 38% is red.
Usage at or over the cap does not show them: the cap tick and the breathing fill
already tell that story.

- **Usage page:** a red-outlined `limit` chip (16 px) sits in the chip row under
  the gauge. It hides while the gauge is paused (the `paused` chip shows instead)
  or stale (the gauge is grey). When `AUTH`, `limit` and an age chip would all
  show, the row is too narrow for three, so the age chip steps aside; the rings
  stay dim to say the numbers are old.
- **Detail page:** the cap line reads `limit reached` in red. `paused` still wins
  over it, and it wins over `donated cap` and `donated until …`.
- **Saver:** only the red ring; the saver stays a quiet screen.

## Measurement age

```yaml
usage:
  gauge1_measured_at: {topic: home/example/usage, json: 'gauges[0].measured_at'}
  gauge1_old_after: {value: 1800}
```

`measured_at` is when the numbers were measured, in Unix seconds. It is separate
from `updated_at`, the feed update time: an alive feed can repeat old numbers.
After more than `old_after` seconds (1800 by default), both rings dim and a
secondary-grey, 16 px chip shows `35m old` or `2h05 old`. The saver ring dims too.
Age is checked once a minute and when these inputs change. Stale grey takes
priority and hides the age chip. Without a measurement time, nothing changes.
A missing or negative age threshold uses 1800; a future measurement is not old.

## Weekly allowance

```yaml
usage:
  gauge1_weekly_cap_pct: {topic: home/example/usage, json: 'gauges[0].weekly_cap_pct'}
  gauge1_weekly_base_cap_pct: {topic: home/example/usage, json: 'gauges[0].weekly_base_cap_pct'}
  gauge1_weekly_donated_pct: {topic: home/example/usage, json: 'gauges[0].weekly_donated_pct'}
```

The weekly ring and `wk` text use the same cap thresholds as the five-hour ring:
green below 80% of the cap, amber from 80%, red at the cap. A cap adds a tick;
a donation makes the allowance violet and adds the violet `+`. Weekly red stays
steady, without breathing. Unbound weekly inputs keep the existing green ring.

## Donation end

```yaml
usage:
  gauge1_donated_until: {topic: home/example/usage, json: 'gauges[0].donated_until'}
```

While a donation is active, detail shows `until 18:00` if it ends today, or
`donated until Tue` for another day, using your configured local timezone.
At that epoch both windows use their base caps and lose the violet highlight,
without another message. Bind both base caps so the expired allowance has a
known replacement; an unknown base becomes an unknown cap. No end time keeps
the existing donation behaviour. An unavailable clock postpones expiry.

All these slots also work with the `gauge2_` prefix for Plan B.

## Example states

`tools/examples/` includes `fresh`, `waiting`, `stale`, `cap`, `donated`, `snoozed`,
`auth`, `inferred` and `one` JSON feeds. Use the `one` feed with one-gauge bindings.
For a saver screenshot, add `--cmd 'drift off; saver'` to a preview shot command.

For the richer inputs, copy the example bindings and uncomment the optional
`gauge1_` and `gauge2_` lines. Try `usage-limit.json` (hard limit below the cap),
`usage-old.json` (2h05 and 35m measurements), `usage-weekly.json` (weekly amber
with a donation), and `usage-donated.json` (active and expired donations).
Use `--bindings firmware/bindings.yaml` and select usage, detail or the saver.

## Publishing to an MQTT broker

The tools work with `paho-mqtt` 1.6 and 2, including ESPHome's environment.
Install `paho-mqtt>=1.6` and PyYAML if needed, then choose test topics in your bindings:

```bash
python3 tools/publish_example.py --broker BROKER --once
python3 tools/publish_example.py --broker BROKER --interval 5
python3 tools/cyd_mqtt.py --broker BROKER --prefix cyd/dash cmd 'page usage'
python3 tools/cyd_mqtt.py --broker BROKER --prefix cyd/dash status --expect page=usage --wait 10
python3 tools/cyd_mqtt.py --broker BROKER publish home/example/value 42
```

The sample publisher walks the build's bindings, so every bound page gets sample
values. Fixed values need no publication; a device-only build publishes nothing.
Changing mode varies numeric values gently and refreshes timestamps. These are
examples, not measurements. Mixed raw/JSON bindings on one topic are rejected by
the sample publisher because they have no unambiguous sample representation.

`MQTT_USER` and `MQTT_PASSWORD` supply authentication without command-line secrets.
Use `--tls` and the broker's TLS port for encrypted transport. Publications are
not retained unless `cyd_mqtt.py publish --retain` is requested. Commands are never
retained, including a `publish` aimed at a topic ending in `/cmd`.

Status expectations accept dotted keys and JSON scalar values, for example
`--expect online=true`. A timeout or connection error exits nonzero. Status may
come from a retained message; it is not proof of a new command being processed.

## Optional groups for page authors

A manifest can declare an optional bundle of slots:

```yaml
optional_groups:
  second:
    required: [second_label, second_value]
    members: [second_label, second_value, second_unit]
```

If any member has an explicit binding, every slot in `required` needs an explicit
binding too. All names must refer to declared slots. Omit all members to disable
the bundle. The page decides the resulting layout.
