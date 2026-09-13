# 07 — Conformance: toml-test, differentials, roundtrip property

Depends: 03, 05. Status: v1 complete for the inline corpus; fetched
toml-test runner pending network-pinned corpora fetch.

## Goal

The verification matrix from the board, executed.

## Design

- **Inline corpus (v1, checked in).** `test/corpus/` cases as C string
  literals or data files: valid → expected typed-JSON trees; invalid
  → expected status. Covers every grammar rule and every define-
  semantics rule from item 03 with at least one positive and one
  negative case. Runs offline in ctest.
- **toml-test (BurntSushi), pinned.** `scripts/fetch-corpora.sh`
  pins a commit into `corpus/` (gitignored). Driver: for `valid/*`
  run `teptris to-json` and compare *semantically* against the
  expected JSON (parse both, deep-compare); for `invalid/*` require
  a non-zero exit with a syntax/semantic error. Report a divergence
  ledger like yeptris item 17 — every divergence documented, waived,
  or fixed; 100% is the bar, waivers are explicit.
- **Differentials.** Same driver shape against Ruby `tomllib`
  (stdlib) and `tomlrb`: full-record equality over the corpus plus a
  real-world set (a pinned bundle of Cargo.toml files, editor and
  tool configs). Runs from Ruby via the binding (item 06) — until
  then, the C driver emits typed-JSON and a small Ruby script
  compares with tomllib's trees.
- **Roundtrip property.** For every corpus input:
  `parse→emit→parse` tree equality and `emit` byte-stability.
- **Fuzz.** libFuzzer target `fuzz_parse` feeding arbitrary bytes
  (item 19-equivalent); invariants: no crash, no leak, and any
  accepted input must roundtrip. Nightly job + seed corpus from the
  inline cases.

## Acceptance gates

- Inline corpus green in ctest (offline).
- toml-test: 100% valid + invalid or explicit waiver ledger.
- Differential equality green; roundtrip property green.
- Fuzz: 1M execs clean under ASAN in CI.

## Ledger

- Corpus fetch needs network; inline corpus is the offline floor.
