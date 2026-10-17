#!/usr/bin/env python3
"""The desktop preview: your pages in a window on your computer, and PNG screenshots of them.

  python3 tools/cyd_preview.py run --feed tools/examples/usage.json
  python3 tools/cyd_preview.py shot --page usage --feed tools/examples/usage.json --out usage.png
  python3 tools/cyd_preview.py shot --all --feed tools/examples/usage.json --out-dir shots
  python3 tools/cyd_preview.py shot --page usage --frames 20 --out glow.png

It prepares generated/ (tools/cyd_build.py), compiles firmware/preview.yaml for ESPHome's host
platform with an SDL window, and runs it. `shot` needs no screen: it renders off-screen, saves
320x240 PNGs and exits. With --frames N it saves N frames 100 ms apart, and an animated GIF too
when Pillow is installed.

A feed is a JSON list of messages that go through the same code as MQTT messages do:

  [{"topic": "home/livingroom/climate", "payload": {"temperature": 21.5}},
   {"topic": "${usage_topic}", "payload": {"ts": "now", "resets_at": "now+3600"}}]

A payload is text, or JSON that is sent compact. "now", "now+N" and "now-N" become the current
Unix time (plus or minus N seconds), and ${name} in a topic is your site's substitution.
docs/preview.md has the details.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
NAME = 'cyd-preview'  # esphome: name: in firmware/preview.yaml
NOW = re.compile(r'now(?:\s*([+-])\s*(\d+))?$')
SUB = re.compile(r'\$\{(\w+)\}|\$(\w+)')


class PreviewError(Exception):
    """Something to fix before the preview can run, worded for the person fixing it."""


def read_substitutions(firmware):
    """generated/site_preview.yaml: one `  key: "json string"` per line (cyd_build.py writes it)."""
    path = Path(firmware) / 'generated/site_preview.yaml'
    subs = {}
    if path.is_file():
        for line in path.read_text().splitlines():
            m = re.match(r'  (\w+): (".*")$', line)
            if m:
                subs[m.group(1)] = json.loads(m.group(2))
    return subs


def page_list(firmware):
    """The build's pages, from generated/cyd_slots.h."""
    path = Path(firmware) / 'generated/cyd_slots.h'
    m = re.search(r'CYD_PAGE_LIST = "([^"]*)"', path.read_text()) if path.is_file() else None
    if not m:
        raise PreviewError('%s has no page list yet: run tools/cyd_build.py prepare first.' % path)
    return [p for p in m.group(1).split(',') if p]


def resolve(value, now):
    """Turn "now", "now+N" and "now-N" anywhere in a payload into Unix seconds."""
    if isinstance(value, dict):
        return {k: resolve(v, now) for k, v in value.items()}
    if isinstance(value, list):
        return [resolve(v, now) for v in value]
    if isinstance(value, str):
        m = NOW.match(value.strip())
        if m:
            offset = int(m.group(2) or 0)
            return int(now) + (-offset if m.group(1) == '-' else offset)
    return value


def load_feed(path, subs, now=None):
    """A feed file -> [(topic, payload text)]."""
    now = time.time() if now is None else now
    try:
        data = json.loads(Path(path).read_text())
    except (OSError, ValueError) as e:
        raise PreviewError('the feed %s cannot be read: %s' % (path, e))
    if isinstance(data, dict):
        data = data.get('messages')
    if not isinstance(data, list):
        raise PreviewError('the feed %s should be a JSON list of {"topic": ..., "payload": ...}.' % path)
    out = []
    for n, msg in enumerate(data, 1):
        if not isinstance(msg, dict) or not isinstance(msg.get('topic'), str) or 'payload' not in msg:
            raise PreviewError('%s, message %d: expected {"topic": "...", "payload": ...}.' % (path, n))

        def sub(m):
            key = m.group(1) or m.group(2)
            if key not in subs:
                known = ', '.join(sorted(subs)) or 'none'
                raise PreviewError('%s, message %d: the topic uses ${%s}, which your site does not define '
                                   '(known: %s).' % (path, n, key, known))
            return subs[key]
        topic = SUB.sub(sub, msg['topic'])
        payload = msg['payload']
        if not isinstance(payload, str):
            payload = json.dumps(resolve(payload, now), separators=(',', ':'), ensure_ascii=False)
        if '\t' in topic or '\n' in topic or '\n' in payload:
            raise PreviewError('%s, message %d: a topic or text payload cannot hold a tab or a line break.' % (path, n))
        out.append((topic, payload))
    return out


def png_size(path):
    """(width, height) of a PNG, or None."""
    with open(path, 'rb') as f:
        head = f.read(24)
    if len(head) < 24 or head[:8] != b'\x89PNG\r\n\x1a\n' or head[12:16] != b'IHDR':
        return None
    return struct.unpack('>II', head[16:24])


def program(firmware):
    return Path(firmware) / '.esphome/build' / NAME / '.pioenvs' / NAME / 'program'


def prepare(args):
    if args.no_prepare:
        return
    sys.path.insert(0, str(ROOT / 'tools'))
    import cyd_build
    site = args.site or str(ROOT / 'firmware/site.yaml')
    if not args.site and not Path(site).is_file():
        site = str(ROOT / 'firmware/site.example.yaml')
        print('cyd-preview: no firmware/site.yaml, so the preview uses site.example.yaml.', flush=True)
    argv = ['prepare', '--site', site]
    if args.bindings:
        argv += ['--bindings', args.bindings]
    elif not (ROOT / 'firmware/bindings.yaml').is_file():
        argv += ['--bindings', str(ROOT / 'firmware/bindings.example.yaml')]
    if cyd_build.main(argv) != 0:
        raise PreviewError('tools/cyd_build.py prepare stopped (see above).')


def build(args):
    firmware = Path(args.firmware)
    if args.no_build and program(firmware).is_file():
        return program(firmware)
    rc = subprocess.run([args.esphome, 'compile', str(firmware / 'preview.yaml')]).returncode
    if rc != 0 or not program(firmware).is_file():
        raise PreviewError('the preview did not compile (esphome exit code %d). ESPHome needs SDL2 for it: '
                           'see docs/preview.md.' % rc)
    return program(firmware)


def feed_file(args, workdir):
    """The feed as the preview binary reads it: topic, a tab, the payload, one per line."""
    if not args.feed:
        return ''
    messages = load_feed(args.feed, read_substitutions(args.firmware))
    path = Path(workdir) / 'feed.txt'
    path.write_text(''.join('%s\t%s\n' % m for m in messages))
    return str(path)


def environment(args, feed, page=None, shot=None):
    env = dict(os.environ)
    env['CYD_FEED'] = feed
    env['CYD_PAGE'] = page or ''
    env['CYD_CMDS'] = args.cmd or ''
    if shot:
        env.setdefault('SDL_VIDEODRIVER', 'offscreen')
        env['CYD_SHOT'] = str(shot)
        env['CYD_SHOT_DELAY'] = str(args.delay)
        env['CYD_FRAMES'] = str(args.frames)
    return env


def frame_names(out, frames):
    if frames <= 1:
        return [out]
    return [out.with_name('%s-%03d%s' % (out.stem, i, out.suffix)) for i in range(frames)]


def make_gif(frames, out):
    try:
        from PIL import Image
    except ImportError:
        print('cyd-preview: no Pillow, so no GIF; the frames are PNGs.', flush=True)
        return None
    images = [Image.open(f).convert('RGB') for f in frames]
    gif = out.with_suffix('.gif')
    images[0].save(gif, save_all=True, append_images=images[1:], duration=100, loop=0)
    return gif


def shot(args):
    firmware = Path(args.firmware)
    pages = page_list(firmware)
    wanted = pages if args.all else [args.page]
    for page in wanted:
        if page not in pages:
            raise PreviewError('there is no page "%s" in this build. Its pages: %s.' % (page, ', '.join(pages)))
    binary = build(args)
    with tempfile.TemporaryDirectory(prefix='cyd-preview-') as work:
        feed = feed_file(args, work)
        for page in wanted:
            if args.all:
                out = Path(args.out_dir) / ('%s.png' % page)
            else:
                out = Path(args.out)
            out = out.resolve()
            out.parent.mkdir(parents=True, exist_ok=True)
            frames = frame_names(out, args.frames)
            for f in frames:
                if f.exists():
                    f.unlink()
            timeout = 30 + (args.delay + args.frames * 200) / 1000
            try:
                run = subprocess.run([str(binary)], env=environment(args, feed, page, out), cwd=work,
                                     stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                                     errors='replace', timeout=timeout)
                log, rc = run.stdout, run.returncode
            except subprocess.TimeoutExpired as e:
                log, rc = (e.stdout or b'').decode('utf-8', 'replace') if isinstance(e.stdout, bytes) else (e.stdout or ''), 'timeout'
            if args.verbose or rc != 0 or not all(f.is_file() for f in frames):
                print(log[-6000:])
            if rc != 0 or not all(f.is_file() for f in frames):
                raise PreviewError('the screenshot of page "%s" failed (exit %s); its log is above.' % (page, rc))
            size = png_size(frames[0])
            print('SHOT page=%s file=%s frames=%d size=%s' % (page, frames[0], len(frames),
                                                              'x'.join(map(str, size)) if size else '?'), flush=True)
            if len(frames) > 1:
                gif = make_gif(frames, out)
                if gif:
                    print('GIF page=%s file=%s' % (page, gif), flush=True)


def run(args):
    binary = build(args)
    with tempfile.TemporaryDirectory(prefix='cyd-preview-') as work:
        feed = feed_file(args, work)
        print('cyd-preview: close the window or press Ctrl+C to stop.', flush=True)
        try:
            subprocess.run([str(binary)], env=environment(args, feed, args.page), cwd=work)
        except KeyboardInterrupt:
            pass


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('action', choices=['build', 'run', 'shot'])
    parser.add_argument('--page', help='the page to show (a name from cyd.pages)')
    parser.add_argument('--all', action='store_true', help='shot: every page of the build')
    parser.add_argument('--feed', help='a JSON feed file, such as tools/examples/usage.json')
    parser.add_argument('--cmd', help='commands after the feed, ";" between them, such as "drift off"')
    parser.add_argument('--out', default='preview.png', help='shot: the PNG to write')
    parser.add_argument('--out-dir', default='shots', help='shot --all: the folder for <page>.png')
    parser.add_argument('--frames', type=int, default=1, help='shot: frames 100 ms apart (1 to 200)')
    parser.add_argument('--delay', type=int, default=1500, help='shot: ms from the feed to the first frame')
    parser.add_argument('--site', help='default: firmware/site.yaml, else firmware/site.example.yaml')
    parser.add_argument('--bindings', help='default: firmware/bindings.yaml')
    parser.add_argument('--firmware', default=str(ROOT / 'firmware'), help=argparse.SUPPRESS)
    parser.add_argument('--esphome', default=shutil.which('esphome') or 'esphome', help='the esphome command')
    parser.add_argument('--no-prepare', action='store_true', help='use generated/ as it is')
    parser.add_argument('--no-build', action='store_true', help='reuse the last preview binary if there is one')
    parser.add_argument('-v', '--verbose', action='store_true', help='shot: print the preview\'s log')
    args = parser.parse_args(argv)
    if args.action == 'shot' and not args.all and not args.page:
        parser.error('shot needs --page NAME or --all')
    if not 1 <= args.frames <= 200:
        parser.error('--frames is 1 to 200')
    try:
        prepare(args)
        if args.action == 'build':
            print('PROGRAM=%s' % build(args))
        elif args.action == 'run':
            run(args)
        else:
            shot(args)
    except PreviewError as e:
        print('cyd-preview: %s' % e, file=sys.stderr)
        return 2
    return 0


if __name__ == '__main__':
    sys.exit(main())
