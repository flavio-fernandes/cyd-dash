"""Offline tests for tools/cyd_build.py: the build plan, its friendly errors and its output."""
import contextlib
import importlib.util
import io
from pathlib import Path
import tempfile
import re
import textwrap
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('cyd_build', ROOT / 'tools/cyd_build.py')
assert spec is not None and spec.loader is not None
build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build)

THERMOSTAT = '''
name: thermostat
title: Thermostat
description: Room temperature and humidity.
rotate: true
slots:
  temperature: {type: number, required: true, unit: "°C", doc: "Room temperature."}
  humidity:    {type: number, required: false, unit: "%", doc: "Relative humidity."}
  label:       {type: text, default: "Thermostat", doc: "Title shown on top."}
  heating:     {type: bool, doc: "Is the heating on?"}
  updated_at:  {type: epoch, doc: "When the values were measured."}
hooks: {build: cyd_thermostat::build, render: cyd_thermostat::render}
'''
SITE = '''
substitutions:
  device_name: cyd-dash
  wifi_password: !secret wifi_password
cyd:
  pages: [%s]
  rotation_interval: 30
'''


def page_yaml(name):
    return 'lvgl:\n  pages:\n    - id: %s_page\n      widgets:\n        - obj: {id: %s_root}\n' % (name, name)


class Fixture:
    """A small firmware tree with three pages and a template that is never a page."""
    def __init__(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.fw = self.root / 'firmware'
        pages = {'thermostat': THERMOSTAT,
                 'hello': 'name: hello\ntitle: Hello\nslots: {}\n',
                 'device': 'name: device\ntitle: Device\nslots: {}\n',
                 'needy': 'name: needy\nrequires: [hello]\n',
                 '_template': 'name: _template\n'}
        for name, manifest in pages.items():
            folder = self.fw / 'pages' / name
            folder.mkdir(parents=True)
            (folder / 'manifest.yaml').write_text(manifest)
            (folder / 'page.yaml').write_text(page_yaml(name))
        (self.fw / 'pages/thermostat/thermostat.h').write_text('#pragma once\n')

    def files(self, pages, bindings):
        site = self.root / 'site.yaml'
        site.write_text(SITE % pages)
        bind = self.root / 'bindings.yaml'
        bind.write_text(textwrap.dedent(bindings))
        return site, bind

    def plan(self, pages, bindings):
        site, bind = self.files(pages, bindings)
        return build.plan(site, bind, self.fw)

    def error(self, pages, bindings):
        with self.assertRaises(build.BuildError) as ctx:  # noqa: set by the test case
            self.plan(pages, bindings)
        return str(ctx.exception)


GOOD = '''
thermostat:
  temperature: {topic: home/livingroom/climate, json: temperature}
  humidity:    {topic: home/livingroom/climate, json: humidity}
  label:       {value: "Living room"}
hello: {}
'''


class PathTests(unittest.TestCase):
    def test_good_paths(self):
        self.assertEqual(build.parse_path('a.b[0].c'), ['a', 'b', 0, 'c'])
        self.assertEqual(build.parse_path('plans.codex.five_hour.used_pct'), ['plans', 'codex', 'five_hour', 'used_pct'])
        self.assertEqual(build.parse_path('a[1][2]'), ['a', 1, 2])
        self.assertEqual(build.parse_path('[0].value'), [0, 'value'])
        self.assertEqual(build.parse_path('temperature'), ['temperature'])

    def test_bad_paths(self):
        for bad in ['', ' ', 'a..b', '.a', 'a.', 'a[x]', 'a[0', 'a]', 'a b', 'a.[0]', 'a[-1]']:
            with self.subTest(path=bad), self.assertRaises(build.BuildError) as ctx:
                build.parse_path(bad)
            self.assertIn('[n]', str(ctx.exception))


class PlanTests(unittest.TestCase):
    def setUp(self):
        self.fx = Fixture()
        self.addCleanup(self.fx.tmp.cleanup)

    def error(self, pages, bindings):
        with self.assertRaises(build.BuildError) as ctx:
            self.fx.plan(pages, bindings)
        return str(ctx.exception)

    def test_missing_page_lists_the_pages(self):
        msg = self.error('thermostatt, device', '{}')
        self.assertIn('there is no page "thermostatt"', msg)
        self.assertIn('Did you mean "thermostat"?', msg)
        self.assertIn('The pages are: device, hello, needy, thermostat.', msg)
        self.assertNotIn('_template', msg)

    def test_missing_required_slot_shows_an_example(self):
        msg = self.error('thermostat', 'thermostat: {}\n')
        self.assertIn('page "thermostat" needs a value for "temperature" (number, required)', msg)
        self.assertIn('  thermostat:\n    temperature: {topic: home/livingroom/climate, json: temperature}', msg)

    def test_unknown_slot_and_page_suggest_the_closest(self):
        msg = self.error('thermostat', '''
            thermostat:
              temperature: {topic: t}
              humidty: {topic: t}
            thermostatt: {}
            ''')
        self.assertIn('bindings.yaml, thermostat.humidty: page "thermostat" has no slot "humidty". Did you mean "humidity"?', msg)
        self.assertIn('bindings.yaml: there is no page "thermostatt". Did you mean "thermostat"?', msg)

    def test_type_mismatch(self):
        msg = self.error('thermostat', '''
            thermostat:
              temperature: {value: warm}
              heating: {value: maybe}
              updated_at: {value: 1.5}
            ''')
        self.assertIn("thermostat.temperature: the value 'warm' is not a number.", msg)
        self.assertIn("thermostat.heating: the value 'maybe' is not a bool.", msg)
        self.assertIn('thermostat.updated_at: the value 1.5 is not a epoch.', msg)

    def test_literals_that_fit(self):
        p = self.fx.plan('thermostat', '''
            thermostat:
              temperature: {value: "21.5"}
              heating: {value: on}
              updated_at: {value: 1760000000}
            ''')
        lit = {s['slot']: s['literal'] for s in p['slots']}
        self.assertEqual(lit, {'temperature': '21.5', 'humidity': None, 'label': 'Thermostat',
                               'heating': 'true', 'updated_at': '1760000000'})

    def test_on_and_yes_stay_words(self):
        data, _ = build.load_yaml(self.fx.files('hello', 'on: {topic: t}\nyes: true\n')[1], 'bindings.yaml')
        self.assertEqual(data, {'on': {'topic': 't'}, 'yes': True})

    def test_bad_json_path_in_a_binding(self):
        msg = self.error('thermostat', 'thermostat:\n  temperature: {topic: t, json: "a..b"}\n')
        self.assertIn('bindings.yaml, thermostat.temperature: the JSON path "a..b" is not valid.', msg)

    def test_binding_shape(self):
        msg = self.error('thermostat', 'thermostat:\n  temperature: {topic: t, value: 3}\n  humidity: {json: h}\n')
        self.assertIn('use either value', msg)
        self.assertIn('humidity: needs a topic', msg)

    def test_requires(self):
        msg = self.error('needy', '{}')
        self.assertIn('page "needy" needs page "hello" in cyd.pages too.', msg)

    def test_one_topic_feeds_several_slots(self):
        p = self.fx.plan('thermostat, hello, device', GOOD)
        self.assertEqual(p['topics'], ['home/livingroom/climate'])
        out = build.build_yaml(p)
        mqtt = build.mqtt_yaml(p)
        self.assertEqual(mqtt.count('- topic:'), 1)
        self.assertIn('script.execute: {id: cyd_deliver, topic: 0, payload: !lambda return x;}', mqtt)
        self.assertIn('lambda: cyd_slots::on_message(topic, payload);', out)
        self.assertIn('static const char *const names[] = {"home/livingroom/climate"};', out)
        header = build.slots_header(p)
        self.assertIn('static const int CYD_SLOT_thermostat_temperature = 0;', header)
        self.assertIn('static const int CYD_SLOT_thermostat_humidity = 1;', header)
        self.assertIn('{"thermostat", "temperature", 0, cyd_slots::NUMBER, cyd_slots::JSON, 0, "temperature", nullptr},', header)
        self.assertIn('{"thermostat", "humidity", 0, cyd_slots::NUMBER, cyd_slots::JSON, 0, "humidity", nullptr},', header)
        self.assertIn('{"thermostat", "label", 0, cyd_slots::TEXT, cyd_slots::LITERAL, -1, "", "Living room"},', header)
        self.assertIn('{"thermostat", "heating", 0, cyd_slots::BOOL, cyd_slots::UNBOUND, -1, "", nullptr},', header)
        self.assertIn('static const int CYD_PAGE_COUNT = 3;', header)
        self.assertIn('static const int CYD_ROTATION_INTERVAL = 30;', header)
        self.assertIn('#define CYD_HAS_PAGE_hello 1', header)
        registry = build.registry_header(p)
        self.assertIn('{"thermostat", "Thermostat", true, cyd_thermostat::build, cyd_thermostat::render, nullptr, nullptr, nullptr},', registry)
        self.assertIn('{"hello", "Hello", true, nullptr, nullptr, nullptr, nullptr, nullptr},', registry)
        self.assertIn('    - pages/thermostat/thermostat.h\n', out)
        self.assertIn('  page_hello: !include ../pages/hello/page.yaml', out)
        self.assertIn('lv_obj_t *roots[] = {id(thermostat_root), id(hello_root), id(device_root)};', out)

    def test_no_input_page_and_summary(self):
        p = self.fx.plan('thermostat, hello, device', GOOD)
        self.assertEqual(build.summary(p), '\n'.join([
            'cyd-dash build plan',
            '  pages (3, Auto rotation every 30 s): thermostat → hello → device',
            '  thermostat.temperature  ← home/livingroom/climate  json: temperature  (required)',
            '  thermostat.humidity     ← home/livingroom/climate  json: humidity',
            '  thermostat.label        = "Living room"',
            '  thermostat.heating      (not bound)',
            '  thermostat.updated_at   (not bound)',
            '  hello                   (no inputs)',
            '  device                  (no inputs)',
            '  topics subscribed: 1']))

    def test_one_page_build(self):
        p = self.fx.plan('hello', '{}')
        self.assertEqual(build.summary(p).splitlines()[1], '  pages (1): hello   (one page: the arrows and dots are hidden)')
        self.assertNotIn('mqtt:', build.build_yaml(p))
        self.assertNotIn('mqtt:', build.mqtt_yaml(p))
        self.assertIn('placeholder: no slots', build.slots_header(p))

    def test_default_shows_in_the_summary(self):
        p = self.fx.plan('thermostat', 'thermostat:\n  temperature: {topic: t}\n')
        self.assertRegex(build.summary(p), r'thermostat\.label +  = "Thermostat" +\(default\)')
        self.assertIn('thermostat.temperature  ← t  (whole payload)', build.summary(p))

    def test_site_package_keeps_tags_and_drops_cyd(self):
        site, _ = self.fx.files('hello', '{}')
        text = site.read_text() + 'extra: !include more.yaml\n'
        out = build.site_package(text)
        self.assertIn('wifi_password: !secret wifi_password', out)
        self.assertIn('extra: !include ../more.yaml', out)
        self.assertNotIn('cyd:', out)
        self.assertNotIn('rotation_interval', out)

    def test_main_with_site_and_bindings_options(self):
        site, bind = self.fx.files('thermostat, device', GOOD)
        old = build.FIRMWARE
        build.FIRMWARE = self.fx.fw
        self.addCleanup(setattr, build, 'FIRMWARE', old)
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            rc = build.main(['prepare', '--site', str(site), '--bindings', str(bind)])
        self.assertEqual(rc, 0, err.getvalue())
        self.assertIn('pages (2, Auto rotation every 30 s): thermostat → device', out.getvalue())
        self.assertIn('note: bindings for "hello" are skipped: it is not in cyd.pages.', out.getvalue())
        gen = self.fx.fw / 'generated'
        self.assertEqual(sorted(p.name for p in gen.iterdir()), ['build.yaml', 'cyd_registry.h', 'cyd_slots.h', 'mqtt.yaml', 'site.yaml', 'site_preview.yaml'])
        self.assertTrue((self.fx.fw / 'version/generated.h').is_file())
        bind.write_text('thermostat: {}\n')
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            rc = build.main(['prepare', '--site', str(site), '--bindings', str(bind)])
        self.assertEqual(rc, 2)
        self.assertIn('the build cannot start yet', err.getvalue())
        self.assertIn('needs a value for "temperature"', err.getvalue())

    def run_main(self, argv):
        site, bind = self.fx.files('hello, device', '{}')
        old = build.FIRMWARE
        build.FIRMWARE = self.fx.fw
        self.addCleanup(setattr, build, 'FIRMWARE', old)
        out, err = io.StringIO(), io.StringIO()
        with mock.patch.object(build, 'revision', return_value='abc1234'), \
                mock.patch.object(build.subprocess, 'run') as esphome, \
                contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            rc = build.main(argv + ['--site', str(site), '--bindings', str(bind)])
        return rc, esphome, err.getvalue()

    def test_run_with_device_skips_the_upload_menu(self):
        rc, esphome, err = self.run_main(['run', '--device', '/dev/ttyUSB0'])
        self.assertEqual(rc, 0, err)
        esphome.assert_called_once_with(['esphome', 'run', 'firmware/cyd-dash.yaml', '--device', '/dev/ttyUSB0'],
                                        cwd=build.ROOT, check=True)

    def test_run_without_device_lets_esphome_ask(self):
        rc, esphome, err = self.run_main(['run'])
        self.assertEqual(rc, 0, err)
        esphome.assert_called_once_with(['esphome', 'run', 'firmware/cyd-dash.yaml'], cwd=build.ROOT, check=True)

    def test_device_only_with_run(self):
        for action in ('prepare', 'compile', 'docs'):
            with self.assertRaises(SystemExit) as ctx:
                self.run_main([action, '--device', '/dev/ttyUSB0'])
            self.assertEqual(ctx.exception.code, 2)
        rc_err = io.StringIO()
        with contextlib.redirect_stderr(rc_err), self.assertRaises(SystemExit):
            build.main(['compile', '--device', '/dev/ttyUSB0'])
        self.assertIn('--device is only available with run', rc_err.getvalue())


class RepoTests(unittest.TestCase):
    def test_the_examples_build(self):
        fw = ROOT / 'firmware'
        p = build.plan(fw / 'site.example.yaml', fw / 'bindings.example.yaml', fw)
        self.assertEqual([pg['name'] for pg in p['pages']], ['usage', 'detail', 'device'])
        self.assertEqual(p['rotation'], 20)

    def test_install_hello_world_settings_build(self):
        """docs/install.md's broker-free Hello World files plan as written."""
        doc = (ROOT / 'docs/install.md').read_text()
        section = doc.split('## Try Hello World first (no broker needed)\n', 1)[1].split('\n## ', 1)[0]
        site_text, bindings_text = re.findall(r'```yaml\n(.*?)```', section, re.S)[:2]
        with tempfile.TemporaryDirectory() as tmp:
            site, bind = Path(tmp) / 'site.yaml', Path(tmp) / 'bindings.yaml'
            site.write_text(site_text)
            bind.write_text(bindings_text)
            p = build.plan(site, bind, ROOT / 'firmware')
        self.assertEqual([pg['name'] for pg in p['pages']], ['hello', 'device'])
        self.assertEqual(p['substitutions']['mqtt_reboot_timeout'], '0s')
        self.assertEqual(p['slots'], [])

    def test_reboot_timeout_default_comes_before_site(self):
        """ESPHome lets a later package override an earlier one: defaults must precede site."""
        main = (ROOT / 'firmware/cyd-dash.yaml').read_text()
        self.assertLess(main.index('defaults: !include core/defaults.yaml'), main.index('site: !include generated/site.yaml'))
        self.assertIn('mqtt_reboot_timeout: 30min', (ROOT / 'firmware/core/defaults.yaml').read_text())
        self.assertIn('reboot_timeout: ${mqtt_reboot_timeout}', (ROOT / 'firmware/core/mqtt.yaml').read_text())


if __name__ == '__main__':
    unittest.main()
