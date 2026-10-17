# Install and flash

This page takes you from a new CYD to one showing your values. The
[README's quick start](../README.md#quick-start) is the short version.

## What you need

- **A CYD**: the ESP32-2432S028 board, with a 2.8" ILI9341 screen (320x240) and XPT2046 touch.
- **A USB data cable.** Some cables only charge; if no serial port appears, try another.
- **Wi-Fi** at 2.4 GHz, and for your own values an **MQTT broker** that the CYD can reach.
  [Hello World](#try-hello-world-first-no-broker-needed) needs no broker.
- **Python 3** with PyYAML, and **ESPHome** 2026.9 or newer.

## ESPHome with pip

On Ubuntu, install `python3-venv` first (`sudo apt install python3-venv`).
Use a virtual environment rather than changing the system Python:

```bash
python3 -m venv .venv
. .venv/bin/activate
pip install esphome paho-mqtt pyyaml
esphome version
```

Activate `.venv` again in each new terminal. Container users can run the Python tools
inside the image too; this avoids needing PyYAML on the host.

`paho-mqtt` is only for the example tools (`tools/publish_example.py`, `tools/cyd_mqtt.py`).

## ESPHome with Docker

The ESPHome image can do every step, so your computer needs neither ESPHome nor PyYAML. From
the repository's top folder, prepare the build, then compile and flash it:

```bash
docker run --rm -v "$PWD:/config" -w /config --entrypoint python3 ghcr.io/esphome/esphome tools/cyd_build.py prepare
docker run --rm -v "$PWD:/config" --device=/dev/ttyUSB0 ghcr.io/esphome/esphome run firmware/cyd-dash.yaml --device /dev/ttyUSB0
```

The first `--device` lets the container see the CYD's port; the second tells ESPHome to upload
there without asking. Change both to your CYD's port. No CYD plugged in yet? Use `compile`
instead of `run` and leave out both `--device` options: ESPHome then only checks that
everything builds. Run `prepare` again after every change to `site.yaml` or `bindings.yaml`.
`podman` takes the same commands.

## Your settings

```bash
cp firmware/site.example.yaml firmware/site.yaml
cp firmware/secrets.example.yaml firmware/secrets.yaml
cat firmware/bindings.example.yaml firmware/bindings.thermostat.example.yaml firmware/bindings.history.example.yaml > firmware/bindings.yaml
```

These are the same three steps as the quick start. The three bindings examples together cover
the usage, detail, thermostat and history pages; the build skips bindings for pages you do not
list in `site.yaml`.

- `site.yaml`: your Wi-Fi, broker, topics, time zone and pages.
- `secrets.yaml`: the password for updates over Wi-Fi (`ota_password`). Pick a long one.
- `bindings.yaml`: where each page gets its values.

[Configure](configure.md) explains every key. Git ignores all three copies.

## Try Hello World first (no broker needed)

The Hello World and device pages need no broker and no bindings, so they make a good first
build. Write these three files instead of the copies above.

`firmware/site.yaml`:

```yaml
substitutions:
  device_name: cyd-dash
  wifi_ssid: "your-ssid"              # your 2.4 GHz network
  wifi_password: ""                   # fill in yourself; empty for an open network
  mqtt_broker: mqtt.example.com       # a placeholder: these pages need no broker
  mqtt_port: "1883"
  topic_prefix: cyd/dash
  mqtt_reboot_timeout: 0s             # no broker: never restart while waiting for one
  timezone: America/New_York          # yours, for example Europe/Berlin

cyd:
  pages: [hello, device]
  rotation_interval: 20
```

`firmware/bindings.yaml`, with no bindings:

```yaml
{}
```

`firmware/secrets.yaml`, with a new random password for updates over Wi-Fi. This command
writes it straight into the file, so it never shows on screen or in a chat:

```bash
python3 -c "import secrets; print('ota_password: \"' + secrets.token_urlsafe(16) + '\"')" > firmware/secrets.yaml
```

Then look at it in the [desktop preview](preview.md) and flash it as shown below:

```bash
python3 tools/cyd_preview.py shot --all --feed tools/examples/hello.json --out-dir shots
python3 tools/cyd_build.py run --device /dev/ttyUSB0
```

Hello World shows a greeting, your local time and a small dot. Give Wi-Fi and the network
clock time to connect; the log's `Synchronized time` line confirms that the clock is ready.
The pages take turns every 20 seconds. If this CYD was used before, it remembers its pinned
mode: tap once for the touch bar, then tap **Pin** to resume rotation.

The device page then shows `MQTT down for …`, and the log shows MQTT connection
warnings; both are expected. When you have a
broker, put its address in `mqtt_broker`, delete the `mqtt_reboot_timeout` line and add pages
([No broker yet](configure.md#no-broker-yet)).

## The first flash, over USB

Plug the CYD in and find its serial port:

- **Linux:** `ls /dev/serial/by-id/`, for example `/dev/ttyUSB0`. Unplug the CYD and list
  again: the entry that goes away is the CYD.
- **macOS:** `ls /dev/cu.*`, for example `/dev/cu.usbserial-110`.
- **Windows:** Device Manager, under "Ports (COM & LPT)", for example `COM3`.

Then build and upload to that port:

```bash
python3 tools/cyd_build.py run --device /dev/ttyUSB0
```

It prints the build plan, compiles (a few minutes the first time), uploads, then shows the
CYD's log. Without `--device`, ESPHome asks where to upload and waits for your answer.

## Updates over Wi-Fi

Once the CYD is on your network, upload to its network address instead: its `device_name`
with `.local`, or its IP.

```bash
python3 tools/cyd_build.py run --device cyd-dash.local
```

The upload uses the `ota_password` from `secrets.yaml`.

## Reading the logs

`esphome logs firmware/cyd-dash.yaml` shows the log over USB or Wi-Fi. A healthy start looks
like this:

```text
[I][main]: CYD-DASH marker=4821 pages=history,thermostat,hello,device rotation=20
```

- `marker` is a number from `firmware/cyd-dash.yaml` (the `marker` substitution). Change it
  before a build and you can tell at a glance that the new firmware is running.
- `pages` is your page list, in order.
- `rotation` is the number of seconds between pages in Auto mode.

Other lines you may see: `CMD next` for every command or button, `SCREEN 0 -> 1` when the
screen changes, `CALIB ...` during touch calibration, and `RESOURCES uptime=... heap=...`
once a minute.

Right after the first line comes `IDENTITY git=… reset_reason=… boots=…`: the git revision
the firmware was built from (with `-dirty` when it had uncommitted changes, a new `marker`
among them), why the CYD started, and how many times it has. Opening the
USB serial port restarts the CYD, so a USB log always begins with a fresh boot.
[The board](hardware.md#restarts-and-the-reset-reason) explains each reset reason.

## Common problems

- **No serial port, or the upload fails to connect.** Try another cable or USB port. On
  Linux, add yourself to the `dialout` group (then log in again). If it still fails, hold the
  **BOOT** button on the CYD while the upload starts.
- **A different board variant.** Some CYDs use other pins or another display chip. The pins
  live in `firmware/core/hardware.yaml`: compare them with your board's documentation.
- **The colours look swapped** (blue where you expect red). Your panel wants the other colour
  order: in `firmware/core/hardware.yaml`, change `color_order: rgb` to `color_order: bgr`.
- **Touches land in the wrong place.** Calibrate the touch screen: hold a finger on the screen
  for 5 seconds (see [Using your CYD](using-your-cyd.md#touch-calibration)).
- **The pages say "waiting".** The CYD has not had a value yet. Check the broker address in
  `site.yaml` and the topics in `bindings.yaml`, then send sample values with
  `python3 tools/publish_example.py --broker YOUR-BROKER --once`.
- **The build stops before compiling.** Read the message under "the build cannot start yet":
  it names the file, the line and what to add. [Configure](configure.md#when-the-build-stops)
  lists the messages.
- **The broker needs a login.** The firmware connects without a username for now. To add one,
  put `username:` and `password: !secret mqtt_password` under `mqtt:` in
  `firmware/core/mqtt.yaml`, and the password in `secrets.yaml`.
