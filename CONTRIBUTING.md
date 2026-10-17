# Contributing

Thank you for helping! New pages are the most welcome contribution, and fixes to the docs come
right after: if a step confused you, it will confuse the next person too.

## Propose a page

Follow [Add a page](docs/add-a-page.md). It walks from a copy of `firmware/pages/_template/` to
a page you can see in the preview. A page is ready for a pull request when it has:

1. **A manifest** (`firmware/pages/<name>/manifest.yaml`) with a title, a one-line description,
   and a short `doc` for every slot.
2. **The docs table**: run `python3 tools/cyd_build.py docs` and commit the updated
   `docs/pages.md`.
3. **An example feed**: `tools/examples/<name>.json`, so anyone can see the page without a
   broker. If the page has inputs, check that `tools/publish_example.py` sends sensible values
   for them.
4. **A preview image**: `docs/images/<name>.png`, taken with
   `python3 tools/cyd_preview.py shot --page <name> --feed tools/examples/<name>.json --out docs/images/<name>.png`.
   Keep it under about 100 KB.
5. **A clean compile** for the CYD: `python3 tools/cyd_build.py compile` with your page in
   `cyd: pages:`.

A few habits keep the device smooth (the [Add a page](docs/add-a-page.md) guide explains each):
build widgets once and update them only when a value changes, animate only from the shared
100 ms clock and only while your page is on screen, and keep text at 16 px or larger.

Working with a coding agent? [AGENTS.md](AGENTS.md) gives it the map, the commands and the
checks, and `.claude/skills/` holds step-by-step skills for the common jobs.

## Run the tests

```bash
python3 -m unittest discover -s tests -v
```

They need Python 3, PyYAML and a C++17 compiler such as `g++` (two tests build the
history and tap models with it). They check the build generator, the page manifests, the docs'
links and images, the MQTT reference and the tone of the docs.

## Writing style

Everything here is read by people who may be new to ESPHome, so keep it friendly:

- Lead with an example, then explain.
- Use short steps and plain words.
- Skip words that make a reader feel slow, such as `simple` or `obviously`. The tests flag
  a few of them outside code.

## Pull requests

- One topic per pull request, with a short note on what it changes and how you checked it.
- Never commit your `site.yaml`, `bindings.yaml` or `secrets.yaml`: git ignores them for you.
