---
name: cyd-bindings
description: Use when connecting cyd-dash pages to MQTT topics in bindings.yaml ("make it show my sensor"), or when a slot stays empty, shows waiting, or the build stops on a binding. Covers the binding syntax, value types, the build plan and debugging.
---

# Bindings: where each value comes from

`firmware/bindings.yaml` has one block per page and one line per slot
([bindings.yaml](../../../docs/configure.md#bindingsyaml)):

```yaml
thermostat:
  temperature: {topic: home/livingroom/climate, json: temperature}   # a JSON field
  humidity: {topic: home/livingroom/humidity}                        # the whole payload
  label: {value: Living room}                                        # a fixed value
```

- The three sources: [The three kinds of source](../../../docs/configure.md#the-three-kinds-of-source).
- Paths with dots and `[n]`: [JSON paths](../../../docs/configure.md#json-paths).
- What each type accepts (`number`, `text`, `bool`, `epoch`):
  [Value types](../../../docs/configure.md#value-types).
- Each page's slots, types and defaults: [Page inputs](../../../docs/pages.md).

## Read the build plan

`python3 tools/cyd_build.py prepare` prints the plan
([The build plan](../../../docs/configure.md#the-build-plan)): `←` from MQTT, `=` a fixed
value, `(not bound)` an optional slot left out. If the build stops, the message names the file
and line ([When the build stops](../../../docs/configure.md#when-the-build-stops)).

## A slot that never fills

1. Is it bound? Look for it in the build plan, and for `note:` lines about skipped pages.
2. Does the topic match exactly, including case and slashes?
3. Does the path match the payload? Missing or `null` fields leave the slot empty by design.
4. Does the value fit the type? `"21.5"` is a number; `"n/a"` is not.
5. Try it without a device: put the real payload into a feed and take a preview screenshot
   ([Try your settings without a CYD](../../../docs/configure.md#try-your-settings-without-a-cyd)).
6. On a device: publish a test value (`python3 tools/publish_example.py --broker BROKER --once`,
   or `tools/cyd_mqtt.py ... publish`), then read `seq`, `msg_age` and `state` in the status
   ([Fields](../../../docs/mqtt.md#fields)) and the log.

The user's broker and topics are private: keep them in the gitignored `bindings.yaml`, and use
neutral topics such as `home/livingroom/temperature` in examples.
