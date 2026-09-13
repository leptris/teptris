# 06 — Ruby binding: teptris-ruby

Depends: 05. Status: v1 COMPLETE — teptris-ruby (14/14 specs incl. 7
tomlib-parity cases) and teptris-py (7/7 tests incl. tomli parity),
both FFI/ctypes-only, yeptris resolution chains. C builder API for
`dump` deferred (naive in-language writer measured fine for v1).
Language-tier numbers recorded in `benchmarks/LEDGER.md`: per-node
crossings cap the bindings at 0.6–30 MB/s vs the engine's 186–335 —
**one-bulk-drain materializer SHIPPED** (`teptris_document_flatten`,
2026-09-13): Ruby 10–45 MB/s (beats tomlib on 7/9), Python 25–63 MB/s
(3.2–6.9× tomli on every shape) — numbers in `benchmarks/LEDGER.md`.

## Goal

`teptris-ruby` — an FFI-only gem (no C extension, no compile at
install) following the leptris-ruby/yeptris-ruby pattern: thin
handles, one Ruby method = one FFI call, vendored precompiled
`libteptris` per platform, lockstep versioning with the C lib.

## Design

- Library resolution chain: `TEPTRIS_LIB_PATH` env → vendored
  `lib/platform/<tag>/` → sibling checkout `../teptris/build*/` →
  system paths.
- Surface: `Teptris::TOML.load(string, symbolize_keys: false)` /
  `Teptris::TOML.dump(obj)` with tomllib/tomlrb-compatible semantics
  (string keys by default; datetimes as `Time` when offset/date-time
  and `Date`/`Time` for local forms per the adapter contract — match
  `TomlibAdapter` behavior exactly; scalar classes: Integer, Float,
  TrueClass/FalseClass, String).
- Errors: `Teptris::ParseError` carrying line/column; the lutaml-model
  adapter maps it like `Tomlib::ParseError`.
- **C prerequisite:** a builder API (`teptris_document_new`,
  append scalar/array/table, mutate table entries) so `dump` walks
  the Ruby object once and emits from the C DOM — same shape as
  yeptris-ruby's DOM-mutation dumper. Design it in this item, gated
  on measured dump performance of a naive `emit`-string builder first.
- One-bulk-drain materialization for `load`: single crossing that
  returns primitives arrays (yeptris-ruby recorder pattern) if
  per-node accessors measure hot; measure first.

## Acceptance gates

- Spec suite ported from tomllib's and tomlrb's test corpora: parse
  parity on every case both libraries accept, error parity classes.
- `bin/smoke` against a local build; ASAN build under the suite.
- Gem version/publishing = USER release decisions.

## Files

Own repo `~/src/leptris/teptris-ruby` (leptris-ruby pattern).
