#!/usr/bin/env python3
"""MQTT commands, status assertions and data publishing. Commands are never retained.

python3 tools/cyd_mqtt.py --broker BROKER --prefix cyd/dash cmd 'page usage'
python3 tools/cyd_mqtt.py --broker BROKER status --expect screen=on --wait 10
Authentication: MQTT_USER and MQTT_PASSWORD environment variables. Use --tls for TLS.
"""
import argparse
import json
import os
import threading
import sys


def options(parser):
    parser.add_argument('--broker', required=True)
    parser.add_argument('--port', type=int, default=1883)
    parser.add_argument('--prefix', default='cyd/dash')
    parser.add_argument('--tls', action='store_true')


def client(args):
    import paho.mqtt.client as mqtt
    c = (mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
         if hasattr(mqtt, "CallbackAPIVersion") else mqtt.Client())
    if os.environ.get('MQTT_USER'):
        c.username_pw_set(os.environ['MQTT_USER'], os.environ.get('MQTT_PASSWORD'))
    if args.tls:
        c.tls_set()
    return c


def expectations(items):
    result = {}
    for item in items:
        key, sep, value = item.partition('=')
        if not sep or not key:
            raise ValueError('--expect needs k=v')
        try:
            value = json.loads(value)
        except ValueError:
            pass
        result[key] = value
    return result


def matches(value, expected):
    for path, wanted in expected.items():
        current = value
        for key in path.split('.'):
            if not isinstance(current, dict) or key not in current:
                return False
            current = current[key]
        if current != wanted:
            return False
    return True


def send(c, topic, payload, retain=False):
    # A publish aimed at a command topic gets the same safety rule as cmd.
    if topic.rstrip('/').endswith('/cmd'):
        retain = False
    info = c.publish(topic, payload, qos=1, retain=retain)
    info.wait_for_publish(timeout=10)
    if not info.is_published():
        raise TimeoutError('publish was not acknowledged within 10 seconds')


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    options(p)
    sub = p.add_subparsers(dest='action', required=True)
    cmd = sub.add_parser('cmd'); cmd.add_argument('command')
    status = sub.add_parser('status')
    status.add_argument('--expect', action='append', default=[])
    status.add_argument('--wait', type=float, default=10)
    pub = sub.add_parser('publish')
    pub.add_argument('topic'); pub.add_argument('payload'); pub.add_argument('--retain', action='store_true')
    args = p.parse_args(argv)
    c = None
    try:
        expected = expectations(args.expect) if args.action == 'status' else {}
        if args.action == 'status' and args.wait <= 0:
            raise ValueError('--wait must be positive')
        c = client(args)
        ready, done = threading.Event(), threading.Event()
        error = []
        def connected(c, userdata, flags, reason, properties=None):
            if getattr(reason, "is_failure", reason != 0):
                error.append('connection refused'); done.set()
            elif args.action == 'status':
                c.subscribe(args.prefix.rstrip('/') + '/status', qos=1)
            ready.set()
        def message(c, userdata, msg):
            try:
                data = json.loads(msg.payload)
            except (ValueError, UnicodeError):
                return
            if isinstance(data, dict) and matches(data, expected):
                print(json.dumps(data, sort_keys=True), flush=True); done.set()
        c.on_connect, c.on_message = connected, message
        c.connect(args.broker, args.port, keepalive=30); c.loop_start()
        if not ready.wait(10) or error:
            raise TimeoutError('MQTT connection did not succeed')
        if args.action == 'status':
            if not done.wait(args.wait):
                raise TimeoutError('no matching status within the requested wait')
        elif args.action == 'cmd':
            send(c, args.prefix.rstrip('/') + '/cmd', args.command, False)
            print('command acknowledged')
        else:
            send(c, args.topic, args.payload, args.retain)
            print('publish acknowledged')
        return 0
    except (OSError, RuntimeError, ValueError, ImportError) as e:
        print('cyd-mqtt: ' + str(e), file=sys.stderr); return 2
    finally:
        if c is not None:
            c.disconnect(); c.loop_stop()


if __name__ == '__main__':
    sys.exit(main())
