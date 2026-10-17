#!/usr/bin/env python3
"""Plan a cyd-dash build, write firmware/generated/, then (compile, run) invoke ESPHome.

It reads site.yaml (the `cyd:` part: which pages, the rotation interval), bindings.yaml
(where each page's values come from) and each chosen page's manifest.yaml, checks them,
prints the build plan, and writes the files ESPHome includes:

  generated/site.yaml       site.yaml without its `cyd:` part (ESPHome would reject that key)
  generated/build.yaml      the pages' packages, the header list and the page glue scripts,
                            cyd_deliver among them: every incoming message goes through it
  generated/mqtt.yaml       one MQTT subscription per topic (the device only)
  generated/site_preview.yaml  site.yaml's substitutions without the network and secret ones,
                            for the desktop preview (preview.yaml)
  generated/cyd_slots.h     page and slot ids as named constants, and the slot table
  generated/cyd_registry.h  the page table: names, titles, rotation flags and hooks

Needs only the standard library and PyYAML (ESPHome depends on it too).
"""
import argparse
import difflib
import json
from pathlib import Path
import re
import subprocess
import sys

import yaml

ROOT = Path(__file__).resolve().parents[1]
FIRMWARE = ROOT / 'firmware'
DEFAULT_PAGES = ['usage', 'detail', 'device']
DEFAULT_ROTATION = 20
MAX_PAGES = 8
SLOT_TYPES = ('number', 'text', 'bool', 'epoch')
HOOKS = ('build', 'render', 'tick10', 'status', 'selftest')
MANIFEST_KEYS = ('name', 'title', 'description', 'rotate', 'requires', 'slots', 'headers', 'hooks', 'topics', 'optional_groups')
SLOT_KEYS = ('type', 'required', 'unit', 'doc', 'default')
NAME = re.compile(r'[a-z][a-z0-9_]*$')
CPP_NAME = re.compile(r'[A-Za-z_]\w*(::[A-Za-z_]\w*)*$')
# Core headers, in include order around the generated and page headers.
CORE_FIRST = ['model.h', 'version/', 'version/identity.h', 'core/reset_reason.h', 'core/net.h', 'core/touchcal.h', 'core/tap.h',
              'core/slots.h', 'generated/cyd_slots.h', 'core/perf.h']
CORE_LAST = ['core/saver.h', 'generated/cyd_registry.h', 'core/pages.h']
# Substitutions the preview leaves out: it has no network, and generated/ should hold no secret.
PRIVATE_SUBSTITUTION = re.compile(r'wifi|password|passwd|secret|token|key|mqtt_(broker|user)', re.I)
BOOL_WORDS = {'true': True, 'on': True, 'yes': True, '1': True,
              'false': False, 'off': False, 'no': False, '0': False}


class BuildError(Exception):
    """A problem in site.yaml, bindings.yaml or a manifest, worded for the person fixing it."""


def revision(root):
    try:
        rev = subprocess.check_output(
            ['git', '-C', str(root), 'rev-parse', '--short', 'HEAD'],
            stderr=subprocess.DEVNULL, text=True).strip()
        if not re.fullmatch(r'[0-9a-f]{7,40}', rev):
            return 'unknown'
        dirty = subprocess.check_output(
            ['git', '-C', str(root), 'status', '--porcelain', '--untracked-files=normal'],
            stderr=subprocess.DEVNULL, text=True)
        return rev + ('-dirty' if dirty else '')
    except (OSError, subprocess.CalledProcessError):
        return 'unknown'


# ---- YAML that may hold ESPHome tags (!secret, !include, !lambda ...) ----

class Tagged:
    """An ESPHome tag such as `!secret wifi_password`, kept as it is."""
    def __init__(self, tag, value):
        self.tag, self.value = tag, value

    def __repr__(self):
        return '%s %s' % (self.tag, self.value)


class _Loader(yaml.SafeLoader):
    pass


def _tagged(loader, suffix, node):
    if isinstance(node, yaml.ScalarNode):
        value = loader.construct_scalar(node)
    elif isinstance(node, yaml.SequenceNode):
        value = loader.construct_sequence(node)
    else:
        value = loader.construct_mapping(node)
    return Tagged('!' + suffix, value)


_Loader.add_multi_constructor('!', _tagged)
# Only true and false are booleans. In YAML 1.1 `on`, `off`, `yes` and `no` are booleans too,
# which would turn a slot named `on` into True; here they stay words (a bool literal still
# accepts them, see literal_text).
_Loader.yaml_implicit_resolvers = {
    key: [r for r in resolvers if r[0] != 'tag:yaml.org,2002:bool']
    for key, resolvers in yaml.SafeLoader.yaml_implicit_resolvers.items()}
_Loader.add_implicit_resolver('tag:yaml.org,2002:bool', re.compile(r'^(?:true|True|TRUE|false|False|FALSE)$'), list('tTfF'))


def load_yaml(path, what):
    try:
        text = Path(path).read_text()
    except OSError as e:
        raise BuildError('cannot read %s (%s): %s' % (what, path, e.strerror))
    try:
        return yaml.load(text, Loader=_Loader), text
    except yaml.YAMLError as e:
        raise BuildError('%s (%s) is not valid YAML:\n%s' % (what, path, e))


def closest(word, choices):
    match = difflib.get_close_matches(str(word), list(choices), n=1, cutoff=0.5)
    return ' Did you mean "%s"?' % match[0] if match else ''


# ---- JSON paths ----

def parse_path(path):
    """'a.b[0].c' -> ['a', 'b', 0, 'c']. Raises BuildError for a malformed path."""
    hint = ('Use dots between keys and [n] for list items, for example '
            '"plans.main.used_pct" or "sensors[0].temperature".')
    if not isinstance(path, str) or not path.strip():
        raise BuildError('the JSON path is empty. ' + hint)
    parts = []
    for i, seg in enumerate(path.split('.')):
        m = re.fullmatch(r'([^.\[\]\s]*)((?:\[\d+\])*)', seg)
        if not m or not (m.group(1) or m.group(2)) or (i > 0 and not m.group(1)):
            raise BuildError('the JSON path "%s" is not valid. %s' % (path, hint))
        if m.group(1):
            parts.append(m.group(1))
        parts += [int(n) for n in re.findall(r'\[(\d+)\]', m.group(2))]
    return parts


# ---- literals ----

def literal_text(kind, value):
    """A literal or default as the text the firmware converts at boot; None if the type is wrong."""
    if isinstance(value, (dict, list, Tagged)) or value is None:
        return None
    if kind == 'text':
        return ('true' if value else 'false') if isinstance(value, bool) else str(value)
    if kind == 'bool':
        if isinstance(value, bool):
            return 'true' if value else 'false'
        word = str(value).strip().lower()
        return ('true' if BOOL_WORDS[word] else 'false') if word in BOOL_WORDS else None
    if isinstance(value, bool):
        return None
    try:
        number = float(value)
    except (TypeError, ValueError):
        return None
    if number != number:
        return None
    if kind == 'epoch':
        return str(int(number)) if number == int(number) else None
    return repr(number) if isinstance(value, float) else str(value).strip()


# ---- manifests ----

def available_pages(firmware):
    pages = firmware / 'pages'
    if not pages.is_dir():
        return []
    return sorted(p.name for p in pages.iterdir()
                  if p.is_dir() and not p.name.startswith(('_', '.')) and (p / 'manifest.yaml').is_file())


def load_manifest(firmware, name):
    folder = firmware / 'pages' / name
    where = 'pages/%s/manifest.yaml' % name
    data, _ = load_yaml(folder / 'manifest.yaml', where)
    errors = []
    if not isinstance(data, dict):
        raise BuildError('%s: expected keys such as name, title and slots.' % where)
    for key in data:
        if key not in MANIFEST_KEYS:
            errors.append('%s: unknown key "%s".%s' % (where, key, closest(key, MANIFEST_KEYS)))
    if data.get('name') != name:
        errors.append('%s: name is "%s" but the folder is "%s"; they must match.' % (where, data.get('name'), name))
    rotate = data.get('rotate', True)
    if not isinstance(rotate, bool):
        errors.append('%s: rotate must be true or false.' % where)
    slots = data.get('slots') or {}
    if not isinstance(slots, dict):
        errors.append('%s: slots must be a mapping, one entry per value.' % where)
        slots = {}
    clean = {}
    for slot, spec in slots.items():
        label = '%s, slot "%s"' % (where, slot)
        if not isinstance(slot, str) or not NAME.match(slot):
            errors.append('%s: slot names use lower case letters, digits and _.' % label)
            continue
        if not isinstance(spec, dict):
            errors.append('%s: expected {type: ..., required: ..., doc: ...}.' % label)
            continue
        for key in spec:
            if key not in SLOT_KEYS:
                errors.append('%s: unknown key "%s".%s' % (label, key, closest(key, SLOT_KEYS)))
        kind = spec.get('type')
        if kind not in SLOT_TYPES:
            errors.append('%s: type must be one of %s.' % (label, ', '.join(SLOT_TYPES)))
            continue
        required = spec.get('required', False)
        if not isinstance(required, bool):
            errors.append('%s: required must be true or false.' % label)
        default = None
        if 'default' in spec:
            default = literal_text(kind, spec['default'])
            if default is None:
                errors.append('%s: the default %r is not a %s.' % (label, spec['default'], kind))
        clean[slot] = {'type': kind, 'required': required is True, 'default': default,
                       'unit': spec.get('unit'), 'doc': spec.get('doc', '')}
    hooks = data.get('hooks') or {}
    if not isinstance(hooks, dict):
        errors.append('%s: hooks must be a mapping such as {build: my_page::build}.' % where)
        hooks = {}
    for hook, fn in hooks.items():
        if hook not in HOOKS:
            errors.append('%s: unknown hook "%s".%s Hooks: %s.' % (where, hook, closest(hook, HOOKS), ', '.join(HOOKS)))
        elif not isinstance(fn, str) or not CPP_NAME.match(fn):
            errors.append('%s: hook %s must name a C++ function, such as my_page::%s.' % (where, hook, hook))
    headers = data.get('headers')
    if headers is None:
        headers = sorted(p.name for p in folder.glob('*.h'))
    elif not isinstance(headers, list):
        errors.append('%s: headers must be a list of the page\'s .h files.' % where)
        headers = []
    for h in headers:
        if not (folder / str(h)).is_file():
            errors.append('%s: header "%s" is not in pages/%s/.' % (where, h, name))
    requires = data.get('requires') or []
    if not isinstance(requires, list):
        errors.append('%s: requires must be a list of page names.' % where)
        requires = []
    page_yaml = folder / 'page.yaml'
    text = ''
    if not page_yaml.is_file():
        errors.append('pages/%s/page.yaml is missing (the page\'s ESPHome package).' % name)
    else:
        text = page_yaml.read_text()
        for wanted in ('%s_page' % name, '%s_root' % name):
            if not re.search(r'\bid:\s*%s\b' % wanted, text):
                errors.append('pages/%s/page.yaml: no widget with "id: %s". Every page has an LVGL page '
                              '"%s_page" holding a 320x240 obj "%s_root".' % (name, wanted, name, name))
    topics = data.get('topics') or {}
    if not isinstance(topics, dict):
        errors.append('%s: topics must be a mapping such as {"${my_topic}": %s_on_message}.' % (where, name))
        topics = {}
    for topic, script in topics.items():
        if not isinstance(topic, str) or not topic.strip():
            errors.append('%s: a topic must be text, such as "home/livingroom/climate" or "${my_topic}".' % where)
        elif not isinstance(script, str) or not NAME.match(script):
            errors.append('%s: topic %s must name a script in page.yaml, such as %s_on_message.' % (where, topic, name))
        elif text and not re.search(r'\bid:\s*%s\b' % script, text):
            errors.append('pages/%s/page.yaml: no script "%s" for topic %s. Add one with a '
                          '`payload: string` parameter.' % (name, script, topic))
    groups = data.get('optional_groups') or {}
    if not isinstance(groups, dict):
        errors.append('%s: optional_groups must be a mapping.' % where)
        groups = {}
    for group, spec in groups.items():
        if (not isinstance(spec, dict) or set(spec) != {'required', 'members'}
                or not isinstance(spec['required'], list) or not spec['required']
                or not isinstance(spec['members'], list)
                or not all(isinstance(s, str) and s in clean for s in spec['members'])
                or not all(s in spec['members'] for s in spec['required'])):
            errors.append('%s: optional group %s needs required and members lists of known slots.' % (where, group))
    if errors:
        raise BuildError('\n'.join(errors))
    return {'optional_groups': groups, 'name': name, 'title': str(data.get('title', name)), 'rotate': rotate,
            'slots': clean, 'hooks': hooks, 'headers': [str(h) for h in headers], 'requires': requires,
            'topics': [(t, s) for t, s in topics.items()]}


# ---- the plan ----

def read_site(site_path):
    data, text = load_yaml(site_path, 'site.yaml')
    notes = []
    if not isinstance(data, dict):
        raise BuildError('site.yaml (%s) should hold substitutions: and cyd:. '
                         'Start from firmware/site.example.yaml.' % site_path)
    cyd = data.get('cyd')
    if cyd is None:
        notes.append('site.yaml has no cyd: part, so the build uses pages [%s] and rotation_interval %d '
                     '(see site.example.yaml).' % (', '.join(DEFAULT_PAGES), DEFAULT_ROTATION))
        cyd = {}
    if not isinstance(cyd, dict):
        raise BuildError('site.yaml: cyd: should hold pages: and rotation_interval:.')
    for key in cyd:
        if key not in ('pages', 'rotation_interval'):
            raise BuildError('site.yaml: unknown key cyd.%s.%s' % (key, closest(key, ('pages', 'rotation_interval'))))
    pages = cyd.get('pages', DEFAULT_PAGES)
    if isinstance(pages, str):
        pages = [p.strip() for p in pages.split(',') if p.strip()]
    if not isinstance(pages, list) or not pages or not all(isinstance(p, str) for p in pages):
        raise BuildError('site.yaml: cyd.pages should be a list of page names, for example\n'
                         '  cyd:\n    pages: [usage, device]')
    rotation = cyd.get('rotation_interval', DEFAULT_ROTATION)
    if isinstance(rotation, bool) or not isinstance(rotation, int) or not 5 <= rotation <= 600:
        raise BuildError('site.yaml: cyd.rotation_interval should be a whole number of seconds '
                         'from 5 to 600, for example\n  cyd:\n    rotation_interval: 20')
    return pages, rotation, text, notes


def plan(site_path, bindings_path, firmware=FIRMWARE):
    """Check everything and return the build plan (a dict). Raises BuildError."""
    firmware = Path(firmware)
    names, rotation, site_text, notes = read_site(site_path)
    site_data, _ = load_yaml(site_path, 'site.yaml')
    substitutions = site_data.get('substitutions') if isinstance(site_data, dict) else None
    available = available_pages(firmware)
    errors = []
    seen = set()
    for name in names:
        if name in seen:
            errors.append('site.yaml: page "%s" is listed twice in cyd.pages.' % name)
        seen.add(name)
        if name not in available:
            errors.append('site.yaml: there is no page "%s".%s The pages are: %s.'
                          % (name, closest(name, available), ', '.join(available) or '(none)'))
    if len(names) > MAX_PAGES:
        errors.append('site.yaml: %d pages listed; the dots in the nav bar fit at most %d.' % (len(names), MAX_PAGES))
    if errors:
        raise BuildError('\n'.join(errors))
    pages = []
    for name in names:
        try:
            pages.append(load_manifest(firmware, name))
        except BuildError as e:
            errors.append(str(e))
    if errors:
        raise BuildError('\n'.join(errors))
    for page in pages:
        for need in page['requires']:
            if need not in names:
                errors.append('site.yaml: page "%s" needs page "%s" in cyd.pages too.' % (page['name'], need))
    if not any(p['rotate'] for p in pages):
        notes.append('no page takes part in Auto rotation, so Auto mode stays on the current page.')

    # bindings
    if Path(bindings_path).is_file():
        bindings, _ = load_yaml(bindings_path, 'bindings.yaml')
    else:
        bindings = None
        notes.append('no bindings.yaml (%s): every slot is unbound. Start from '
                     'firmware/bindings.example.yaml.' % bindings_path)
    bindings = bindings or {}
    if not isinstance(bindings, dict):
        raise BuildError('bindings.yaml should hold one block per page, for example\n'
                         '  thermostat:\n    temperature: {topic: home/livingroom/climate, json: temperature}')
    by_page = {p['name']: p for p in pages}
    chosen = {}
    for page_name, block in bindings.items():
        if page_name not in available:
            errors.append('bindings.yaml: there is no page "%s".%s The pages are: %s.'
                          % (page_name, closest(page_name, available), ', '.join(available)))
            continue
        if page_name not in by_page:
            notes.append('bindings for "%s" are skipped: it is not in cyd.pages.' % page_name)
            continue
        block = block or {}
        if not isinstance(block, dict):
            errors.append('bindings.yaml: %s: should hold one line per slot, or {} for none.' % page_name)
            continue
        slots = by_page[page_name]['slots']
        for slot, entry in block.items():
            label = 'bindings.yaml, %s.%s' % (page_name, slot)
            if slot not in slots:
                known = ', '.join(slots) or 'none: it needs no input'
                errors.append('%s: page "%s" has no slot "%s".%s Its slots: %s.'
                              % (label, page_name, slot, closest(slot, slots), known))
                continue
            try:
                chosen[(page_name, slot)] = check_binding(entry, slots[slot]['type'])
            except BuildError as e:
                errors.append('%s: %s' % (label, e))

    for page in pages:
        for group, spec in page.get('optional_groups', {}).items():
            if any((page['name'], s) in chosen for s in spec['members']):
                missing = [s for s in spec['required'] if (page['name'], s) not in chosen]
                if missing:
                    errors.append('bindings.yaml: optional group %s.%s needs %s when any member is bound.'
                                  % (page['name'], group, ', '.join(missing)))

    # the slot table, in page order then manifest order
    table, topics = [], []
    for index, page in enumerate(pages):
        for slot, spec in page['slots'].items():
            bound = chosen.get((page['name'], slot))
            row = {'page': page['name'], 'page_idx': index, 'slot': slot, 'type': spec['type'],
                   'required': spec['required'], 'source': 'unbound', 'topic': None, 'path': '',
                   'literal': None, 'default': False}
            if bound:
                row.update(bound)
            elif spec['default'] is not None:
                row.update(source='literal', literal=spec['default'], default=True)
            elif spec['required']:
                example = example_line(slot, spec['type'])
                errors.append('bindings.yaml: page "%s" needs a value for "%s" (%s, required). Add, for example:\n'
                              '  %s:\n    %s' % (page['name'], slot, spec['type'], page['name'], example))
            if row['topic'] is not None and row['topic'] not in topics:
                topics.append(row['topic'])
            table.append(row)
    if errors:
        raise BuildError('\n'.join(errors))
    # Topics a page reads whole (its manifest's topics:), after the slots' topics.
    page_topics = [{'page': page['name'], 'topic': t, 'script': s} for page in pages for t, s in page['topics']]
    every = list(topics)
    for t in page_topics:
        if t['topic'] not in every:
            every.append(t['topic'])
    return {'pages': pages, 'rotation': rotation, 'slots': table, 'topics': topics,
            'page_topics': page_topics, 'all_topics': every,
            'substitutions': substitutions if isinstance(substitutions, dict) else {},
            'site_text': site_text, 'notes': notes}


def example_line(slot, kind):
    if kind == 'text':
        return '%s: {value: "Living room"}' % slot
    return '%s: {topic: home/livingroom/climate, json: %s}' % (slot, slot)


def check_binding(entry, kind):
    if not isinstance(entry, dict):
        raise BuildError('expected {topic: ..., json: ...}, {topic: ...} or {value: ...}.')
    for key in entry:
        if key not in ('topic', 'json', 'value'):
            raise BuildError('unknown key "%s".%s Use topic, json or value.' % (key, closest(key, ('topic', 'json', 'value'))))
    if 'value' in entry:
        if 'topic' in entry or 'json' in entry:
            raise BuildError('use either value (a fixed value) or topic (a value from MQTT), not both.')
        text = literal_text(kind, entry['value'])
        if text is None:
            raise BuildError('the value %r is not a %s.' % (entry['value'], kind))
        return {'source': 'literal', 'literal': text}
    topic = entry.get('topic')
    if not isinstance(topic, str) or not topic.strip():
        raise BuildError('needs a topic, for example {topic: home/livingroom/climate, json: temperature}.')
    if 'json' not in entry:
        return {'source': 'raw', 'topic': topic}
    path = entry['json']
    if isinstance(path, (int, float)) and not isinstance(path, bool):
        path = str(path)
    parse_path(path)
    return {'source': 'json', 'topic': topic, 'path': path}


# ---- output ----

def summary(p):
    lines = ['cyd-dash build plan']
    names = [pg['name'] for pg in p['pages']]
    rotating = [pg['name'] for pg in p['pages'] if pg['rotate']]
    if len(names) == 1:
        lines.append('  pages (1): %s   (one page: the arrows and dots are hidden)' % names[0])
    else:
        lines.append('  pages (%d, Auto rotation every %d s): %s' % (len(names), p['rotation'], ' → '.join(names)))
        if len(rotating) < len(names):
            lines.append('  not in Auto rotation: %s' % ', '.join(n for n in names if n not in rotating))
    rows = []
    for pg in p['pages']:
        mine = [s for s in p['slots'] if s['page'] == pg['name']]
        if not mine:
            rows.append((pg['name'], '(no inputs)', ''))
        for s in mine:
            label = '%s.%s' % (s['page'], s['slot'])
            if s['source'] == 'json':
                rows.append((label, '← %s  json: %s' % (s['topic'], s['path']), '(required)' if s['required'] else ''))
            elif s['source'] == 'raw':
                rows.append((label, '← %s  (whole payload)' % s['topic'], '(required)' if s['required'] else ''))
            elif s['source'] == 'literal':
                shown = json.dumps(s['literal'], ensure_ascii=False) if s['type'] == 'text' else s['literal']
                rows.append((label, '= %s' % shown, '(default)' if s['default'] else ''))
            else:
                rows.append((label, '(not bound)', ''))
    w1 = max(len(r[0]) for r in rows)
    w2 = max(len(r[1]) for r in rows)
    for a, b, c in rows:
        lines.append(('  %-*s  %-*s  %s' % (w1, a, w2, b, c)).rstrip())
    lines.append('  topics subscribed: %d' % len(p.get('all_topics', p['topics'])))
    for note in p['notes']:
        lines.append('  note: ' + note)
    return '\n'.join(lines)


def cstr(s):
    out = []
    for ch in s:
        if ch in '"\\':
            out.append('\\' + ch)
        elif ch == '\n':
            out.append('\\n')
        elif ord(ch) < 0x20:
            out.append('\\%03o' % ord(ch))
        else:
            out.append(ch)
    return '"' + ''.join(out) + '"'


HEADER = 'Generated by tools/cyd_build.py from site.yaml, bindings.yaml and the page manifests: do not edit.'


def site_package(site_text):
    """site.yaml without its top-level cyd: block; relative !include paths still point at firmware/."""
    out, skipping = [], False
    for line in site_text.splitlines():
        top = line[:1] not in ('', ' ', '\t', '#')
        if top:
            skipping = re.match(r'cyd\s*:', line) is not None
        if not skipping:
            out.append(line)
    text = '\n'.join(out).rstrip() + '\n'
    text = re.sub(r'(!include(?:_dir_\w+)?\s+)(?![/~])([^\s{}\[\],#]+)', r'\1../\2', text)
    return '# %s\n%s' % (HEADER, text)


def build_yaml(p):
    pages = p['pages']
    lines = ['# ' + HEADER, 'esphome:', '  includes:']
    includes = list(CORE_FIRST)
    for pg in pages:
        includes += ['pages/%s/%s' % (pg['name'], h) for h in pg['headers']]
    includes += CORE_LAST
    lines += ['    - ' + i for i in includes]
    lines.append('packages:')
    lines += ['  page_%s: !include ../pages/%s/page.yaml' % (pg['name'], pg['name']) for pg in pages]
    lines += ['script:',
              '  # Every page\'s root to the registry, which calls the build hooks (from on_boot).',
              '  - id: cyd_pages_build',
              '    then:',
              '      - lambda: |-',
              '          lv_obj_t *roots[] = {%s};' % ', '.join('id(%s_root)' % pg['name'] for pg in pages),
              '          cyd_pages::build_all(roots);',
              '  # Show the page screen_want names (from apply_screen, core/ui.yaml).',
              '  - id: cyd_show_page',
              '    then:']
    for i, pg in enumerate(pages):
        lines += ['      - if:',
                  '          condition:',
                  '            lambda: return id(screen_want) == %d && id(screen_shown) != %d;' % (i, i),
                  '          then:',
                  '            - lvgl.page.show: %s_page' % pg['name']]
    every = p.get('all_topics', p['topics'])
    lines += ['  # Every incoming message, from MQTT (generated/mqtt.yaml) or the preview\'s feed: the',
              '  # slots bound to its topic first, then the pages that read the topic (manifest topics:).',
              '  - id: cyd_deliver',
              '    mode: queued',
              '    parameters: {topic: int, payload: string}',
              '    then:',
              '      - lambda: cyd_slots::on_message(topic, payload);']
    for t in p.get('page_topics', []):
        lines += ['      - if:',
                  '          condition:',
                  '            lambda: return topic == %d;' % every.index(t['topic']),
                  '          then:',
                  '            - script.execute: {id: %s, payload: !lambda return payload;}' % t['script']]
    lines += ['  # The same, by topic name (the preview\'s feed).',
              '  - id: cyd_deliver_named',
              '    mode: queued',
              '    parameters: {topic: string, payload: string}',
              '    then:',
              '      - lambda: |-',
              '          static const char *const names[] = {%s};' % (', '.join(cstr(t) for t in every) or 'nullptr'),
              '          for (int i = 0; i < %d; i++)' % len(every),
              '            if (topic == names[i]) { id(cyd_deliver).execute(i, payload); return; }',
              '          ESP_LOGW("cyd", "No page reads the topic %s", topic.c_str());']
    return '\n'.join(lines) + '\n'


def mqtt_yaml(p):
    """One MQTT subscription per topic, each handing its payload to cyd_deliver (the device only)."""
    every = p.get('all_topics', p['topics'])
    lines = ['# ' + HEADER]
    if not every:
        return '\n'.join(lines + ['# No page reads a topic, so there is nothing to subscribe to.', '{}']) + '\n'
    lines += ['mqtt:', '  on_message:']
    for i, topic in enumerate(every):
        lines += ['    - topic: %s' % json.dumps(topic, ensure_ascii=False),
                  '      then:',
                  '        - script.execute: {id: cyd_deliver, topic: %d, payload: !lambda return x;}' % i]
    return '\n'.join(lines) + '\n'


def preview_site(p):
    """site.yaml's plain substitutions minus the network and secret ones (preview.yaml)."""
    lines = ['# ' + HEADER, '# For the desktop preview: your substitutions, without the network and secret ones.']
    kept = []
    for key, value in (p.get('substitutions') or {}).items():
        if not isinstance(key, str) or PRIVATE_SUBSTITUTION.search(key):
            continue
        if isinstance(value, bool) or not isinstance(value, (str, int, float)):
            continue
        kept.append('  %s: %s' % (key, json.dumps(str(value), ensure_ascii=False)))
    lines += ['substitutions:'] + kept if kept else ['substitutions: {}']
    return '\n'.join(lines) + '\n'


def slots_header(p):
    pages, slots, topics = p['pages'], p['slots'], p['topics']
    kinds = {'number': 'NUMBER', 'text': 'TEXT', 'bool': 'BOOL', 'epoch': 'EPOCH'}
    sources = {'unbound': 'UNBOUND', 'literal': 'LITERAL', 'raw': 'RAW', 'json': 'JSON'}
    out = ['#pragma once', '// ' + HEADER, '// Needs core/slots.h before it.', '',
           'static const int CYD_PAGE_COUNT = %d;' % len(pages),
           'static const int CYD_ROTATION_INTERVAL = %d;' % p['rotation'],
           'static const char *const CYD_PAGE_LIST = %s;' % cstr(','.join(pg['name'] for pg in pages))]
    for i, pg in enumerate(pages):
        out.append('static const int CYD_PAGE_%s = %d;' % (pg['name'], i))
        out.append('#define CYD_HAS_PAGE_%s 1' % pg['name'])
    out += ['', '// Slot ids: cyd_slots::number(CYD_SLOT_<page>_<slot>) and friends (core/slots.h).']
    for i, s in enumerate(slots):
        out.append('static const int CYD_SLOT_%s_%s = %d;  // %s' % (s['page'], s['slot'], i, s['type']))
    out += ['static const int CYD_SLOT_COUNT = %d;' % len(slots),
            'static const int CYD_TOPIC_COUNT = %d;' % len(topics), '',
            'static const char *const CYD_TOPICS[] = {%s};' % (', '.join(cstr(t) for t in topics) or '""'),
            'static const cyd_slots::Def CYD_SLOT_DEFS[] = {']
    for s in slots:
        topic = topics.index(s['topic']) if s['topic'] is not None else -1
        literal = cstr(s['literal']) if s['literal'] is not None else 'nullptr'
        out.append('  {%s, %s, %d, cyd_slots::%s, cyd_slots::%s, %d, %s, %s},'
                   % (cstr(s['page']), cstr(s['slot']), s['page_idx'], kinds[s['type']],
                      sources[s['source']], topic, cstr(s['path']), literal))
    if not slots:
        out.append('  {"", "", 0, cyd_slots::TEXT, cyd_slots::UNBOUND, -1, "", nullptr},  // placeholder: no slots')
    out += ['};',
            'static cyd_slots::Value cyd_slot_values[%d];' % max(1, len(slots)), '',
            'inline void cyd_slots_begin() {',
            '  cyd_slots::begin(CYD_SLOT_DEFS, cyd_slot_values, CYD_SLOT_COUNT, CYD_TOPICS, CYD_TOPIC_COUNT);',
            '}', '']
    return '\n'.join(out)


def registry_header(p):
    out = ['#pragma once', '// ' + HEADER, '// Needs the page headers before it.', '',
           'namespace cyd_pages {', 'static const cyd_page::Page PAGES[CYD_PAGE_COUNT] = {']
    for pg in p['pages']:
        hooks = [pg['hooks'].get(h, 'nullptr') for h in HOOKS]
        out.append('  {%s, %s, %s, %s},' % (cstr(pg['name']), cstr(pg['title']),
                                           'true' if pg['rotate'] else 'false', ', '.join(hooks)))
    out += ['};', '}  // namespace cyd_pages', '']
    return '\n'.join(out)


def write_generated(p, firmware=FIRMWARE):
    out = Path(firmware) / 'generated'
    out.mkdir(parents=True, exist_ok=True)
    files = {'site.yaml': site_package(p['site_text']), 'build.yaml': build_yaml(p),
             'cyd_slots.h': slots_header(p), 'cyd_registry.h': registry_header(p),
             'mqtt.yaml': mqtt_yaml(p), 'site_preview.yaml': preview_site(p)}
    for name, text in files.items():
        target = out / name
        if not target.is_file() or target.read_text() != text:
            target.write_text(text)
    return sorted(files)


def pages_document(firmware=FIRMWARE):
    """Render the input reference deterministically from every public manifest."""
    def cell(value):
        return str(value if value is not None else '—').replace('|', r'\|').replace('\n', ' ')
    lines = ['# Page inputs', '', 'Generated by `python3 tools/cyd_build.py docs`. Do not edit the tables.', '']
    for name in available_pages(Path(firmware)):
        page = load_manifest(Path(firmware), name)
        lines += ['## ' + page['title'], '', '`' + name + '`', '']
        if page['requires']:
            lines += ['Requires: ' + ', '.join(page['requires']) + '.', '']
        if not page['slots']:
            lines += ['No inputs.', '']; continue
        lines += ['| Slot | Type | Required | Default | Unit | Meaning |', '|---|---|---|---|---|---|']
        for name, slot in page['slots'].items():
            lines.append('| ' + ' | '.join(cell(v) for v in [name, slot['type'], 'yes' if slot['required'] else 'no', slot['default'], slot['unit'], slot['doc']]) + ' |')
        lines += ['']
    return '\n'.join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('action', choices=['prepare', 'compile', 'run', 'docs'])
    parser.add_argument('--release', action='store_true', help='Require a clean git checkout')
    parser.add_argument('--site', default=str(FIRMWARE / 'site.yaml'), help='default: firmware/site.yaml')
    parser.add_argument('--bindings', default=str(FIRMWARE / 'bindings.yaml'), help='default: firmware/bindings.yaml')
    parser.add_argument('--device', help='USB serial port or network address for run (skips the upload menu)')
    args = parser.parse_args(argv)
    if args.device and args.action != 'run':
        parser.error('--device is only available with run')
    if args.action == "docs":
        try:
            text = pages_document()
        except BuildError as e:
            print('cyd-dash: docs/pages.md was not written.\n', file=sys.stderr)
            print(str(e), file=sys.stderr)
            return 2
        (ROOT / "docs/pages.md").write_text(text)
        print("Wrote docs/pages.md")
        return 0
    rev = revision(ROOT)
    if args.release and (rev == 'unknown' or rev.endswith('-dirty')):
        parser.error('release requires a clean committed git checkout')
    try:
        p = plan(args.site, args.bindings, FIRMWARE)
    except BuildError as e:
        print('cyd-dash: the build cannot start yet.\n', file=sys.stderr)
        print(str(e), file=sys.stderr)
        return 2
    print(summary(p), flush=True)
    write_generated(p, FIRMWARE)
    target = FIRMWARE / 'version/generated.h'
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text('#pragma once\n#define CYD_GIT_REV "' + rev + '"\n')
    print('GIT=' + rev, flush=True)
    if args.action != 'prepare':
        command = ['esphome', args.action, 'firmware/cyd-dash.yaml']
        if args.device:
            command += ['--device', args.device]
        subprocess.run(command, cwd=ROOT, check=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
