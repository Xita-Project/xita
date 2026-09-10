#!/usr/bin/env python3
"""Progress extraction must report observed artifacts without claiming correctness."""
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from profile_progress import generate, site


class ProgressTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='xita-progress-test-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.profile = SimpleNamespace(id='fixture_game', name='Fixture <script>game</script>',
            binary={'sha256':'a'*64, 'base_address':0x10000, 'size_of_image':0x10000})
        self.manifest = {'functions':[0x11000, 0x12000, 0x13000], 'profile':self.profile.id,
            'input_sha256':'a'*64, 'hle_used':['ExampleHLE'], 'kernel_used':['ExampleKernel']}
        self.save_manifest()
        (self.root/'code_000.c').write_text('''void f_00011000(xctx *restrict c)
{
    /* 00011000  nop */
    /* 00011000  nop */
}
void f_00012000(xctx *restrict c)
{
    { extern int example_native(xctx *); if (example_native(c)) return; }
    /* 00012000  ret */
}
void f_00013000(xctx *restrict c)
{
    /* 00013000  ud2 */
    xv_unimpl(c, 0x13000u, "ud2");
    xv_unimpl(c, 0x13000u, "ud2");
}
''')

    def save_manifest(self):
        (self.root/'recomp_report.json').write_text(json.dumps(self.manifest))

    def test_counts_and_no_false_verification(self):
        doc=generate(self.profile,self.root,'fixture')
        self.assertEqual(doc['summary'],{'functions':3, 'instruction_sites':3,
            'statuses':{'translated':1,'native':1,'unsupported':1,'boundary':2},'hle':1,'kernel':1})
        self.assertEqual(doc['items'][2]['unsupported_sites'],1)
        self.assertEqual(doc['items'][1]['helpers'],['example_native'])
        for item in doc['items']:
            self.assertIsNone(item['exact_match']); self.assertIsNone(item['validation'])
        text=json.dumps(doc)
        self.assertNotIn('xv_unimpl(c',text); self.assertNotIn(self.temp.name,text)

    def test_reject_wrong_game_and_incomplete_artifacts(self):
        for key,value in [('input_sha256','b'*64),('profile','different'),('functions',[0x11000]),('functions',[0x11000]*2),('functions',[0x90000])]:
            with self.subTest(key=key,value=value):
                old=self.manifest[key]; self.manifest[key]=value; self.save_manifest()
                with self.assertRaises(ValueError): generate(self.profile,self.root,'fixture')
                self.manifest[key]=old
        self.save_manifest()
        (self.root/'code_001.c').write_bytes((self.root/'code_000.c').read_bytes())
        with self.assertRaises(ValueError): generate(self.profile,self.root,'fixture')

    def test_multiple_profiles_and_portable_safe_export(self):
        doc=generate(self.profile,self.root,'fixture')
        first=self.root/'first.json';first.write_text(json.dumps(doc))
        doc['profile_id']='second_game';doc['name']='Second game'
        second=self.root/'second.json';second.write_text(json.dumps(doc))
        out=self.root/'viewer'
        self.assertEqual(site([second,first],out),2)
        js=(out/'reports.js').read_text()
        self.assertNotIn('<script>',js)
        self.assertIn('\\u003cscript\\u003e',js)
        self.assertTrue((out/'index.html').exists())
        self.assertEqual(len(json.loads((out/'reports.json').read_text())),2)
        with self.assertRaises(ValueError): site([first,first],out)
        self.assertEqual(js,(out/'reports.js').read_text())


if __name__=='__main__': unittest.main()
