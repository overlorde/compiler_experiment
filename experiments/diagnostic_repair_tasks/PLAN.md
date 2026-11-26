# D2: repair at matched private-label disclosure

1. Freeze the task grammar, public information, repair strategies, and decision
   rule in `protocol.json` before collecting measurements.
2. Use the real ce frontend with three synthetic producers and three consumers.
   Publish every canonical signature to every arm. Vary only private display
   labels across 16 worlds; do not hide a needed public signature from a baseline.
3. Generate six families: consumer arguments, declarations, assignments, helper
   returns, buffer stores, and conditional calls. Cross scalar/pointer types with
   one/two/three erroneous producer selections: 36 templates. Repairs may change
   producer selections only; deleting work or changing required types is invalid.
4. Enumerate all 3^n permitted edits for each template. Check all candidates in
   all worlds and four feedback modes. Compare complete visible JSON replies,
   not just literal private strings. Equal replies for every allowed action
   also protect adaptive transcripts restricted to this finite action domain.
   Timing, process inspection, arbitrary ce inputs, and generated artifacts are
   outside this claim.
5. Run a shared finite repair search with plain redaction, generic feedback,
   and projection. Add redaction plus public reconstruction and generic feedback
   plus the same public reconstruction as strong baselines. Reconstruction uses
   public source/signatures only. All arms receive those inputs; this is an
   explicit strategy ablation, not a claim that a redacted agent cannot reason.
6. Confirm every proposed repair with a separate frontend invocation and verify
   membership in the allowed edit domain. Record query counts including the
   initial failure, success at fixed budgets, and all query references.
7. Report layout-check families separately from development families. They share
   primitives and are designed by the same author; they are not an independently
   sampled or blinded holdout. A credible superiority result must beat public
   reconstruction at equal measured private-label disclosure. A win only over
   plain redaction is a representation/heuristic result.
8. Repeat the full run, compare semantic artifacts, save compressed raw records,
   source/binary hashes, summaries, and the report. Preserve the earlier D1 pilot.

Expected challenge: since this fragment has public, concrete signatures, public
reconstruction can solve the type constraints without private information.
Projection may beat a limited diagnostic reader but should tie a strong public
solver. The experiment is allowed to reject the superiority hypothesis.
