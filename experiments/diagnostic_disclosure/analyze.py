"""Validate a completed pilot, compare a repeat, and produce a scoped report."""
import argparse
from collections import defaultdict
import csv
import gzip
import hashlib
import json
from pathlib import Path


def read(path):
    return [json.loads(line) for line in path.read_text().splitlines()]


def analyze(root, repeat=None, save=None):
    manifest = json.loads((root/'manifest.json').read_text())
    cells = read(root/'cells.jsonl')
    assert len(cells) == 1280
    assert len({(c['template'], c['world'], c['mode']) for c in cells}) == 1280
    assert not manifest['exclusions']
    by_trial = defaultdict(list)
    for c in cells:
        by_trial[c['template'], c['world']].append(c['initial']['accepted'])
        assert c['repair_success'] and c['repair_queries'] <= 20
    assert all(len(v) == 4 and len(set(v)) == 1 for v in by_trial.values())
    controls = read(root/'controls.jsonl')
    assert len(controls) == 1024
    public_controls = defaultdict(set)
    for c in controls:
        assert c['view']['accepted'] == (c['control'] == 'valid')
        if c['mode'] == 'projected':
            public_controls[c['control'],c['stratum'],c['shape']].add(json.dumps(c['view'],sort_keys=True))
    assert all(len(v) == 1 for v in public_controls.values())
    rows = list(csv.DictReader((root/'aggregate.csv').open()))
    assert len(rows) == 80
    summary = []
    for stratum in ('metadata','contracts'):
        for mode in ('full','redacted','generic','projected'):
            group = [r for r in rows if r['stratum'] == stratum and r['mode'] == mode]
            assert len(group) == 10
            totals = {k:sum(int(r[k]) for r in group) for k in ('eligible_pairs','additional_distinguished_pairs','private_string_exposures','repair_successes','worlds','repaired_by_2','repaired_by_4','repaired_by_20')}
            totals.update(stratum=stratum,mode=mode,mean_repair_queries=sum(float(r['mean_repair_queries']) for r in group)/10)
            summary.append(totals)
            if mode == 'projected':
                assert totals['additional_distinguished_pairs'] == totals['private_string_exposures'] == 0
    probes = read(root/'probes.jsonl')
    assert len(probes) == 640 and all(p['correct'] and len(p['history']) == 4 for p in probes)
    verified = False
    if repeat:
        for filename in ('fixtures.json','cells.jsonl','probes.jsonl','controls.jsonl','aggregate.csv','manifest.json'):
            assert (root/filename).read_bytes() == (repeat/filename).read_bytes(), filename
        a,b = read(root/'queries.jsonl'),read(repeat/'queries.jsonl')
        for seq in (a,b):
            for q in seq: q.pop('elapsed_ms')
        assert a == b, 'semantic query logs differ'
        verified = True
    report = '''# Diagnostic disclosure pilot results

Executed local deterministic pilot, November 17, 2026. The experiment uses the
real ce frontend with synthetic imported signatures and display metadata. It
runs no LLMs and executes no generated code.

All 1,280 planned cells and 1,024 control cells completed without exclusions.
Each template has 16 private worlds and four feedback modes. The table counts
private-world pairs distinguishable by the initial fixed query's diagnostics
when their public interfaces and verdicts agree. Each stratum has 1,200 eligible
pairs, nested inside ten template families; pairs are not independent samples.

| Stratum | Feedback | Additional distinctions | Literal label exposures | Mean repair queries | Repaired within 2 queries |
| --- | --- | --- | --- | --- | --- |
'''
    for s in summary:
        report += f'| {s["stratum"]} | {s["mode"]} | {s["additional_distinguished_pairs"]}/{s["eligible_pairs"]} | {s["private_string_exposures"]}/{s["worlds"]} | {s["mean_repair_queries"]:.1f} | {s["repaired_by_2"]}/{s["worlds"]} |\n'
    report += f'''
The separate 640 scripted probe sessions all identified their four-bit contract
using four verdict queries. Thus generic feedback does not keep these contracts
secret under the permitted query interface. With the repair strategy's initial
all-invalid query, detailed contract errors permit a successful repair in two
queries, while finite verdict-only enumeration averages 9.5. All repairs succeed
within the 20-query budget. This is a bounded latency/query tradeoff in a fixed
strategy, not evidence about LLM usefulness.

In metadata fixtures, the public interface already specifies the necessary type.
Every strategy receives that information, so all four modes repair in two
queries. Redaction and projection both remove the measured private-world
distinctions. Projection supplies no measured repair advantage over the simpler
redaction baseline in this fixture set. This result does not support a novelty
claim for the projection mechanism.

Projected metadata views were identical across all 16 worlds. Projected contract
views equal verdict-only views. Valid programs, public parse failures, JSON
escaping cases, and unstructured checker failures passed controls. The structured
callback was also tested for snapshot lifetime, invocation before text delivery,
legacy text compatibility, listener removal, and error counts.

Two complete runs have matching semantic records, aggregate tables, fixtures and
manifests: **{verified}**. Per-query elapsed times are excluded from that comparison;
they include process startup and contention and are not used to claim a speedup.

## Interpretation and limits

These are constructed channel/conformance cases, not estimates of deployed
leakage frequency. Metadata privacy means private display labels, not a new ce
module abstraction. The private-contract projection deliberately returns generic
checker failures; it is not a general confidential type checker. All fixtures
and secrets are synthetic and released for reproducibility; each query's assigned
world and the analysis oracle are withheld from the scripted strategy API.

The primary distinction metric uses the initial fixed public query. Adaptive
contract recovery is reported separately with complete query/response traces;
it is not presented as transcript-wide noninterference. The finite world tests
establish no theorem about arbitrary secrets, inputs, timing, exceptions,
artifacts, process inspection, or a hostile Python strategy. There is no held-out
LLM trial or population confidence interval. No opaque compiler policy or
capability system was added to ce.

The next research decision is to study whether more realistic public interfaces
leave an incremental diagnostic channel worth restricting. This pilot does not
justify building a new diagnostic type system or claiming improved agent safety.
'''
    (root/'report.md').write_text(report)
    (root/'summary.json').write_text(json.dumps({'semantic_repeat_verified':verified,'groups':summary},indent=2)+'\n')
    if save:
        save.mkdir(parents=True,exist_ok=True)
        for filename in ('report.md','summary.json','manifest.json','fixtures.json','aggregate.csv'):
            (save/filename).write_bytes((root/filename).read_bytes())
        for filename in ('cells.jsonl','queries.jsonl','probes.jsonl','controls.jsonl'):
            (save/(filename+'.gz')).write_bytes(gzip.compress((root/filename).read_bytes(),mtime=0))
        hashes = {p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(save.iterdir()) if p.name != 'sha256.json'}
        (save/'sha256.json').write_text(json.dumps(hashes,indent=2)+'\n')
    print(report)


if __name__ == '__main__':
    p=argparse.ArgumentParser(); p.add_argument('results',type=Path); p.add_argument('--repeat',type=Path); p.add_argument('--save',type=Path)
    a=p.parse_args(); analyze(a.results,a.repeat,a.save)
