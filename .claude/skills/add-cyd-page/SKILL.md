---
name: add-cyd-page
description: Use when asked to add, create or change a page on the cyd-dash display (for example "add a page that shows my power use"). Covers the template, slots, the docs table, example bindings and feed, preview screenshots, the compile and a device check.
---

# Add a page to cyd-dash

Follow [Add a page](../../../docs/add-a-page.md); this is the checklist around it.

1. **Copy the template** to `firmware/pages/<name>/` and rename `example` everywhere
   ([1. Copy the template](../../../docs/add-a-page.md#1-copy-the-template)). There is no
   registry to edit: the folder and its manifest are the registration.
2. **Declare the slots** in `manifest.yaml`: a type, `required` or a default, and a friendly
   `doc` for each. List every header under `headers`
   ([2. Describe the page](../../../docs/add-a-page.md#2-describe-the-page),
   [5. Add inputs](../../../docs/add-a-page.md#5-add-inputs-the-thermostat)).
   The thermostat (`firmware/pages/thermostat/`) is the model for a page with inputs; the
   big-number page (`firmware/pages/stat/`) is a smaller one. Quote a `doc` that contains a
   comma or a colon. A font of your own (a 48 px digits-only hero) goes in `page.yaml`,
   never in the manifest.
3. **Write the hooks**: widgets once in `build`, changed values only in `render`, motion only in
   `tick10` while visible; show waiting, never zero, for a missing value
   ([3. Draw once](../../../docs/add-a-page.md#3-draw-once-update-only-what-changed),
   and the cyd-ui-guidelines skill). A text slot bound to a topic is empty until its first
   message: fall back to the default in your header. If you add a `status` hook, describe its
   fields in the page table of [the MQTT reference](../../../docs/mqtt.md#fields); a test checks it.
4. **Regenerate the docs table**: `python3 tools/cyd_build.py docs`
   ([7. Regenerate](../../../docs/add-a-page.md#7-regenerate-the-reference-and-propose-your-change)).
5. **Add examples**: `firmware/bindings.<name>.example.yaml`, and feeds
   `tools/examples/<name>.json`, `<name>-waiting.json`, `<name>-stale.json`
   ([Feeding values](../../../docs/preview.md#feeding-values)). A stale feed needs an
   `updated_at` epoch slot (`"updated_at": "now-1500"`). Add a `<name>_sample` function to
   `tools/publish_example.py` (its generic values rarely fit) and a test for it.
6. **Preview and look**: a one-page site file
   ([4. Select and preview](../../../docs/add-a-page.md#4-select-and-preview-your-page)), then
   `python3 tools/cyd_preview.py shot --site <name>-site.yaml --bindings firmware/bindings.<name>.example.yaml --page <name> --feed tools/examples/<name>.json --out <name>.png`.
   Read each PNG and describe it. Save the best as `docs/images/<name>.png`, shrunk first: the
   preview writes about 230 KB, Pillow's `save(..., optimize=True)` brings it to about 6 KB
   ([7. Regenerate](../../../docs/add-a-page.md#7-regenerate-the-reference-and-propose-your-change)).
   A feed's messages all arrive at once, so a transition between two values shows only on a device.
7. **Test and compile**: `python3 -m unittest discover -s tests -v`, then
   `python3 tools/cyd_build.py compile` with `cyd: pages: [<name>]` alone, and again with the
   other pages.
8. **On a device**, when one is available: change the `marker`, flash, then
   `cmd 'page <name>'` and `status --expect page=<name>`; a photo of the screen helps the
   reviewer ([6. Compile, flash, and check status](../../../docs/add-a-page.md#6-compile-flash-and-check-status)).

Done means every box in the definition of done in
[AGENTS.md](../../../AGENTS.md#definition-of-done-for-a-page-pull-request).
Keep your one-page site files out of the commit.
