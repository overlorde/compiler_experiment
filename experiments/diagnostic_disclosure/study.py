"""Deterministic diagnostic-disclosure pilot. Private worlds stay in the driver."""
import argparse
import csv
import hashlib
import itertools
import json
import platform
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
HELPER = ROOT / 'build/diagnostic_checker'
MODES = ('full', 'redacted', 'generic', 'projected')
BUDGETS = (1, 2, 4, 8, 20)


def atom(kind):
    return {'int': '1', 'bool': '(1 == 1)', 'char': "'A'"}[kind]


def value(kind, shape):
    return atom(kind) if shape == 'scalar' else f'alloc<{kind}>(8)'


def typename(kind, shape):
    return kind if shape == 'scalar' else f'ptr<{kind}>'


def program(body, extra=''):
    return extra + '\nint main() {\n' + body + '\nreturn 0;\n}\n'


def contract_program(mask, shape, family='direct'):
    calls = [f'probe{i}({value("bool" if mask & (1 << i) else "int", shape)})' for i in range(4)]
    return wrap_calls(calls, family)


def wrap_calls(calls, family):
    if family == 'nested':
        body = '\n'.join(f'print_int({c});' for c in calls)
    elif family == 'locals':
        body = '\n'.join(f'int x{i} = {c};' for i, c in enumerate(calls))
    elif family == 'conditional':
        body = '\n'.join(f'if (1 == 1) {{ {c}; }}' for c in calls)
    elif family == 'helper':
        return program('return work();', 'int work() {\n' + '\n'.join(c + ';' for c in calls) + '\nreturn 0;\n}\n')
    else:
        body = '\n'.join(c + ';' for c in calls)
    return program(body)


def fixtures():
    result = []
    for shape in ('scalar', 'pointer'):
        for family in ('initializer', 'argument', 'return', 'assignment', 'operator'):
            t = typename('int', shape)
            call = f'probe0({value("int", shape)})'
            def source(k, family=family, call=call):
                ty = typename(k, shape)
                if family == 'argument':
                    return program(f'probe0({value(k, shape)});')
                if family == 'initializer':
                    return program(f'{ty} x = {call};')
                if family == 'return':
                    return program('work();', f'{ty} work() {{ return {call}; }}\n')
                if family == 'assignment':
                    return program(f'{ty} x = {value(k, shape)}; x = {call};')
                # int arithmetic vs bool logic, or differently typed pointer equality.
                expr = f'{call} == {value(k, shape)}' if shape == 'pointer' else (
                    f'{call} + 1' if k == 'int' else f'{call} && (1 == 1)' if k == 'bool' else f'{call} == \'A\'')
                return program(expr + ';')
            candidates = [{'hint': typename(k, shape), 'source': source(k)} for k in ('char', 'bool', 'int')]
            result.append({'id': f'metadata-{shape}-{family}', 'stratum': 'metadata', 'shape': shape,
                           'initial': source('char'), 'candidates': candidates, 'public_hint': t})
        for family in ('direct', 'nested', 'locals', 'conditional', 'helper'):
            initial = wrap_calls([f'probe{i}({value("char", shape)})' for i in range(4)], family)
            result.append({'id': f'contracts-{shape}-{family}', 'stratum': 'contracts', 'shape': shape,
                           'initial': initial, 'candidates': [{'hint': None, 'source': contract_program(m, shape, family)} for m in range(16)],
                           'public_hint': None})
    return result


def query(stratum, shape, world, mode, source):
    start = time.perf_counter_ns()
    p = subprocess.run([str(HELPER), stratum, str(world), shape, mode], input=source,
                       text=True, capture_output=True, timeout=5)
    if p.returncode or p.stderr:
        raise RuntimeError(f'checker infrastructure failure: {p.returncode}: {p.stderr}')
    view = json.loads(p.stdout)
    if set(view) != {'accepted', 'feedback', 'constraints'} or not isinstance(view['accepted'], bool):
        raise RuntimeError('invalid checker response')
    return view, (time.perf_counter_ns() - start) / 1e6


def repair(public, ask):
    """No private assignment, registry, or oracle enters this strategy."""
    response = ask(public['initial'])
    if response['accepted']:
        return public['initial'], 1
    candidates = list(public['candidates'])
    # Every arm receives the same public interface; never withhold it to force a win.
    if public['public_hint']:
        candidates.sort(key=lambda c: c['hint'] != public['public_hint'])
    else:
        # Exact type hints for the four calls can prune candidates only when given.
        expected = [d['expected'] for d in response['constraints'] if d['context'] == 'argument']
        if len(expected) == 4 and all(x in ('int', 'bool', 'ptr<int>', 'ptr<bool>') for x in expected):
            mask = sum((1 << i) for i, x in enumerate(expected) if 'bool' in x)
            candidates = [candidates[mask]]
    for n, c in enumerate(candidates, 2):
        if n > 20:
            break
        if ask(c['source'])['accepted']:
            return c['source'], n
    return None, min(20, 1 + len(candidates))


def contract_probe(shape, ask):
    """A public four-bit contract model; never consults the target world."""
    possibilities = set(range(16))
    history = []
    for i in range(4):
        source = program(f'probe{i}({value("int", shape)});')
        view = ask(source)
        possibilities = {m for m in possibilities if (not bool(m & (1 << i))) == view['accepted']}
        # The response itself may disclose this parameter; no oracle labels used.
        history.append({'source': source, 'view': view, 'queries': i + 1, 'identified': next(iter(possibilities)) if len(possibilities) == 1 else None})
    return history


def visible_key(view):
    return json.dumps(view, sort_keys=True, separators=(',', ':'))


def run(out):
    out.mkdir(parents=True, exist_ok=True)
    templates = fixtures()
    (out / 'fixtures.json').write_text(json.dumps(templates, indent=2) + '\n')
    cells, query_records, probe_records = [], [], []
    for f in templates:
        public = {k: f[k] for k in ('initial', 'candidates', 'public_hint')}
        for world in range(16):
            for mode in MODES:
                trial = f'{f["id"]}:{world}:{mode}'
                first = []
                def ask(source):
                    view, elapsed = query(f['stratum'], f['shape'], world, mode, source)
                    first.append(view)
                    query_records.append({'trial': trial, 'query': len(first), 'source': source, 'view': view, 'elapsed_ms': elapsed})
                    return view
                repaired, count = repair(public, ask)
                # Separate full-mode held-out call, not counted as strategy feedback.
                oracle_ok = repaired is not None and query(f['stratum'], f['shape'], world, 'full', repaired)[0]['accepted']
                cells.append({'template': f['id'], 'stratum': f['stratum'], 'world': world, 'mode': mode,
                              'initial': first[0], 'repair_success': oracle_ok, 'repair_queries': count})
                if f['stratum'] == 'contracts':
                    def probe_ask(source):
                        view, _ = query('contracts', f['shape'], world, mode, source)
                        return view
                    history = contract_probe(f['shape'], probe_ask)
                    probe_records.append({'template': f['id'], 'world': world, 'mode': mode, 'history': history,
                                          'correct': history[-1]['identified'] == world})
    controls = []
    control_sources = {'valid': program('print_int(1);'), 'parse_error': 'int main( {',
                       'unstructured_error': program('unknown();'), 'escaping': 'int main(){ @ return 0; }'}
    for stratum, shape, world, mode in itertools.product(('metadata', 'contracts'), ('scalar', 'pointer'), range(16), MODES):
        for name, source in control_sources.items():
            view, _ = query(stratum, shape, world, mode, source)
            controls.append({'control': name, 'stratum': stratum, 'shape': shape, 'world': world, 'mode': mode, 'view': view})
    aggregates = []
    for f in templates:
        for mode in MODES:
            group = [c for c in cells if c['template'] == f['id'] and c['mode'] == mode]
            eligible = [(a,b) for a,b in itertools.combinations(group, 2) if a['initial']['accepted'] == b['initial']['accepted']]
            distinguished = sum(visible_key(a['initial']) != visible_key(b['initial']) for a,b in eligible)
            aggregates.append({'template': f['id'], 'stratum': f['stratum'], 'mode': mode,
                               'eligible_pairs': len(eligible), 'additional_distinguished_pairs': distinguished,
                               'private_string_exposures': sum('private_' in visible_key(c['initial']) for c in group),
                               'repair_successes': sum(c['repair_success'] for c in group), 'worlds':16,
                               'mean_repair_queries': sum(c['repair_queries'] for c in group)/16,
                               **{f'repaired_by_{b}':sum(c['repair_success'] and c['repair_queries'] <= b for c in group) for b in BUDGETS}})
    for name, records in [('cells', cells), ('queries', query_records), ('probes', probe_records), ('controls', controls)]:
        (out / f'{name}.jsonl').write_text(''.join(json.dumps(r, sort_keys=True) + '\n' for r in records))
    with (out / 'aggregate.csv').open('w', newline='') as handle:
        w = csv.DictWriter(handle, fieldnames=list(aggregates[0])); w.writeheader(); w.writerows(aggregates)
    manifest = {'protocol':'D1-v1', 'seed':'ce-d1-v1-fixed', 'cells':len(cells), 'templates':20,
                'private_assignments':16, 'control_cells':len(controls), 'modes':list(MODES), 'budgets':list(BUDGETS), 'exclusions':[],
                'python': platform.python_version(), 'compiler':subprocess.check_output(['clang++','--version'],text=True).splitlines()[0],
                'git_head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
                'git_diff_sha256':hashlib.sha256(subprocess.check_output(['git','diff'],cwd=ROOT)).hexdigest(),
                'helper_sha256':hashlib.sha256(HELPER.read_bytes()).hexdigest(),
                'study_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                'fixtures_sha256':hashlib.sha256((out/'fixtures.json').read_bytes()).hexdigest(),
                'scope':'Synthetic frontend fixtures; no LLMs, native program execution, deployment prevalence, or confidentiality proof.'}
    manifest['source_hashes'] = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
                                 for p in sorted([*ROOT.glob('src/*.cpp'), *ROOT.glob('src/*.h'),
                                                  *Path(__file__).parent.glob('*.py'), *Path(__file__).parent.glob('*.cpp'),
                                                  Path(__file__).parent/'protocol.json', ROOT/'Makefile'])}
    (out / 'manifest.json').write_text(json.dumps(manifest, indent=2, sort_keys=True) + '\n')
    print(f'D1: {len(cells)} cells, {len(query_records)} repair queries, {len(probe_records)} probe sessions; no exclusions')
    return cells, aggregates


if __name__ == '__main__':
    ap = argparse.ArgumentParser(); ap.add_argument('--output', type=Path, default=ROOT/'build/diagnostic_disclosure')
    args = ap.parse_args(); run(args.output)
