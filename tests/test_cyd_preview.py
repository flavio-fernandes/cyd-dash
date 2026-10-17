"""Offline tests for the desktop preview: the feed format (tools/cyd_preview.py) and the parts of
tools/cyd_build.py it relies on (manifest topics:, cyd_deliver, mqtt.yaml, site_preview.yaml)."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'tools' / (name + '.py'))
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


preview = load('cyd_preview')
build = load('cyd_build')

NEWS_PAGE = '''script:
  - id: news_on_message
    parameters:
      payload: string
    then: []
lvgl:
  pages:
    - id: news_page
      widgets:
        - obj: {id: news_root}
'''
SITE = '''substitutions:
  device_name: cyd-dash
  wifi_ssid: !secret wifi_ssid
  wifi_password: "open sesame"
  mqtt_broker: mqtt.example.com
  api_key: abc
  news_topic: home/news
  timezone: Etc/UTC
cyd:
  pages: [news]
'''


class Tree:
    def __init__(self, manifest, page=NEWS_PAGE, bindings='{}\n'):
        self.tmp = tempfile.TemporaryDirectory()
        root = Path(self.tmp.name)
        self.fw = root / 'firmware'
        folder = self.fw / 'pages/news'
        folder.mkdir(parents=True)
        (folder / 'manifest.yaml').write_text(manifest)
        (folder / 'page.yaml').write_text(page)
        self.site = root / 'site.yaml'
        self.site.write_text(SITE)
        self.bindings = root / 'bindings.yaml'
        self.bindings.write_text(bindings)

    def plan(self):
        return build.plan(self.site, self.bindings, self.fw)


class PageTopics(unittest.TestCase):
    def test_a_page_topic_goes_through_cyd_deliver(self):
        tree = Tree('name: news\nslots: {}\ntopics:\n  "${news_topic}": news_on_message\n')
        p = tree.plan()
        self.assertEqual(p['all_topics'], ['${news_topic}'])
        out = build.build_yaml(p)
        self.assertIn('- script.execute: {id: news_on_message, payload: !lambda return payload;}', out)
        self.assertIn('lambda: return topic == 0;', out)
        self.assertIn('static const char *const names[] = {"${news_topic}"};', out)
        self.assertNotIn('mqtt:', out)  # the preview has no MQTT client
        mqtt = build.mqtt_yaml(p)
        self.assertIn('    - topic: "${news_topic}"\n', mqtt)
        self.assertIn('script.execute: {id: cyd_deliver, topic: 0, payload: !lambda return x;}', mqtt)
        self.assertIn('topics subscribed: 1', build.summary(p))

    def test_a_missing_script_is_named(self):
        tree = Tree('name: news\ntopics:\n  "${news_topic}": news_on_msg\n')
        with self.assertRaises(build.BuildError) as ctx:
            tree.plan()
        self.assertIn('no script "news_on_msg"', str(ctx.exception))

    def test_no_topics_means_no_subscriptions(self):
        p = Tree('name: news\n').plan()
        self.assertNotIn('mqtt:', build.mqtt_yaml(p))
        self.assertIn('names[] = {nullptr}', build.build_yaml(p))

    def test_preview_site_leaves_out_network_and_secrets(self):
        out = build.preview_site(Tree('name: news\n').plan())
        self.assertIn('  news_topic: "home/news"\n', out)
        self.assertIn('  timezone: "Etc/UTC"\n', out)
        for private in ('wifi', 'open sesame', 'mqtt.example.com', 'abc', '!secret'):
            self.assertNotIn(private, out)


class Feed(unittest.TestCase):
    def feed(self, data):
        tmp = tempfile.NamedTemporaryFile('w', suffix='.json', delete=False)
        self.addCleanup(Path(tmp.name).unlink)
        tmp.write(json.dumps(data))
        tmp.close()
        return tmp.name

    def test_now_and_substitutions(self):
        path = self.feed([{'topic': '${news_topic}', 'payload': {'ts': 'now', 'at': ['now+60', 'now - 5'], 'n': 3}},
                          {'topic': 'home/raw', 'payload': 'now'}])
        out = preview.load_feed(path, {'news_topic': 'home/news'}, now=1000)
        self.assertEqual(out, [('home/news', '{"ts":1000,"at":[1060,995],"n":3}'), ('home/raw', 'now')])

    def test_unknown_substitution_is_named(self):
        path = self.feed([{'topic': '${nope}', 'payload': 1}])
        with self.assertRaises(preview.PreviewError) as ctx:
            preview.load_feed(path, {'news_topic': 'x'})
        self.assertIn('${nope}', str(ctx.exception))
        self.assertIn('news_topic', str(ctx.exception))

    def test_bad_shapes(self):
        with self.assertRaises(preview.PreviewError):
            preview.load_feed(self.feed({'topic': 't'}), {})
        with self.assertRaises(preview.PreviewError):
            preview.load_feed(self.feed([{'topic': 't'}]), {})
        with self.assertRaises(preview.PreviewError):
            preview.load_feed(self.feed([{'topic': 't', 'payload': 'two\nlines'}]), {})

    def test_the_example_feed_loads(self):
        out = preview.load_feed(ROOT / 'tools/examples/usage.json', {'usage_topic': 'cyd/usage'}, now=100)
        self.assertEqual(out[0][0], 'cyd/usage')
        self.assertEqual(json.loads(out[0][1])['gauges'][0]['resets_at'], 7900)

    def test_substitutions_and_pages_from_generated(self):
        tree = Tree('name: news\n')
        build.write_generated(tree.plan(), tree.fw)
        self.assertEqual(preview.read_substitutions(tree.fw)['news_topic'], 'home/news')
        self.assertEqual(preview.page_list(tree.fw), ['news'])


if __name__ == '__main__':
    unittest.main()
