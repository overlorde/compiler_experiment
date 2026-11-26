import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch
from types import SimpleNamespace

import study


class PilotTests(unittest.TestCase):
    def test_infrastructure_failures_are_not_rejections(self):
        for p in (SimpleNamespace(returncode=2, stderr='', stdout=''),
                  SimpleNamespace(returncode=0, stderr='unexpected output', stdout='{}'),
                  SimpleNamespace(returncode=0, stderr='', stdout='{"accepted": 0}')):
            with patch('study.subprocess.run', return_value=p):
                with self.assertRaises(RuntimeError):
                    study.query('metadata', 'scalar', 0, 'generic', '')

    def test_fixture_dimensions_and_valid_repairs(self):
        fs = study.fixtures()
        self.assertEqual(len(fs), 20)
        self.assertEqual(len({f['id'] for f in fs}), 20)
        for f in fs:
            for world in (0, 15):
                good = f['candidates'][2 if f['stratum'] == 'metadata' else world]['source']
                self.assertTrue(study.query(f['stratum'], f['shape'], world, 'generic', good)[0]['accepted'], f['id'])
                self.assertFalse(study.query(f['stratum'], f['shape'], world, 'generic', f['initial'])[0]['accepted'], f['id'])

    def test_metadata_equivalence_and_modes(self):
        for f in study.fixtures():
            if f['stratum'] != 'metadata': continue
            projected = []
            full = []
            for world in range(16):
                for mode in study.MODES:
                    view, _ = study.query('metadata', f['shape'], world, mode, f['initial'])
                    self.assertFalse(view['accepted'])
                    if mode == 'projected': projected.append(study.visible_key(view))
                    if mode == 'full': full.append(study.visible_key(view))
                    if mode != 'full': self.assertNotIn('private_', study.visible_key(view))
            self.assertEqual(len(set(projected)), 1, f['id'])
            self.assertEqual(len(set(full)), 16, f['id'])

    def test_contract_verdict_only_projection(self):
        for shape in ('scalar', 'pointer'):
            for world in range(16):
                for mask in (0, 5, 15):
                    source = study.contract_program(mask, shape)
                    views = [study.query('contracts', shape, world, m, source)[0] for m in study.MODES]
                    self.assertEqual({v['accepted'] for v in views}, {world == mask})
                    self.assertEqual(views[-1], views[2])

    def test_parse_and_unstructured_failures(self):
        for source in ('int main( {', 'int main() { unknown(); return 0; }'):
            for stratum in ('metadata', 'contracts'):
                a = study.query(stratum, 'scalar', 0, 'projected', source)[0]
                b = study.query(stratum, 'scalar', 15, 'projected', source)[0]
                self.assertFalse(a['accepted']); self.assertEqual(a, b)
                self.assertNotIn('private_', study.visible_key(a))
        self.assertEqual(study.query('metadata', 'scalar', 0, 'projected', 'int main() { unknown(); return 0; }')[0]['feedback'], 'rejected')

    def test_source_escaping_and_invalid_requests(self):
        for source in ("int main(){ @ return 0; }", 'int main(){ return 0; }'):
            self.assertIsInstance(study.query('metadata', 'scalar', 1, 'full', source)[0], dict)
        for world in ('16', '-1', 'x'):
            p = subprocess.run([str(study.HELPER), 'metadata', world, 'scalar', 'full'], input='', capture_output=True, text=True)
            self.assertEqual(p.returncode, 2); self.assertEqual(p.stdout, '')

    def test_public_strategies_and_budget(self):
        for f in study.fixtures():
            for world in (0, 7, 15):
                for mode in study.MODES:
                    transcript = []
                    def ask(source):
                        v, _ = study.query(f['stratum'], f['shape'], world, mode, source)
                        transcript.append(v); return v
                    public = {k:f[k] for k in ('initial', 'candidates', 'public_hint')}
                    source, n = study.repair(public, ask)
                    self.assertLessEqual(n, 20); self.assertEqual(n, len(transcript))
                    self.assertTrue(study.query(f['stratum'], f['shape'], world, 'generic', source)[0]['accepted'])
        for world in range(16):
            history = study.contract_probe('scalar', lambda s:study.query('contracts', 'scalar', world, 'generic', s)[0])
            self.assertEqual(history[-1]['identified'], world)


if __name__ == '__main__':
    unittest.main()
