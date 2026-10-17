import unittest, sys, tempfile
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import cyd_build as build
import cyd_mqtt as mqtt
import publish_example as pub

class UsageTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(); self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.site = self.root / 'site.yaml'; self.site.write_text('cyd: {pages: [usage, detail, device]}\n')
        self.bind = self.root / 'bindings.yaml'
    def plan(self, extra=''):
        self.bind.write_text('usage:\n  gauge1_label: {value: Main}\n  gauge1_used_pct: {topic: test/use}\n  gauge1_resets_at: {topic: test/reset}\n'+extra)
        return build.plan(self.site, self.bind)
    def test_one_gauge(self):
        p=self.plan(); self.assertEqual(len(p['topics']),2)
        self.assertFalse(p['page_topics'])
        self.assertTrue(all(s['source']=='unbound' for s in p['slots'] if s['slot'].startswith('gauge2') and not s['slot'].endswith('old_after')))
    def test_partial_second_gauge_rejected(self):
        for field in ['label','weekly_pct','auth']:
            with self.subTest(field=field), self.assertRaisesRegex(build.BuildError, 'optional group usage.gauge2 needs'):
                self.plan('  gauge2_'+field+': {topic: test/second}\n')
    def test_full_second_gauge(self):
        p=self.plan('  gauge2_label: {value: Other}\n  gauge2_used_pct: {topic: test/second}\n  gauge2_resets_at: {topic: test/reset}\n')
        self.assertEqual(len(p['topics']),3)
    def test_missing_primary(self):
        self.bind.write_text('{}')
        with self.assertRaisesRegex(build.BuildError,'gauge1_label'):
            build.plan(self.site,self.bind)
    def test_device_no_inputs(self):
        self.site.write_text('cyd: {pages: [device]}\n'); self.bind.write_text('{}')
        p=build.plan(self.site,self.bind); self.assertEqual(pub.messages(p),[])
    def test_sample_bindings(self):
        p=build.plan(build.FIRMWARE/'site.example.yaml',build.FIRMWARE/'bindings.example.yaml')
        import json
        messages=pub.messages(p,now=1000)
        self.assertEqual(len(messages),1)
        g=json.loads(messages[0][1])['gauges']
        self.assertEqual(len(g),2); self.assertEqual(g[0]['resets_at'],8800)
        self.assertNotEqual(pub.messages(p,step=0,now=1000),pub.messages(p,step=1,now=1000))
    def test_status_matching(self):
        expect=mqtt.expectations(['online=true','state=fresh','a.n=2'])
        self.assertTrue(mqtt.matches({'online':True,'state':'fresh','a':{'n':2}},expect))
        self.assertFalse(mqtt.matches({'online':False},expect))
        with self.assertRaises(ValueError): mqtt.expectations(['bad'])
    def test_commands_never_retained(self):
        class Info:
            def wait_for_publish(self,timeout): pass
            def is_published(self): return True
        class Client:
            def publish(self,*args,**kw): self.kw=kw; return Info()
        c=Client(); mqtt.send(c,'cyd/test/cmd','wake',True)
        self.assertFalse(c.kw['retain'])
        mqtt.send(c,'cyd/test/data','1',True); self.assertTrue(c.kw['retain'])

if __name__=='__main__': unittest.main()
