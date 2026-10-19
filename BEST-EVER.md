# BEST-EVER — the mission and the plan

**Mission: be the best TOML parser and builder ever made — in C, in
Ruby, in Rust, and in JavaScript — measured, not claimed.**

teptris is one engine (pure C99) with four native surfaces. A win in
the engine is a win everywhere; a gap in any surface × dimension is a
family gap. This document is the bar, the current honest standing, and
the plan to close every remaining gap. It is refreshed at every
release.

## The bar — six dimensions

A surface can call itself best only when every dimension below is
either (a) measured best with published numbers, or (b) explicitly
N/A with a stated reason. **"At parity" is not best. "Unmeasured" is
not best.**

1. **Correctness** — TOML 1.1 superset; 100% on toml-test (1.1 and 1.0
   views); differential-tested against reference implementations.
2. **Speed** — fastest parse AND emit, on every corpus shape, vs the
   best native competitor in each ecosystem.
3. **Memory safety** — sanitizer-clean (ASan/UBSan/TSan); one fuzz
   harness per public entrypoint run nightly; no UB, no leaks, and a
   library that never writes to stdout/stderr on normal paths.
4. **Ergonomics** — idiomatic per language: `tomllib` shape in Python,
   `tomlib` shape in Ruby, idiomatic Rust value model, real TypeScript
   types; the Descriptor plan ABI for framework adapters that need
   planned-key materialization at engine speed.
5. **Packaging** — prebuilt artifacts on every registry (rubygems,
   PyPI, crates.io, npm), self-publishing pipelines, and every
   published artifact smoke-tested *from the registry*.
6. **Truthfulness** — a version number is a claim about the engine
   inside ({engine}.{iteration} law; the pin moves in the same PR as
   the bump). Every number in the ledger carries a date, a host, and a
   competitor version. Measurement limits are stated (±25% cross-host
   ref rule; best-of-≥40 for binding benches).

## The ledger — current honest standing

Last refreshed: 2026-10-19 (engine v0.3.4 family).

| dimension | C engine | Ruby | Python | Rust | JS/TS |
| --- | --- | --- | --- | --- | --- |
| correctness | 714/714 + 711/711 toml-test | parity specs vs tomllib-shape | tomllib-shape parity | toml-test via engine | toml-test via engine |
| differential vs references | **GAP** — oracle job unbuilt | **GAP** — vs tomlrb/tomllib pending | **GAP** — vs tomllib pending | **GAP** — vs toml crate pending | **GAP** — vs smol-toml pending |
| speed: parse | **best** — 5.2–8.2× the best C/C++ competitor, every shape (186–335 MB/s) | **best vs tomllib** 3–8×; **GAP** — tomlrb head-to-head unmeasured | **best vs tomllib/rtoml** 4.8–6× | **best vs toml crate** 7–45× | **best vs @iarna** 2–4×; **GAP** — smol-toml only at parity |
| speed: emit | **GAP** — no competitor emit head-to-head | engine-fed, competitor cell open | engine-fed, competitor cell open | **GAP** — emit vs toml crate unmeasured | **GAP** — emit vs smol-toml unmeasured |
| memory safety | 4 fuzz targets, ASan/UBSan/TSan lanes, depth caps, stderr-silence test | inherits engine | inherits engine | inherits engine | inherits engine (wasm build warning-gated) |
| ergonomics | stable C ABI, opaque handles | tomlib-shaped + Descriptor | tomllib-shaped + Descriptor | value model + Descriptor crate | ES module + .d.ts + Descriptor |
| packaging | pkg-config/cmake, 12-lane CI | fat gems, every platform, OIDC | 60 wheels + sdist, PGO | crates.io TP | npm TP, wasm, provenance |
| truthfulness | v0.3.4 anchor | 0.3.4.1 = engine 0.3.4 | 0.3.4.1 = engine 0.3.4 | 0.1.5 (3-part law) | 0.3.7 (3-part law) |

## The plan to close every gap

Each phase carries an acceptance gate. A phase is done when its gate
holds, not when its code merges.

### Phase 1 — kill every unmeasured cell
1. Ruby: head-to-head vs **tomlrb** (parse + dump, generated corpus,
   best-of-≥40) → ledger cell.
2. C: emit head-to-head vs **toml11** and **tomlplusplus** emit paths
   → ledger cell.
3. Rust: emit head-to-head vs the **toml** crate → ledger cell.
4. JS: per-shape emit + parse vs **smol-toml** and **@iarna/toml** →
   ledger cell.
5. Python: re-confirm dumps numbers vs tomllib/rtoml on the current
   engine → ledger cell.

**Gate:** no cell in the ledger reads "unmeasured".

### Phase 2 — differential correctness everywhere
6. Engine CI: a differential job running teptris and a reference
   (tomllib as oracle) over the full corpus + fuzz seeds, asserting
   tree/value equality.
7. Binding CI: per-language differential vs the native reference where
   one exists (ruby→toml-test goldens are already total; py→tomllib;
   js→smol-toml on the shared corpus; rs→toml crate).

**Gate:** zero divergences on corpus + seeds; differential wired into
the nightly lane.

### Phase 3 — win JavaScript outright
8. Profile the wasm boundary per shape; attack call overhead: API
   thinning, memory pooling, bulk-load paths, single-instantiation
   API.
9. Re-measure; publish per-shape numbers.

**Gate:** strictly faster than smol-toml on every bench shape, parse
and emit.

### Phase 4 — Rust ecosystem table stakes (owner decision pending)
10. serde support (Serialize/Deserialize over the value model) behind
    a feature flag. **This adds a dependency — an owner decision, not
    an engineering one.** Proposed; awaiting the owner's call.

**Gate:** the rust surface is idiomatic by the ecosystem's own
conventions, or the owner records the scope decision here.

### Phase 5 — ergonomics and error quality
11. Error-message audit vs competitors: every error carries line:col
    (already enforced); review message quality against native
    references per language.
12. API review per surface against native conventions.

**Gate:** each surface documents its ergonomics decisions and any
deliberate divergences from native shapes.

### Phase 6 — ledger discipline
13. Bench lanes post summaries on every release; every release PR
    refreshes the affected cells here (date + host + competitor
    version).

**Gate:** every release ships with a refreshed BEST-EVER.md.

## What "done" means

The mission is achieved when this table has no GAP cells, every BEST
cell has fresh evidence, and each of the four surfaces is the
default recommendation for TOML in its ecosystem on every dimension
at once. Then we keep it that way: every new competitor number is a
gap until beaten.
