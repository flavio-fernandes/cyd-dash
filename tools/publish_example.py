#!/usr/bin/env python3
"""Publish sample values for every bound page, once or with gentle changes every five seconds.

python3 tools/publish_example.py --broker BROKER --once
python3 tools/publish_example.py --broker BROKER --interval 5
Uses site.yaml and bindings.yaml. Install paho-mqtt>=1.6 and PyYAML.
Choose test topics in your bindings before publishing; values are examples, not measurements.
"""
import argparse
import json
import math
from pathlib import Path
import sys
import time
import cyd_build
import cyd_mqtt
from cyd_preview import SUB


# The history page's fixed inputs, if your bindings give them a topic.
HISTORY = {'label': 'Living room', 'unit': '°C', 'warn': 25, 'alert': 28, 'window': 7200, 'period': 60}
# The thermostat's fixed inputs, the same way.
THERMOSTAT = {'label': 'Living room', 'unit': '°C', 'setpoint': 22}
# The big-number page's fixed inputs: a home's power use in watts.
STAT = {'label': 'House power', 'unit': 'W', 'warn': 3000, 'alert': 5000, 'decimals': 0}


def room_temperature(t):
    """A living room over a day: a slow two-hour swing, a gentler wave and a little wobble."""
    return round(21.5 + 1.2 * math.sin(2 * math.pi * t / 7200) + 0.3 * math.sin(2 * math.pi * t / 1300)
                 + 0.05 * math.sin(t / 37), 1)


def history_sample(name, step, now):
    if name == 'value':
        return room_temperature(now)
    if name == 'seed':
        # Once, at the start: the last two hours, one reading a minute, ending with the live value.
        return [room_temperature(now - 60 * i) for i in range(119, -1, -1)] if step == 0 else None
    return HISTORY.get(name)  # min and max stay unset: the page scales itself


def thermostat_sample(name, step, now):
    if name == 'temperature':
        return round(21.5 + 0.6 * math.sin(step / 12), 1)
    if name == 'humidity':
        return round(47 + 3 * math.sin(step / 17))
    if name == 'updated_at':
        return now
    return THERMOSTAT.get(name)


def stat_sample(name, step, now):
    if name == 'value':
        # A home's power use: a calm base load with the kettle and oven coming and going.
        return round(1800 + 900 * math.sin(step / 12) + 400 * math.sin(step / 5))
    if name == 'updated_at':
        return now
    return STAT.get(name)


def sample(slot, step, now):
    name, kind = slot['slot'], slot['type']
    if slot['page'] == 'history':
        return history_sample(name, step, now)
    if slot['page'] == 'thermostat':
        return thermostat_sample(name, step, now)
    if slot['page'] == 'stat':
        return stat_sample(name, step, now)
    if kind == 'epoch':
        return now if name.endswith(('updated_at', 'measured_at')) else now + (302400 if 'weekly' in name else 7800)
    if kind == 'bool':
        return False
    if kind == 'text':
        return 'ok' if name.endswith('auth') else 'Plan B' if name.startswith('gauge2') else 'Plan A'
    if name.endswith('old_after'):
        return 1800
    if 'donated' in name:
        return 0
    if 'cap_pct' in name:
        return 90
    return round(38 + 4 * math.sin(step / 12), 1)


def assign(root, parts, value):
    current = root
    for i, key in enumerate(parts):
        last = i == len(parts) - 1
        if isinstance(key, int):
            if not isinstance(current, list):
                raise ValueError('bindings have conflicting JSON paths')
            while len(current) <= key:
                current.append(None)
        elif not isinstance(current, dict):
            raise ValueError('bindings have conflicting JSON paths')
        if last:
            current[key] = value
        else:
            child = current[key] if isinstance(current, list) else current.get(key)
            if child is None:
                child = [] if isinstance(parts[i + 1], int) else {}
                current[key] = child
            current = child


def messages(plan, step=0, now=None):
    now = int(time.time()) if now is None else now
    result, shapes = {}, {}
    def topic_name(topic):
        def replace(m):
            value = plan['substitutions'].get(m.group(1) or m.group(2))
            if not isinstance(value, (str, int, float)):
                raise ValueError('topic substitution is missing or is not plain text')
            return str(value)
        return SUB.sub(replace, topic)
    for slot in plan['slots']:
        if slot['source'] not in ('raw', 'json'):
            continue
        topic = topic_name(slot['topic'])
        if topic in shapes and shapes[topic] != slot['source']:
            raise ValueError('sample publisher cannot mix raw and JSON slots on one topic')
        shapes[topic] = slot['source']
        value = sample(slot, step, now)
        if value is None:
            continue
        if slot['source'] == 'raw':
            if isinstance(value, list):
                result[topic] = json.dumps(value, separators=(',', ':'))
            else:
                result[topic] = str(value).lower() if isinstance(value, bool) else str(value)
        else:
            parts = cyd_build.parse_path(slot['path'])
            if topic not in result:
                result[topic] = [] if isinstance(parts[0], int) else {}
            assign(result[topic], parts, value)
    return [(topic, json.dumps(value, separators=(',', ':')) if shapes[topic] == 'json' else value)
            for topic, value in result.items()]


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    cyd_mqtt.options(p)
    p.add_argument('--once', action='store_true')
    p.add_argument('--interval', type=float, default=5)
    p.add_argument('--site', default=str(cyd_build.FIRMWARE / 'site.yaml'))
    p.add_argument('--bindings', default=str(cyd_build.FIRMWARE / 'bindings.yaml'))
    args = p.parse_args(argv)
    if args.interval < 1:
        p.error('--interval must be at least 1 second')
    c = None
    try:
        plan = cyd_build.plan(args.site, args.bindings)
        for page in plan['pages']:
            if not page['slots'] and not page['requires']:
                print('%s: this page has no inputs, so there is nothing to send for it.' % page['name'])
        if not messages(plan):
            print('No bound inputs in this build; there is nothing to publish.'); return 0
        c = cyd_mqtt.client(args)
        c.connect(args.broker, args.port, keepalive=30); c.loop_start()
        step = 0
        while True:
            for topic, payload in messages(plan, step):
                cyd_mqtt.send(c, topic, payload, False)
                print('published ' + topic, flush=True)
            if args.once:
                return 0
            step += 1; time.sleep(args.interval)
    except KeyboardInterrupt:
        return 0
    except (OSError, RuntimeError, ValueError, ImportError, cyd_build.BuildError) as e:
        print('publish-example: ' + str(e), file=sys.stderr); return 2
    finally:
        if c is not None:
            c.disconnect(); c.loop_stop()


if __name__ == '__main__':
    sys.exit(main())
