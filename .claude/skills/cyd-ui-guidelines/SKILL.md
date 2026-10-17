---
name: cyd-ui-guidelines
description: Use when designing or reviewing how a cyd-dash page looks or moves - font sizes, colours and contrast, animation, redraw cost, drift margins - and when measuring a page's drawing cost on a device or in the preview.
---

# UI rules for the CYD, with their numbers

The full reasons and measurements: [Keep it smooth and readable](../../../docs/ui-guidelines.md).

| rule | number | why |
|---|---|---|
| fonts | 48 px digits-only hero, 32 main, 20 secondary, 16 floor | [Text size](../../../docs/ui-guidelines.md#text-size): smaller text is unreadable on this panel |
| contrast | at least 4.5:1 on `0x101820`; red text 24 px or more | [Contrast](../../../docs/ui-guidelines.md#contrast) |
| colour | never the only signal | green and amber are equally bright |
| widgets | built once, set only on change | every setter redraws |
| motion | from `tick10` (100 ms), only while visible, at most 10 Hz in total | [Animation](../../../docs/ui-guidelines.md#animation): a free-running `lv_anim` cost 27 % |
| redraws | small areas; a 128 px arc step costs about 27 ms | a style change redraws the bounding box |
| layout | not scrollable, 3 px margin | [Drift and scrolling](../../../docs/ui-guidelines.md#drift-and-scrolling) |

For scale ([What the pages cost](../../../docs/ui-guidelines.md#what-the-pages-cost)): the
usage page's glow costs about 20 % busy, history about 10 %, the static pages 1–2 %. Aim for
under 30 % busy and steady refreshes under 50 ms.

## Measure

- On a device: pin the page, wait a minute, read `busy`, `refr_ms`, `refr_max_ms` and
  `inv_px` from the status ([Measure your page](../../../docs/ui-guidelines.md#measure-your-page)).
- In the preview: `shot --frames 10`, then compare neighbouring frames; the changed box shows
  what moves. A still page gives `None`.
- Look at the screenshots yourself for clipping and readability.
