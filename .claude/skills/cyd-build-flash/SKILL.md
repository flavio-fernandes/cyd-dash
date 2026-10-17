---
name: cyd-build-flash
description: Use when compiling cyd-dash firmware, flashing a CYD over USB or Wi-Fi, reading its boot log, or checking that a new build is running and healthy.
---

# Build, flash and confirm

1. **Change the `marker`** in `firmware/cyd-dash.yaml` to a new 4-digit number, so the new
   build is recognisable everywhere ([Reading the logs](../../../docs/install.md#reading-the-logs)).
2. **Compile**: `python3 tools/cyd_build.py compile`. It prints the build plan first
   ([The build plan](../../../docs/configure.md#the-build-plan)).
3. **Flash**: `python3 tools/cyd_build.py run --device /dev/ttyUSB0` for the first flash
   over USB ([finding the port](../../../docs/install.md#the-first-flash-over-usb)), or
   `--device cyd-dash.local` (the `device_name`, or its IP) later
   ([Updates over Wi-Fi](../../../docs/install.md#updates-over-wi-fi)). Without `--device`
   ESPHome asks where to upload and waits for a keypress. Only flash a board you have identified.
4. **Read the boot log**: `esphome logs firmware/cyd-dash.yaml`. Look for
   `CYD-DASH marker=<yours> pages=...` and `IDENTITY git=... reset_reason=... boots=...`.
5. **Confirm by status**:
   `python3 tools/cyd_mqtt.py --broker BROKER status --expect 'marker="<yours>"' --wait 300`
   (the quotes keep the marker text;
   [Checking from a script](../../../docs/mqtt.md#checking-from-a-script)). No broker yet
   ([Hello World](../../../docs/install.md#try-hello-world-first-no-broker-needed))? The boot
   log from step 4 is the check; `MQTT down for …` on the device page is expected.

## A healthy boot

- `reset_reason` is `power-on` for any restart by hand: the EN button and opening the serial
  port both report it. `panic`, a `wdt` reason or `brownout` mean trouble
  ([Restarts and the reset reason](../../../docs/hardware.md#restarts-and-the-reset-reason)).
- `boots` goes up once per restart you caused, then stays steady while the CYD runs. A count
  that keeps climbing means a reset loop.
- `state` reaches `fresh` once values arrive, and `calib` is the same as before the update.
- Opening the USB log restarts the board, so read a running CYD over Wi-Fi or by status.

Problems and fixes: [Common problems](../../../docs/install.md#common-problems).
