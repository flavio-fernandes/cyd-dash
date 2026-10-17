# The board

cyd-dash runs on the ESP32-2432S028, often sold as the "Cheap Yellow Display" (CYD): an ESP32
with a 2.8" ILI9341 screen (320x240, used in landscape) and an XPT2046 resistive touch
controller. Everything board-specific lives in `firmware/core/hardware.yaml`.

## Pins

| part | pins |
|---|---|
| display (SPI) | CLK `GPIO14`, MOSI `GPIO13`, MISO `GPIO12`, CS `GPIO15`, DC `GPIO2` |
| backlight (PWM) | `GPIO21` |
| touch (its own SPI bus) | CLK `GPIO25`, MOSI `GPIO32`, MISO `GPIO39`, CS `GPIO33`, IRQ `GPIO36` |

## Colours

```yaml
color_order: rgb        # in firmware/core/hardware.yaml
invert_colors: false
```

If you see blue where you expect red, your panel wants `color_order: bgr`. If the picture
looks like a photo negative, try `invert_colors: true`.

Phone cameras often shift the hues of this panel (amber and red can both come out blue). Judge
colours by eye, or in a [preview screenshot](preview.md), which holds the exact colours the
firmware asks for.

## Touch

The XPT2046 reports its axes swapped compared with the landscape screen, so the touch
configuration uses `transform: {swap_xy: true}` and gives its calibration along the swapped
axes. Until the first calibration, the CYD uses the defaults from the substitutions in
`firmware/cyd-dash.yaml`:

```yaml
touch_x_min: "342"
touch_x_max: "3547"
touch_y_min: "223"
touch_y_max: "3785"
touch_flags: "1"        # 1 = axes swapped
```

Every board reads a little differently. Hold a finger on the screen for 5 seconds to
calibrate ([Touch calibration](using-your-cyd.md#touch-calibration)). The result is saved in
the `touch_cal` setting and kept across restarts and updates, as long as that setting keeps its
name and type.

## Restarts and the reset reason

The log's `IDENTITY` line and the status field `reset_reason` say why the CYD last started:

| reset_reason | what happened |
|---|---|
| `power-on` | power came on, the EN (reset) button was pressed, or a serial port was opened |
| `software` | the firmware restarted itself, for example after an update over Wi-Fi |
| `panic`, `task-wdt`, `int-wdt`, `wdt` | the firmware crashed or hung: worth a look at the log |
| `brownout` | the supply voltage dipped: try another cable or USB port |

Two things about this board surprise people:

- **The EN button reports `power-on`**, not `reset-pin`. So `power-on` is the normal value
  after any restart you caused by hand.
- **Opening the USB serial port restarts the CYD.** The USB chip's control lines are wired to
  the ESP32's reset, so `esphome logs` over USB always starts with a fresh boot. To watch a CYD
  without restarting it, read the logs over Wi-Fi, or its [status](mqtt.md#status).

`boots` in the status counts start-ups. On a healthy CYD it goes up by one for each restart you
caused, and stays the same while it runs.

## Other board variants

Boards sold under the same name differ: some have a second USB port, a different display
controller, or other pins. Compare the pins above with your board's documentation and change
`firmware/core/hardware.yaml` to match. [Install and flash](install.md#common-problems) lists
the usual symptoms.
