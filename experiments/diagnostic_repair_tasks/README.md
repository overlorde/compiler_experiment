# D2: repair at matched private-label disclosure

The [saved plan](PLAN.md) and [local frozen protocol](protocol.json) define a
follow-up to D1. This study deliberately includes strong public reconstruction:
beating a simple diagnostic reader is insufficient to establish a repair benefit.

There are 36 public templates: six families, scalar/pointer shapes, and one to
three wrong producer selections. All arms get the same public signatures and
the same permitted edits. Only imported types' private display labels vary
across the 16 worlds. The real ce parser, scopes, and checker evaluate every
candidate. No source-language feature, generated program execution, or LLM
provider is added.

For example, public signatures can say `source0: () -> int`,
`source2: () -> bool`, and `sink0: int -> int`. The faulty call is:

```c
sink0(source2());
```

Plain redaction can obscure both sides as `expected [private], got [private]`.
Projection says `expected int, got bool`. However, a source-aware baseline can
also derive the repair `sink0(source0());` from those public signatures. The
benchmark measures that baseline rather than assuming redaction prevents it.
Declarations and other families also provide controls where redaction already
retains the expected public type.

The local search uses explicit diagnostic expected types to filter a shared
candidate order. The strong baseline first derives types from public source and
signatures using a small adapter for the released grammar. That adapter is not
a general ce parser. The generic-plus-public arm shows whether diagnostics were
needed at all. This comparison tests a reasoning-step ablation in a deterministic
strategy; it is not an LLM capability evaluation.

The 468 candidate programs are evaluated in all worlds and four modes: 29,952
observations. Full visible JSON responses are compared for every permitted
action. All 2,880 repair cells replay only their own response views. Each final
edit is checked for membership in the allowed domain and by a separate checker
invocation; those validation answers do not enter a strategy. Repairs cannot
remove calls, change required types, or replace the program with a trivial one.
Type correctness and allowed edits are the goal, not runtime behavior.

```sh
make test-diagnostic-repair
make study-diagnostic-repair
python3 -B experiments/diagnostic_repair_tasks/study.py --output build/diagnostic_repair_repeat
python3 -B experiments/diagnostic_repair_tasks/study.py --analyze \
  --repeat build/diagnostic_repair_repeat \
  --save research/agent_type_systems/diagnostic_repair_tasks
```

The analyzer replays every strategy and recomputes finite-domain disclosure from
raw observations. `--repeat` compares semantic records and manifests exactly;
`--save` exports the report, source/binary hashes, fixtures, CSV, and compressed
raw records with checksums. Infrastructure failures abort instead of becoming
rejections. Cached responses count as queries; this is not a runtime benchmark.

The families designated for layout checks use the same primitives and author
as development tasks; they are not an independent blinded holdout. Private
worlds are nested repeated configurations. The finite edit-domain comparison
does not cover arbitrary inputs, timing, process inspection, or artifacts and
does not establish a general confidentiality theorem or research novelty.

Results are saved at [the D2 report](../../research/agent_type_systems/diagnostic_repair_tasks/report.md).
