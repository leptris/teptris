# 19 — Restructured main line loop (batch dispatch per line)

ANALYZED, CLOSED (2026-09-18) — do not implement without a new
mandate gap.

For most of the cycle this was "the one open path" for mixed 4x:
the ledger (2026-09-16) concluded mixed's residual cost IS the
per-record line (dispatch + advance + ws scan), and only a
restructured loop could clear the scatter.

Why closed:
1. The mandate gap is gone. v0.1.19-0.1.21 took mixed ~+40% and
   cargo ~+50% (lane-measured); no sub-4x shape remains on any
   trusted measurement. The lever that would have been necessary
   is now optional.
2. The class regressed TWICE on both cells: the inlined line
   skeleton (8th lever: mixed -3.7%/-1.3%) and the fused line
   finish (mixed -1.5%/table -10%). The consistent finding: the
   compiler already compiles the call-based loop better than any
   hand-restructured shape tried; straight-line additions cost more
   than the calls they remove.
3. No mechanism. A single-threaded scalar parser that is
   branch-predicted and cache-resident on its input has nothing to
   batch — there is no second pass to fuse and no lookahead to
   exploit that the fused scans (07/08/16) have not already taken.

Reopen only if: a new bench shape drops sub-mandate AND profiles
attribute its cost to the loop skeleton itself (not dispatch,
which 01 covers, or table management, which 03/16 closed).
