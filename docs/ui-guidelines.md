# Keep it smooth and readable

The rules in [Add a page](add-a-page.md#3-draw-once-update-only-what-changed), with the numbers
behind them. They were measured on a CYD; your page can be checked the same way
([Measure your page](#measure-your-page)).

## Text size

| size | use it for |
|---|---|
| 48 px | a digits-only hero value, such as the thermostat's temperature |
| 32 px | the main value of a page |
| 20 px | secondary text: labels, countdowns, chips |
| 16 px | the floor, for close-range detail such as `updated 3m ago` |

Why: the 2.8" panel has 0.18 mm pixels. A 32 px number is about 4 mm tall, readable across a
room. 20 px reads well at arm's length to about 1 m. 16 px reads only up close, and text
below 16 px was not readable in any test, so never use it for words a person needs.

## Contrast

Text needs a contrast of at least 4.5:1 against the background `0x101820`:

| colour | contrast on `0x101820` | note |
|---|---|---|
| grey `788695` | 4.8:1 | good for secondary text |
| red `F04452` | 4.8:1 | the weakest: use 24 px or more for red text |
| violet `B58AFF`, cyan `42D9E8` | 6.9:1, 10.5:1 | fine for chips |

Green `32D583` and amber `FFB547` are equally bright, so they differ by hue alone. Colour
must never be the only signal: add a word, a number or a shape.

## Draw once, update what changed

Create every widget once, in the page's `build` hook. In `render`, compare with the value on
screen and call a setter only when it changed. Every setter call marks part of the screen for
redrawing, even with the same value.

## Animation

Every page animates from the one shared 100 ms clock (the `tick10` hook), so all motion
together stays at 10 steps per second or fewer, and only while the page is on screen.

| what moves | cost (share of time spent drawing) |
|---|---|
| a free-running `lv_anim` on an always-visible arc | 27 % |
| one 128 px arc changing a style at 10 Hz | 24 % (about 27 ms per step) |
| the same arc at 5 Hz | 13 % |
| a 320x8 band moving at 10 Hz | 11 % |
| a 40x40 block moving at 10 Hz | 5 % |

- Never start a free-running `lv_anim`: it redraws at full speed forever, whether or not
  anything is visible.
- A style change redraws the object's whole bounding box. Keep animated objects small, and
  step a large arc at 5 Hz.
- Ease toward a new value in a few bounded steps, then stop.
- Return at once from `tick10` when the page is hidden.

## Drift and scrolling

[Pixel drift](using-your-cyd.md#pixel-drift) moves every page by up to 3 px in each direction.
Leave a 3 px margin at the edges, and clear `LV_OBJ_FLAG_SCROLLABLE` on the page root and on
every container: otherwise a drift step can scroll the page.

## What the pages cost

Measured on a CYD, each page pinned for a full minute:

| page | busy % | mean refresh ms | longest refresh ms | inv_px |
|---|---:|---:|---:|---:|
| usage | 20.1 | 39.2 | 45.1 | 5246 |
| history | 9.8 | 9.8 | 53.9 | 934 |
| detail | 2.3 | 23.5 | 34.5 | 26868 |
| device | 0.6 | 51.8 | 70.2 | 189988 |

The usage page's breathing glow is the most expensive steady motion. Keep a new page's busy
share under about 30 %, and its steady refreshes under 50 ms (longer ones make ESPHome log
"took a long time"; a page change itself can take longer).

## Measure your page

On a device, pin your page and read its [status](mqtt.md#fields) after a full minute:

```bash
python3 tools/cyd_mqtt.py --broker BROKER cmd 'page mypage'
python3 tools/cyd_mqtt.py --broker BROKER status
```

- `busy`: the share of time spent drawing, in percent;
- `refr_n`, `refr_ms`, `refr_max_ms`: redraws in the last window, their average and longest time;
- `inv_px`: the average number of pixels redrawn per refresh. Small is good.

Without a device, the preview shows what moves. Take a few frames and compare them:

```bash
python3 tools/cyd_preview.py shot --page mypage --frames 10 --out mypage.png
```

If your page is not in `firmware/site.yaml`, add the one-page site and bindings from
[Select and preview your page](add-a-page.md#4-select-and-preview-your-page):
`--site mypage-site.yaml --bindings mypage-bindings.yaml`.

```python
from PIL import Image, ImageChops
a, b = Image.open('mypage-000.png'), Image.open('mypage-001.png')
print(ImageChops.difference(a.convert('RGB'), b.convert('RGB')).getbbox())
```

The box printed is the area that changed between two frames, 100 ms apart. On a still page it
is `None`; for a moving dot it is a few pixels wide.
