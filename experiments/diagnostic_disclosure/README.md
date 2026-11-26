# Diagnostic disclosure pilot

This local deterministic study uses ce's actual parser, scopes, and checker with
synthetic private imported-function signatures. It compares full diagnostics,
registered-string redaction, generic verdicts, and a structured public projection.
No LLM provider, LLVM backend, or generated program execution is used.

```sh
make test-diagnostic-disclosure
make study-diagnostic-disclosure
python3 -B experiments/diagnostic_disclosure/study.py --output build/diagnostic_disclosure_repeat
python3 -B experiments/diagnostic_disclosure/analyze.py build/diagnostic_disclosure \
  --repeat build/diagnostic_disclosure_repeat
```

Python 3 and a C++17 compiler are required. The helper links frontend sources
without LLVM. The root Makefile still evaluates its existing LLVM configuration
commands. `make test` runs the existing compiler regressions; the focused target
above runs the diagnostic API and study conformance tests.

## Design

`protocol.json` defines the fixed D1-v1 pilot before the final reproducibility
runs. There are 20 templates, 16 private worlds per template, and four feedback
modes (1,280 cells). Four additional controls across both strata, both shapes,
all worlds, and all modes produce 1,024 control cells.

Metadata fixtures vary internal type-display labels while keeping semantics
constant. Five error contexts each have scalar and pointer variants. The public
interface exposes canonical types to every strategy. These display aliases are
host-provided frontend descriptors, not new source-language type declarations.

Contract fixtures vary four imported argument types between int/bool or
ptr<int>/ptr<bool>. Five calling contexts each have scalar/pointer variants.
Return types and arity stay fixed. The public interface describes the four-bit
contract hypothesis space without revealing the assigned combination.

The initial fixed public query determines the primary private-pair distinction
metric. The strategy then enumerates a common finite repair list, using public
interface knowledge and only explicit diagnostic constraints to prune candidates.
Budget checkpoints are 1, 2, 4, 8, and 20. Success is confirmed by a separate
frontend invocation withheld from the strategy.

A separate scripted adaptive probe uses four public bit queries to infer the
contract from verdicts. Its response filtering depends only on the public
hypothesis space and observed answers, never on the assigned world. This is an
oracle-recovery control, not a claim that all possible adaptive attacks were
covered. Queries and visible responses are saved for independent inspection.

## Boundaries

`checker.cpp` runs once per query, reading source on stdin. The trusted driver
supplies stratum, world, shape, and feedback mode as command-line arguments;
only the JSON view reaches the scripted strategy. The script API does not give
the strategy the assigned world, private registry, or verification oracle. This
is not isolation against hostile Python introspection or process inspection.

The optional compiler diagnostic callback receives borrowed type fields before
legacy formatting. The helper snapshots names and canonical shapes inside that
callback. Existing text sinks and error counting retain their behavior.

Metadata projection strips private display names using the public shape promised
by that fixture's interface. Contract projection returns only a generic checker
verdict, since the expected type is private. An unstructured checker failure
falls back to generic rejection; projected parser errors are public input errors.
There is no arbitrary secret-label inference or general confidentiality theorem.

Subprocess failures, stderr, timeouts, and malformed helper responses abort the
study as infrastructure errors. They are not normalized into rejections or
silently excluded. Valid repairs, parse errors, unstructured failures, invalid
requests, and JSON escaping have explicit conformance tests.

## Artifacts and results

The driver writes fixtures, cell/query/probe/control JSONL, aggregate CSV, and a
manifest of source/toolchain/binary hashes and exclusions. `analyze.py` checks
coverage, mode verdict parity, projection equivalence, control outcomes, and
repair verification. With `--repeat` it compares all semantic records and
aggregates; elapsed subprocess timings are excluded. With `--save PATH` it saves
the report, summary, manifest, fixtures, CSV, and gzip-compressed raw logs with
SHA-256 checksums.

The completed pilot is saved at
[the research report](../../research/agent_type_systems/diagnostic_disclosure_pilot/report.md).
It supports a finite channel/conformance comparison only. It does not estimate
production prevalence, establish LLM repair usefulness, or prove security.
