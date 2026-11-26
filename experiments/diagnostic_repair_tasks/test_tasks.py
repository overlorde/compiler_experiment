from copy import deepcopy
import subprocess
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import study


class TaskTests(unittest.TestCase):
    def test_frozen_dimensions_and_edit_domain(self):
        tasks = study.tasks()
        self.assertEqual(len(tasks), 36)
        self.assertEqual(len({t['id'] for t in tasks}), 36)
        self.assertEqual(sum(len(t['candidates']) for t in tasks), 468)
        self.assertEqual(sum(t['split'] == 'layout_check' for t in tasks), 18)
        self.assertEqual({t['variant'] for t in tasks}, set(range(6)))
        for t in tasks:
            self.assertEqual(len(t['candidates']), 3**t['holes'])
            self.assertIn(t['initial'], {c['source'] for c in t['candidates']})
            shapes = set()
            for c in t['candidates']:
                import re
                shapes.add(re.sub(r'source\d', 'SOURCE', c['source']))
                self.assertNotIn('@@', c['source'])
            self.assertEqual(len(shapes), 1, t['id'])

    def test_public_signatures_match_actual_frontend(self):
        for variant in range(6):
            for shape in ('scalar', 'pointer'):
                interface = study.api(variant, shape)
                for source, produced in interface['sources'].items():
                    for sink, consumed in interface['sinks'].items():
                        program = f'int main() {{ {sink}({source}()); return 0; }}'
                        for world in (0, 15):
                            view = study.query(world, variant, shape, 'projected', program)
                            self.assertEqual(view['accepted'], produced == consumed)
                            if not view['accepted']:
                                self.assertEqual(view['constraints'][0]['expected'], consumed)
                                self.assertEqual(view['constraints'][0]['actual'], produced)

    def test_public_reconstruction_and_unique_valid_edits(self):
        for t in study.tasks():
            facts = study.public_facts(t['initial'], t['api'])
            self.assertEqual(set(facts), set(t['hole_lines']))
            for i, line in enumerate(t['hole_lines']):
                self.assertEqual(facts[line], t['api']['sinks'][f'sink{i}'])
            valid = [c for c in t['candidates'] if all(
                t['api']['sources'][f'source{c["choices"][i]}'] == facts[line]
                for i, line in enumerate(t['hole_lines']))]
            self.assertEqual(len(valid), 1, t['id'])
            for world in (0, 15):
                self.assertTrue(study.query(world, t['variant'], t['shape'], 'generic', valid[0]['source'])['accepted'])
                self.assertFalse(study.query(world, t['variant'], t['shape'], 'generic', t['initial'])['accepted'])

    def test_mode_privacy_on_initial_queries(self):
        for t in study.tasks():
            for mode in study.MODES:
                views = [study.query(w, t['variant'], t['shape'], mode, t['initial']) for w in (0, 7, 15)]
                self.assertEqual({v['accepted'] for v in views}, {False})
                self.assertEqual(len({study.key(v) for v in views}), 3 if mode == 'full' else 1)

    def test_strategies_accept_without_private_inputs(self):
        for t in study.tasks():
            public = {k: deepcopy(t[k]) for k in ('initial', 'candidates', 'api', 'hole_lines')}
            for arm in study.ARMS:
                history = []
                def ask(source):
                    history.append(source)
                    return study.query(7, t['variant'], t['shape'], arm.split('_')[0], source)
                repaired, n = study.repair(public, arm, ask)
                self.assertEqual(n, len(history))
                self.assertLessEqual(n, 32)
                self.assertTrue(study.query(7, t['variant'], t['shape'], 'full', repaired)['accepted'])
                if arm.endswith('_public') or arm == 'projected_local': self.assertEqual(n, 2)

    def test_parser_unstructured_and_request_controls(self):
        for source in ('int main( {', 'int main(){ unknown(); return 0; }', 'int main(){ @ return 0; }'):
            for mode in study.MODES:
                views = [study.query(w, 0, 'scalar', mode, source) for w in (0, 15)]
                self.assertFalse(views[0]['accepted'])
                self.assertEqual(views[0], views[1])
        self.assertEqual(study.query(0, 0, 'scalar', 'projected', 'int main(){unknown();return 0;}')['feedback'], 'rejected')
        for args in [('16', '0', 'scalar', 'full'), ('0', '6', 'scalar', 'full'), ('0', '0', 'scalar', 'bad')]:
            p = subprocess.run([str(study.HELPER), *args], input='', capture_output=True, text=True)
            self.assertEqual(p.returncode, 2); self.assertEqual(p.stdout, '')

    def test_infrastructure_errors_abort(self):
        bad = [SimpleNamespace(returncode=2, stderr='', stdout=''),
               SimpleNamespace(returncode=0, stderr='unexpected', stdout='{}'),
               SimpleNamespace(returncode=0, stderr='', stdout='{"accepted":true,"feedback":"ok","constraints":[{}]}')]
        for response in bad:
            with patch('study.subprocess.run', return_value=response):
                with self.assertRaises(RuntimeError): study.query(0, 0, 'scalar', 'generic', '')
        with patch('study.subprocess.run', side_effect=subprocess.TimeoutExpired('checker', 5)):
            with self.assertRaises(subprocess.TimeoutExpired): study.query(0, 0, 'scalar', 'generic', '')


if __name__ == '__main__':
    unittest.main()
