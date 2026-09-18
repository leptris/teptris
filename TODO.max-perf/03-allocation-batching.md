# 03 — Allocation batching (arena coalescing)

Research background: cpptoml's flat ~98 MB/s on every shape is the
allocation-bound cautionary tale; XGrammar's speed partly comes from
structure reuse.

Status: **measured and reverted, five variants.**
- tiny-table scan (TABLE_TINY=8), fused probe+insert (unscoped),
  seeded inline-table blocks (node + 8-entry array in one arena block),
  plain-string node+payload batching, and the fused line finish — all
  negative-or-neutral on mixed while machine scatter exceeded effects.
- CONCLUSION ledgered: mixed's residual cost is the per-record parse
  line itself, not allocation.

Partially redeemed later by SCOPING: the header-path find+insert
fusion (v0.1.20) kept the keyval path untouched and took cargo +10.3%
— the lesson is scope, not the lever class.
