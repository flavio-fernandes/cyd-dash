@AGENTS.md

## Notes for Claude

- **You can look at your own screenshots.** After `tools/cyd_preview.py shot`, read the PNG
  and describe what you see: the hero value, the waiting and stale states, clipped text,
  anything that overlaps. A screenshot you did not look at is not a check.
- **Skills** in `.claude/skills/` walk through the common jobs, each pointing into the docs:

| skill | use it to |
|---|---|
| [add-cyd-page](.claude/skills/add-cyd-page/SKILL.md) | add a new page, from the template to a pull request |
| [cyd-bindings](.claude/skills/cyd-bindings/SKILL.md) | connect a page to MQTT topics, or find out why a slot stays empty |
| [cyd-build-flash](.claude/skills/cyd-build-flash/SKILL.md) | compile, flash over USB or Wi-Fi, and check the boot |
| [cyd-ui-guidelines](.claude/skills/cyd-ui-guidelines/SKILL.md) | choose sizes, colours and motion, and measure a page's cost |
| [cyd-hardware-quirks](.claude/skills/cyd-hardware-quirks/SKILL.md) | understand colours, touch, resets and board variants |
| [cyd-mqtt-drive](.claude/skills/cyd-mqtt-drive/SKILL.md) | drive and check a CYD from a script, calibration included |
