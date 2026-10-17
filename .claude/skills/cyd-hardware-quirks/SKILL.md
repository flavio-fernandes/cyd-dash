---
name: cyd-hardware-quirks
description: Use when a CYD shows wrong colours, touches land in the wrong place, the board restarts unexpectedly, the reset reason looks odd, or the board is a different variant. Covers colour order, the touch axis swap and calibration, reset behaviour and pins.
---

# CYD hardware quirks

All of these are in [The board](../../../docs/hardware.md); the board's settings live in
`firmware/core/hardware.yaml`.

- **Colours**: `color_order: rgb` on this panel; blue where red belongs means `bgr`
  ([Colours](../../../docs/hardware.md#colours)). Cameras shift this panel's hues: judge colour
  in a preview screenshot.
- **Touch axes are swapped**: the XPT2046 needs `swap_xy: true`, and its calibration is given
  along the swapped axes ([Touch](../../../docs/hardware.md#touch)).
- **Calibration**: a 5 s long press (or the `calibrate` command) starts it; the result lives in
  the `touch_cal` setting, which must keep its id and type
  ([Touch calibration](../../../docs/using-your-cyd.md#touch-calibration)).
- **The EN button reports `power-on`**, and **opening the USB serial port restarts the board**
  ([Restarts and the reset reason](../../../docs/hardware.md#restarts-and-the-reset-reason)).
  To watch a running CYD, use Wi-Fi logs or the status.
- **Pins and variants**: [Pins](../../../docs/hardware.md#pins),
  [Other board variants](../../../docs/hardware.md#other-board-variants).

When changing a pin or the touch transform, change one thing at a time and check the result on
the screen; a wrong touch setting makes the nav bar hard to reach.
