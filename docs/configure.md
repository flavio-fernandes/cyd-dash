# Configure

Two files hold your settings. Both stay on your computer: git ignores them.

- `firmware/site.yaml`: your device, your network and your pages.
- `firmware/bindings.yaml`: where each page gets its values.

`python3 tools/cyd_build.py prepare` (and `compile` and `run`, which call it first) reads both,
prints a build plan and writes the files ESPHome needs into `firmware/generated/`.

## site.yaml

Start from the example:

```yaml
substitutions:
  device_name: cyd-dash               # ESPHome node name (also the mDNS name)
  wifi_ssid: "your-ssid"              # a network from which the broker is reachable
  wifi_password: ""                   # empty string for an open network
  mqtt_broker: mqtt.example.com       # DNS name preferred over an IP
  mqtt_port: "1883"
  usage_topic: cyd/usage              # the topic your usage payload is published to
  topic_prefix: cyd/dash              # device status + cmd topics live under this
  timezone: Etc/UTC          # POSIX/IANA zone for the clock shown on screen

cyd:
  pages: [usage, detail, device]
  rotation_interval: 20
```

| key | what it does |
|---|---|
| `device_name` | the CYD's name on your network: updates over Wi-Fi find it as `<device_name>.local` |
| `wifi_ssid`, `wifi_password` | your 2.4 GHz Wi-Fi; an empty password for an open network |
| `mqtt_broker`, `mqtt_port` | your broker's name (or IP) and port, usually `1883` |
| `usage_topic` | a topic name used by the usage example bindings as `${usage_topic}`; add your own substitutions the same way |
| `topic_prefix` | the CYD's own topics: `<prefix>/status`, `<prefix>/cmd` and `<prefix>/calib` ([MQTT reference](mqtt.md)) |
| `timezone` | for the clocks on screen, for example `Europe/Berlin` or `America/New_York` |
| `mqtt_reboot_timeout` | optional: how long the CYD waits for its broker before it restarts, `30min` if you leave it out; `0s` never restarts ([no broker yet](#no-broker-yet)) |
| `cyd.pages` | the pages to show, in order: folder names from `firmware/pages/` |
| `cyd.rotation_interval` | seconds between pages in Auto mode, 5 to 600 |

Any substitution you add here can be used in `bindings.yaml` as `${name}`.

About `rotation_interval`: the `interval N` command changes it on the device straight away.
That change lasts until you build with a different `rotation_interval` in `site.yaml`.

### No broker yet

The Hello World and device pages need no broker. Keep a placeholder in `mqtt_broker` and add
this line under `substitutions:`:

```yaml
  mqtt_reboot_timeout: 0s             # no broker: never restart while waiting for one
```

Without it the CYD restarts every 30 minutes while it cannot reach the broker, which is the
right thing once you have one: a restart often clears a stuck connection. The default lives in
`firmware/core/defaults.yaml`; your `site.yaml` wins over it. When your broker is ready, put its
address in `mqtt_broker` and delete the line. [Try Hello World first](install.md#try-hello-world-first-no-broker-needed)
has a whole broker-free `site.yaml`.

## bindings.yaml

One block per page, one line per input ("slot"). Here is the thermostat example:

```yaml
thermostat:
  temperature: {topic: home/livingroom/climate, json: temperature}
  humidity: {topic: home/livingroom/climate, json: humidity}
  label: {value: Living room}
  unit: {value: '°C'}
  setpoint: {value: 22}
  updated_at: {topic: home/livingroom/climate, json: updated_at}
```

With this, one message feeds four slots:

```json
{"temperature": 21.5, "humidity": 48, "updated_at": 1760000000}
```

[Page inputs](pages.md) lists every page's slots, their types and which ones are required.

### The three kinds of source

| binding | the slot gets |
|---|---|
| `{topic: home/livingroom/climate, json: temperature}` | one field of a JSON message |
| `{topic: home/livingroom/temperature}` | the whole message, for example `21.5` |
| `{value: Living room}` | a fixed value, set at build time; nothing is sent over MQTT |

A slot you leave out uses the page's default, or stays empty if it has none. A required slot
must be bound.

### JSON paths

Dots go between keys, and `[n]` picks an item from a list (counting from 0):

| path | in this message | reads |
|---|---|---|
| `temperature` | `{"temperature": 21.5}` | `21.5` |
| `climate.temperature` | `{"climate": {"temperature": 21.5}}` | `21.5` |
| `sensors[0].temperature` | `{"sensors": [{"temperature": 21.5}]}` | `21.5` |
| `gauges[1].label` | `{"gauges": [{"label": "A"}, {"label": "B"}]}` | `"B"` |
| `[0]` | `[21.5, 48]` | `21.5` |

If a message lacks the field, or the field is `null`, that slot becomes empty again: the page
shows it as missing, never as zero.

### Value types

| type | accepts |
|---|---|
| `number` | a number such as `21.5` or `"21.5"` |
| `text` | any text; numbers and true/false become text |
| `bool` | `true`/`false`, `on`/`off`, `yes`/`no`, `1`/`0` |
| `epoch` | a time in Unix seconds, such as `1760000000` |

### One topic, many slots

Point as many slots as you like at the same topic: the CYD subscribes once and fills each slot
from its own path. The usage example reads its slots from the one `${usage_topic}` message.

## The build plan

Every build starts by printing what it understood:

```text
cyd-dash build plan
  pages (4, Auto rotation every 20 s): history → thermostat → hello → device
  history.value           ← home/livingroom/temperature  (whole payload)  (required)
  history.label           = "Living room"
  history.min             (not bound)
  history.window          = 7200                                          (default)
  thermostat.temperature  ← home/livingroom/climate  json: temperature    (required)
  thermostat.setpoint     = 22
  hello                   (no inputs)
  device                  (no inputs)
  topics subscribed: 3
  note: bindings for "usage" are skipped: it is not in cyd.pages.
```

(Some lines are left out here.)

- `←` a value from MQTT: the topic, then `json:` and the path, or `(whole payload)`.
- `=` a fixed value; `(default)` means the page's own default.
- `(not bound)` an optional slot you left out.
- `note:` something worth knowing that does not stop the build.

## When the build stops

If something needs your attention, the build stops before compiling with
`cyd-dash: the build cannot start yet.` and one line per problem. Each line names the file and
the slot. Some examples:

```text
bindings.yaml, thermostat.humidty: page "thermostat" has no slot "humidty". Did you mean "humidity"? Its slots: temperature, humidity, label, unit, setpoint, updated_at.
```
A typo in a slot name: fix the spelling.

```text
bindings.yaml: page "thermostat" needs a value for "temperature" (number, required). Add, for example:
  thermostat:
    temperature: {topic: home/livingroom/climate, json: temperature}
```
A required slot has no binding: add the line it suggests, with your topic.

```text
bindings.yaml, thermostat.temperature: the JSON path "sensors..temperature" is not valid. Use dots between keys and [n] for list items, for example "plans.main.used_pct" or "sensors[0].temperature".
```
Check the path against the table above.

```text
bindings.yaml, thermostat.setpoint: use either value (a fixed value) or topic (a value from MQTT), not both.
bindings.yaml, thermostat.setpoint: the value 'warm' is not a number.
bindings.yaml, thermostat.setpoint: unknown key "topik". Did you mean "topic"? Use topic, json or value.
```
Each binding is either `{value: ...}` of the slot's type, or `{topic: ...}` with an optional
`json:`.

```text
site.yaml: there is no page "histroy". Did you mean "history"? The pages are: detail, device, hello, history, thermostat, usage.
site.yaml: page "detail" needs page "usage" in cyd.pages too.
```
Fix the page name in `cyd.pages`, or add the page it needs.

## Try your settings without a CYD

```bash
python3 tools/cyd_build.py prepare
python3 tools/cyd_preview.py run --feed tools/examples/thermostat-comfortable.json
```

`prepare` checks both files and prints the plan without compiling. The
[desktop preview](preview.md) then shows your pages with example messages.
