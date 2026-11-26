"""D2 finite repair tasks; strategies receive public data and their own views only."""
import argparse
from collections import defaultdict
from copy import deepcopy
import csv
import gzip
import hashlib
import itertools
import json
from pathlib import Path
import platform
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
HELPER = ROOT / 'build/diagnostic_repair_checker'
KINDS = ('int', 'bool', 'char')
PERMUTATIONS = tuple(itertools.permutations(KINDS))
FAMILIES = ('arguments', 'initializers', 'assignments', 'returns', 'stores', 'conditional')
MODES = ('full', 'redacted', 'generic', 'projected')
ARMS = ('redacted_local', 'generic_local', 'projected_local', 'redacted_public', 'generic_public')
BUDGETS = (2, 4, 8, 16, 32)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def kind_name(kind, shape):
    return kind if shape == 'scalar' else f'ptr<{kind}>'


def api(variant, shape):
    return {'sources': {f'source{i}': kind_name(k, shape) for i, k in enumerate(PERMUTATIONS[(variant+1) % 6])},
            'sinks': {f'sink{i}': kind_name(k, shape) for i, k in enumerate(PERMUTATIONS[variant])}}


def literal(ty):
    if ty.startswith('ptr<'):
        return f'alloc<{ty[4:-1]}>(8)'
    return {'int': '1', 'bool': '(1 == 1)', 'char': "'A'"}[ty]


def tasks():
    result = []
    for fi, family in enumerate(FAMILIES):
        for si, shape in enumerate(('scalar', 'pointer')):
            for n in (1, 2, 3):
                variant = (2*fi + 3*si + n-1) % 6
                interface = api(variant, shape)
                task_id = f'{family}-{shape}-{n}'
                body, helpers = [], []
                for i in range(n):
                    ty = interface['sinks'][f'sink{i}']
                    value = f'@@{i}@@()'
                    if family == 'arguments': body.append(f'sink{i}({value});')
                    elif family == 'initializers': body.append(f'{ty} local{i} = {value};')
                    elif family == 'assignments':
                        body.extend((f'{ty} local{i} = {literal(ty)};', f'local{i} = {value};'))
                    elif family == 'returns':
                        helpers.extend((f'{ty} helper{i}() {{', f'return {value};', '}'))
                        body.append(f'helper{i}();')
                    elif family == 'stores': body.append(f'store(alloc<{ty}>(8), {value});')
                    else: body.append(f'if (1 == 1) {{ sink{i}({value}); }}')
                template = '\n'.join(helpers + ['int main() {'] + body + ['return 0;', '}', ''])
                hole_lines = [next(i for i, line in enumerate(template.splitlines(), 1) if f'@@{h}@@' in line) for h in range(n)]

                def render(choices):
                    source = template
                    for h, choice in enumerate(choices): source = source.replace(f'@@{h}@@', f'source{choice}')
                    return source

                choices = list(itertools.product(range(3), repeat=n))
                choices.sort(key=lambda c: sha(f'{task_id}:{c}'.encode()))
                candidates = [{'choices': list(c), 'source': render(c)} for c in choices]
                # Every initial selection has the wrong public type. This is
                # fixture generation, not information supplied to the strategy.
                initial_choices = []
                for i in range(n):
                    required = PERMUTATIONS[variant][i]
                    wrong = KINDS[(KINDS.index(required)+1) % 3]
                    initial_choices.append(PERMUTATIONS[(variant+1) % 6].index(wrong))
                initial = render(initial_choices)
                result.append({'id': task_id, 'family': family, 'shape': shape, 'holes': n, 'variant': variant,
                               'split': 'development' if fi < 3 else 'layout_check', 'api': interface,
                               'hole_lines': hole_lines, 'initial': initial, 'candidates': candidates})
    return result


def query(world, variant, shape, mode, source):
    p = subprocess.run([str(HELPER), str(world), str(variant), shape, mode], input=source,
                       text=True, capture_output=True, timeout=5)
    if p.returncode or p.stderr:
        raise RuntimeError(f'checker infrastructure failure: {p.returncode}: {p.stderr}')
    v = json.loads(p.stdout)
    if (not isinstance(v, dict) or set(v) != {'accepted', 'feedback', 'constraints'} or
        not isinstance(v['accepted'], bool) or not isinstance(v['feedback'], str) or
        not isinstance(v['constraints'], list)):
        raise RuntimeError('malformed checker response')
    for f in v['constraints']:
        if (not isinstance(f, dict) or set(f) != {'code', 'line', 'context', 'expected', 'actual'} or
            f['code'] != 'type_mismatch' or type(f['line']) is not int or
            any(not isinstance(f[k], str) for k in ('context', 'expected', 'actual'))):
            raise RuntimeError('malformed diagnostic constraint')
    return v


def public_facts(source, interface):
    """Small syntax adapter for the released task grammar, not a ce parser.

    It consults no diagnostics, private descriptors, fixture goal, or world ID.
    The same adapter is used by redacted_public and generic_public.
    """
    facts, locals_, return_type = {}, {}, None
    for number, line in enumerate(source.splitlines(), 1):
        header = re.fullmatch(r'([a-z<>]+) helper\d+\(\) \{', line)
        if header: return_type = header[1]
        declaration = re.match(r'([a-z<>]+) (local\d+) = ', line)
        if declaration: locals_[declaration[2]] = declaration[1]
        if not re.search(r'source\d\(\)', line): continue
        sink = re.search(r'sink(\d)\(', line)
        store = re.search(r'store\(alloc<([a-z<>]+)>\(8\), source', line)
        assignment = re.match(r'(local\d+) = ', line)
        if sink: required = interface['sinks']['sink'+sink[1]]
        elif declaration: required = declaration[1]
        elif store: required = store[1]
        elif assignment: required = locals_[assignment[1]]
        elif line.startswith('return ') and return_type: required = return_type
        else: raise ValueError('source outside public reconstruction grammar')
        facts[number] = required
    return facts


def repair(public, arm, ask):
    """Finite local search; public reconstruction is an explicit ablation."""
    response = ask(public['initial'])
    if response['accepted']: return public['initial'], 1
    known = public_facts(public['initial'], public['api']) if arm.endswith('_public') else {}
    allowed_types = set(public['api']['sources'].values())
    remaining = [c for c in public['candidates'] if c['source'] != public['initial']]
    for count in range(2, 33):
        for fact in response['constraints']:
            if fact['expected'] in allowed_types:
                known[fact['line']] = fact['expected']
        remaining = [c for c in remaining if all(
            public['api']['sources'][f'source{c["choices"][i]}'] == known[line]
            for i, line in enumerate(public['hole_lines']) if line in known)]
        if not remaining: return None, count-1
        chosen = remaining.pop(0)
        response = ask(chosen['source'])
        if response['accepted']: return chosen['source'], count
    return None, 32


def key(view):
    return json.dumps(view, sort_keys=True, separators=(',', ':'))


def dump(path, data):
    path.write_text(json.dumps(data, indent=2, sort_keys=True)+'\n')


def records(path, data):
    path.write_text(''.join(json.dumps(r, sort_keys=True)+'\n' for r in data))


def run(out):
    out.mkdir(parents=True, exist_ok=True)
    fixtures = tasks()
    dump(out/'fixtures.json', fixtures)
    observations, cells, traces, oracles, privacy = [], [], [], [], []
    for ti, task in enumerate(fixtures):
        views = {}
        for ci, candidate in enumerate(task['candidates']):
            mode_views = defaultdict(list)
            for world in range(16):
                verdicts = []
                for mode in MODES:
                    view = query(world, task['variant'], task['shape'], mode, candidate['source'])
                    views[ci, world, mode] = view
                    mode_views[mode].append(view)
                    verdicts.append(view['accepted'])
                    observations.append({'task': task['id'], 'candidate': ci, 'world': world, 'mode': mode, 'view': view})
                assert len(set(verdicts)) == 1, 'feedback mode changed verdict'
            for mode in MODES:
                group = mode_views[mode]
                assert len({v['accepted'] for v in group}) == 1, 'metadata world changed semantics'
                distinguished = sum(key(a) != key(b) for a, b in itertools.combinations(group, 2))
                privacy.append({'task': task['id'], 'candidate': ci, 'mode': mode, 'eligible_pairs': 120,
                                'additional_distinctions': distinguished})
                if mode != 'full': assert distinguished == 0, 'finite-domain privacy failed'
        public = {k: deepcopy(task[k]) for k in ('initial', 'candidates', 'api', 'hole_lines')}
        indices = {c['source']: i for i, c in enumerate(task['candidates'])}
        verified = {}
        for world in range(16):
            for arm in ARMS:
                mode = arm.split('_')[0]
                trial = f'{task["id"]}:{world}:{arm}'
                history = []
                def ask(source):
                    ci = indices[source]
                    history.append(ci)
                    # Cached views are still counted as frontend queries. Deep
                    # copies prevent strategy mutation of another arm's views.
                    return deepcopy(views[ci, world, mode])
                source, count = repair(public, arm, ask)
                assert source in indices and count == len(history), 'invalid repair domain/count'
                if (world, source) not in verified:
                    # Separate process, never returned to repair().
                    view = query(world, task['variant'], task['shape'], 'full', source)
                    verified[world, source] = view['accepted']
                    oracles.append({'task': task['id'], 'world': world, 'candidate': indices[source], 'accepted': view['accepted']})
                success = verified[world, source]
                assert success, 'verifier rejected repair'
                cells.append({'task': task['id'], 'family': task['family'], 'split': task['split'], 'shape': task['shape'],
                              'holes': task['holes'], 'world': world, 'arm': arm, 'repair_queries': count, 'success': success})
                traces.append({'trial': trial, 'mode': mode, 'candidates': history})
        print(f'D2: {ti+1}/{len(fixtures)} templates audited', flush=True)
    for name, data in [('observations', observations), ('cells', cells), ('traces', traces), ('oracles', oracles), ('privacy', privacy)]:
        records(out/(name+'.jsonl'), data)
    source_files = sorted([*ROOT.glob('src/*.cpp'), *ROOT.glob('src/*.h'), *HERE.glob('*.py'),
                           *HERE.glob('*.cpp'), *HERE.glob('*.json'), *HERE.glob('*.md'), ROOT/'Makefile'])
    manifest = {'protocol': 'D2-v1', 'templates': len(fixtures), 'repair_cells': len(cells),
                'observation_queries': len(observations), 'separate_verifier_queries': len(oracles), 'exclusions': [],
                'python': platform.python_version(), 'compiler': subprocess.check_output(['clang++','--version'],text=True).splitlines()[0],
                'helper_sha256': sha(HELPER.read_bytes()), 'fixtures_sha256': sha((out/'fixtures.json').read_bytes()),
                'git_head': subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
                'git_diff_sha256': sha(subprocess.check_output(['git','diff'],cwd=ROOT)),
                'source_hashes': {str(p.relative_to(ROOT)): sha(p.read_bytes()) for p in source_files}}
    dump(out/'manifest.json', manifest)
    print(f'D2 complete: {len(cells)} repair cells, {len(observations)} finite-domain observations', flush=True)


def read(path):
    return [json.loads(line) for line in path.read_text().splitlines()]


def analyze(out, repeat=None, save=None):
    manifest = json.loads((out/'manifest.json').read_text())
    assert manifest['protocol'] == 'D2-v1' and not manifest['exclusions']
    fixtures = json.loads((out/'fixtures.json').read_text())
    cells, privacy = read(out/'cells.jsonl'), read(out/'privacy.jsonl')
    assert len(fixtures) == 36 and len(cells) == 2880
    assert len({(c['task'], c['world'], c['arm']) for c in cells}) == 2880
    assert all(c['success'] and 2 <= c['repair_queries'] <= 32 for c in cells)
    observations, traces, oracles = read(out/'observations.jsonl'), read(out/'traces.jsonl'), read(out/'oracles.jsonl')
    expected_observations = sum(len(f['candidates']) for f in fixtures)*16*4
    assert manifest['repair_cells'] == len(cells) and manifest['observation_queries'] == expected_observations
    assert len(observations) == expected_observations
    lookup = {(o['task'], o['candidate'], o['world'], o['mode']): o['view'] for o in observations}
    assert len(lookup) == expected_observations
    assert len(traces) == len(cells) and len(oracles) == 576 and all(o['accepted'] for o in oracles)
    by_task = {f['id']: f for f in fixtures}
    cell_lookup = {f'{c["task"]}:{c["world"]}:{c["arm"]}': c for c in cells}
    for t in traces:
        c = cell_lookup[t['trial']]
        assert len(t['candidates']) == c['repair_queries']
        views = [lookup[c['task'], ci, c['world'], t['mode']] for ci in t['candidates']]
        assert not any(v['accepted'] for v in views[:-1]) and views[-1]['accepted']
        public = {k: deepcopy(by_task[c['task']][k]) for k in ('initial', 'candidates', 'api', 'hole_lines')}
        index = {x['source']: i for i, x in enumerate(public['candidates'])}
        replay = []
        def ask(source):
            ci = index[source]; replay.append(ci)
            return deepcopy(lookup[c['task'], ci, c['world'], t['mode']])
        _, count = repair(public, c['arm'], ask)
        assert replay == t['candidates'] and count == c['repair_queries'], 'strategy replay differs'
    assert len(privacy) == expected_observations//16
    for p in privacy:
        group = [lookup[p['task'], p['candidate'], w, p['mode']] for w in range(16)]
        assert len({v['accepted'] for v in group}) == 1
        actual = sum(key(a) != key(b) for a, b in itertools.combinations(group, 2))
        assert actual == p['additional_distinctions'] and p['eligible_pairs'] == 120
        if p['mode'] != 'full': assert actual == 0
    for f in fixtures:
        for arm in ARMS:
            assert len({c['repair_queries'] for c in cells if c['task'] == f['id'] and c['arm'] == arm}) == 1
    summary = []
    for split in ('all', 'development', 'layout_check'):
        for arm in ARMS:
            group = [c for c in cells if c['arm'] == arm and (split == 'all' or c['split'] == split)]
            summary.append({'split': split, 'arm': arm, 'tasks': len(group)//16,
                            'mean_queries': sum(c['repair_queries'] for c in group)/len(group),
                            **{f'success_by_{b}': sum(c['repair_queries'] <= b for c in group)/len(group) for b in BUDGETS}})
    disclosure = {mode: {'eligible_pairs': sum(p['eligible_pairs'] for p in privacy if p['mode'] == mode),
                         'additional_distinctions': sum(p['additional_distinctions'] for p in privacy if p['mode'] == mode)} for mode in MODES}
    comparison = {s['arm']: s for s in summary if s['split'] == 'layout_check'}
    family_utility = []
    for family in FAMILIES:
        for arm in ARMS:
            group = [c for c in cells if c['family'] == family and c['arm'] == arm]
            assert len(group) == 96
            family_utility.append({'family': family, 'arm': arm, 'tasks': 6,
                                   'success_by_2': sum(c['repair_queries'] <= 2 for c in group)/96,
                                   'mean_queries': sum(c['repair_queries'] for c in group)/96})
    delta = comparison['projected_local']['success_by_2'] - comparison['redacted_public']['success_by_2']
    family_deltas = {}
    for family in FAMILIES[3:]:
        rates = {a: sum(c['repair_queries'] <= 2 for c in cells if c['family'] == family and c['arm'] == a)/96
                 for a in ('projected_local', 'redacted_public')}
        family_deltas[family] = rates['projected_local']-rates['redacted_public']
    superior = (delta >= .10 and all(x >= 0 for x in family_deltas.values()) and
                disclosure['projected']['additional_distinctions'] == disclosure['redacted']['additional_distinctions'] == 0)
    repeated = False
    if repeat:
        for filename in ('fixtures.json', 'manifest.json', 'observations.jsonl', 'cells.jsonl', 'traces.jsonl', 'oracles.jsonl', 'privacy.jsonl'):
            assert (out/filename).read_bytes() == (repeat/filename).read_bytes(), filename
        repeated = True
    report = '''# D2 repair tasks: matched private-label disclosure

This synthetic ce frontend experiment publishes the same canonical signatures
and permitted edits to all arms. There are 36 templates: six families, scalar
and pointer shapes, and one/two/three wrong producer selections. Each template
has 16 private display-label worlds. Repairs preserve all code except producer
selections. Separate checker invocations verify the repaired programs; there is
no runtime execution, LLM evaluation, or claim of functional correctness.

The candidate domain contains 468 public programs across the templates. All
29,952 candidate/world/mode responses were checked. For each fixed allowed
query, all 16 worlds have the same verdict. Disclosure below counts additional
private-world pair distinctions in complete visible JSON, over that entire
finite domain, not merely literal labels or a single initial query.

| Feedback | Distinguishable pairs | Eligible pairs |
| --- | --- | --- |
'''
    for mode, d in disclosure.items():
        report += f'| {mode} | {d["additional_distinctions"]} | {d["eligible_pairs"]} |\n'
    report += '''
Equal replies for every permitted action also imply equal transcripts for an
adaptive strategy restricted to these edits, since its next action depends on
the same public information and equal previous replies. This finite check says
nothing about other ce programs, timing, process inspection, or artifacts.

Every strategy receives the same public source, signatures, candidate list, and
ordering. The local reader filters candidates using explicit diagnostic types.
The public solver reconstructs required types from the released source grammar
and signatures before the same search. It consults neither private labels nor
oracle answers. Redacted/generic local readers deliberately lack that reasoning
step: their results are strategy ablations, not bounds on capable repair agents.

| Split | Arm | Public tasks | Success by 2 queries | Mean queries |
| --- | --- | --- | --- | --- |
'''
    for s in summary:
        report += f'| {s["split"]} | {s["arm"]} | {s["tasks"]} | {s["success_by_2"]:.1%} | {s["mean_queries"]:.3f} |\n'
    report += '''
Family-level mean query counts (six public tasks per family):

| Family | Plain redaction | Generic | Projection | Redaction + public solver | Generic + public solver |
| --- | --- | --- | --- | --- | --- |
'''
    for family in FAMILIES:
        means = {s['arm']: s['mean_queries'] for s in family_utility if s['family'] == family}
        report += '| '+family+' | '+' | '.join(f'{means[a]:.3f}' for a in ARMS)+' |\n'
    report += f'''
Queries include the initial failed check. Cached observations count as queries;
runtime is not a performance endpoint. All 2,880 repairs succeed by budget 32,
and 576 separate verification invocations accept their final edits.

The frozen superiority rule requires at least a 10 percentage-point gain in
layout-check success by budget 2 over redaction plus public reconstruction,
with zero extra private distinctions and no loss in any layout-check family.
Observed difference: **{delta:.1%}**. Rule satisfied: **{superior}**.

A gain over plain redaction alone is insufficient evidence of a new repair
capability: public reconstruction is an available baseline. In this concrete,
fully public type fragment it can derive the needed types without diagnostics.
Any local-reader gain therefore measures information presentation to that
limited heuristic. It does not establish a benefit for LLMs or superiority over
a strong source-aware repairer.

Development families are arguments, initializers, and assignments. Layout-check
families are returns, stores, and conditional calls. These layouts share the
same primitives and author; they are not a blinded independent holdout. Private
worlds are repeated nuisance configurations, not independent tasks. No population
confidence interval or novelty claim is inferred from these constructed cases.

Two full runs match all semantic artifacts: **{repeated}**. The previous D1
pilot is preserved separately. The plan and local frozen protocol are saved in
`experiments/diagnostic_repair_tasks/PLAN.md` and `protocol.json`.
'''
    result = {'protocol': 'D2-v1', 'semantic_repeat_verified': repeated, 'superiority_rule_satisfied': superior,
              'layout_check_success_by_2_delta': delta, 'layout_check_family_deltas': family_deltas,
              'disclosure': disclosure, 'utility': summary, 'family_utility': family_utility}
    dump(out/'summary.json', result)
    with (out/'aggregate.csv').open('w', newline='') as h:
        writer = csv.DictWriter(h, fieldnames=list(summary[0])); writer.writeheader(); writer.writerows(summary)
    (out/'report.md').write_text(report)
    if save:
        save.mkdir(parents=True, exist_ok=True)
        for name in ('fixtures.json', 'manifest.json', 'summary.json', 'aggregate.csv', 'report.md'):
            (save/name).write_bytes((out/name).read_bytes())
        for name in ('observations', 'cells', 'traces', 'oracles', 'privacy'):
            (save/(name+'.jsonl.gz')).write_bytes(gzip.compress((out/(name+'.jsonl')).read_bytes(), mtime=0))
        dump(save/'sha256.json', {p.name: sha(p.read_bytes()) for p in sorted(save.iterdir()) if p.name != 'sha256.json'})
    print(report)
    return result


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--output', type=Path, default=ROOT/'build/diagnostic_repair_tasks')
    ap.add_argument('--analyze', action='store_true')
    ap.add_argument('--repeat', type=Path)
    ap.add_argument('--save', type=Path)
    args = ap.parse_args()
    if args.analyze: analyze(args.output, args.repeat, args.save)
    else: run(args.output)
