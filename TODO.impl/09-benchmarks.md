# 09 — Benchmarks: matrix, CI, ledger

Depends: 07. Status: v1 COMPLETE — parse matrix vs six reference
libraries (tomlc99, tomlc17, cpptoml, toml11, tomlplusplus); two
artifact runs recorded (2026-09-12/13, M1 Max). Ruby end-to-end tier
lands with item 06.

## Goal

Artifact numbers, not claims. Two tiers, yeptris item-18 discipline.

## Design

- **C tier (done).** `benchmarks/bench_parse` runs every library over
  the deterministic corpus (`scripts/gen_bench_corpus.py`, seed 42):
  parse and destroy timed separately, min-of-N, MB/s, and a top-level
  key-count cross-check across libraries. References pinned in
  `~/src/external`: tomlc99 @29076df, tomlc17 @d7e91db (C11, strict
  1.0), cpptoml @fededad (v0.5-era), toml11 v4.2.0, tomlplusplus
  v3.4.0. Headline (see `benchmarks/LEDGER.md` for the full matrix):
  teptris 118–291 MB/s — fastest on every shape; 2.0–2.8× tomlc17
  (best competitor), 1.9–3.5× cpptoml where it parses, 3.2–7.5×
  tomlplusplus, 100–1000× toml11.
- Perf work landed with the matrix (artifact deltas in the ledger):
  ryu d2s/s2d port (float emit 16.7×; attribution preserved in
  `src/teptris/common/ryu/`), escape-free string fast path
  (scalar_string 2.6×), memchr advance.
- Ledgered findings: tomlc99 quadratic in keys-per-table; tomlc17 caps
  table size (rejects the 40–60k-key shapes); cpptoml rejects
  heterogeneous arrays (v0.5 semantics); tomlc99/tomlc17 symbol
  collision handled by renaming (`tepc17_*`).

## Acceptance gates

- `benchmarks/run.sh` produces the JSON artifact matrix from a clean
  build; ledger file updated with the first full run. — MET
  (`scripts/run_benchmarks.sh` → `benchmarks/results/bench_parse.json`,
  `benchmarks/LEDGER.md`).
- No perf claim in any README/TODO without an artifact number +
  the command that produced it. — holds.
- Remaining for full closure: memory-peak measures, CI bench job with
  baseline drift alerting, Ruby end-to-end tier (needs item 06).
