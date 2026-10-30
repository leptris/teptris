# CLAUDE.md — teptris (the C engine)

## Mission

Be the best TOML parser and builder ever made — in C, Ruby, Rust, and
JS — measured, not claimed. The bar, the honest ledger, and the
gap-closing plan live in [BEST-EVER.md](BEST-EVER.md). Read it before
proposing work; a dimension that is "at parity" or "unmeasured" is an
open gap, not a win.

## Operating laws (this family)

- **Family versioning law**: a binding version is
  `{engine version}.{binding iteration}`. crates.io now **matches the
  engine version exactly** (owner ruling 2026-10-30: teptris-rs 0.3.9
  rides engine v0.3.9; the 0.1.x line is retired). npm keeps the
  compressed `{engine major.minor}.{binding iteration}` form — its own
  0.3.9 is already published, so the line cannot drop to match.
  A binding version bump moves its engine pin **in the same PR**. The
  engine version is the family anchor; engine changes are patch bumps
  ("CLI is tooling").
- **All-runtimes law** (owner, 2026-10-28): every engine release
  ships **every binding in the same wave** — ruby, py, rs, ts. No
  exemptions: the source+link binding (rs) ships too, carrying the
  engine-ref bump plus whatever honest content the wave has (a
  regression test, a bench tier), so release-plz proposes and
  consumers read the release as "validated against engine X.Y.Z".
  Each wave closes with the published-artifact smokes.
- **Every release is gated by a smoke of the published artifact** from
  the registry (npm/PyPI/rubygems/crates), asserting the engine
  version inside and silence on stdout/stderr.
- **Never write to stdout/stderr from library code.** There is a
  regression test; keep it meaningful. Debug prints in C are invisible
  to unit tests and fuzz — only the published-artifact smoke or an
  explicit silence test catches them.
- **Bytes-vs-count discipline**: byte-sized malloc/memset for
  count-indexed arrays is this family's recurring overflow class
  (three strikes). Always `sizeof`.
- All changes go through PRs (rebase-merge); the fuzz lane and unit
  suite gate the merge; tags are releases.
- Benchmarks: measure before claiming; numbers carry a date, host, and
  competitor version; ±25% cross-host reference limits; best-of-≥40
  for binding benches.

## Layout

- `src/teptris/` — the engine (parser, builder, dom, plan, emitters)
- `src/include/teptris/` — the stable C ABI (`teptris.h`, `plan.h`)
- `test/unit/`, `test/toml-test/`, `test/fuzz/` — the three gates
- `scripts/gen_bench_corpus.py` — the shared corpus generator
- Bindings: `teptris-ruby`, `teptris-py`, `teptris-rs`, `teptris-ts`
  (separate repos; the pin points back here by tag)
