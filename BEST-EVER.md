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

Last refreshed: 2026-11-01 (engine v0.3.17 family; the measured cells
keep their original date + host — new capability cells carry theirs).

| dimension | C engine | Ruby | Python | Rust | JS/TS |
| --- | --- | --- | --- | --- | --- |
| correctness | 712/712 toml-test (1.1 view, pin ff49d109); the 1.0 view is 700/9 — the 9 accepts are TOML 1.1-legalized constructs, documented in `scripts/toml-test-run.sh` | parity specs vs tomllib-shape | tomllib-shape parity | toml-test via engine | toml-test via engine |
| differential vs references | **CLOSED** — the nightly differential lane (vs tomllib, `scripts/differential.py`) runs over the fuzz corpus; zero divergences (latest green run 2026-10-31) | rides the engine lane | rides the engine lane | rides the engine lane | rides the engine lane |
| speed: parse | **best** — 4.2–6.8× the best C/C++ competitor, every shape (367–541 MB/s, run 5) | **best** — 9/9, 19–184× tomlrb, 7–370× tomlib | **best** — 9/9, 16–26× tomllib, 1.5–7.3× rtoml | **best** — 9/9, 6.8–13.5× the toml crate; serde derive path measured 2026-10-31: borrowed plan walk 1.31× ahead of derive (1.65 vs 2.17 ms), the borrowed AoT cell 1.76 ms | @iarna beaten everywhere (1.4–3.3×); **loads: best 9/9 vs smol-toml** (1.09–3.76×, 2026-11-01, M-series host, best-of-40 × best-of-3-runs, FROM the published npm artifact — the encodeInto fast path: TextEncoder writes the input into the wasm heap in one native pass, the profiled biggest JS-side cost of the crossing; teptris-ts 0.3.23) |
| speed: emit | **best** — 3.1–57× tomlplusplus, 3.4–430× toml11, every shape (264–768 MB/s, run 5) + the streaming writer API (v0.3.5) | **best** — 9/9 vs Tomlib.dump 6–10× (tomlrb has no serializer) | **GAP** — 4/9 vs rtoml on the 3.10-floor build (0.3.7.3, borrowed keys everywhere: datetime 1.8×, strings 1.6×, cargo 1.4×, mixed 1.0× won; flat shapes up to 1.3× behind — rtoml's PyO3 value extraction, structural; native-doc dumps_doc wins the roundtrip 9/9 at 3.0-10.1×) | **best** — 9/9, 2.9–5× the toml crate; serde to_string 3.9× | **GAP** — dumps vs smol: smol keeps mega-flat shapes 1.4–1.7× (the remaining Phase 3 cell; loads is now 9/9); AoT-element nested tables dump canonically (v0.3.17 wave, byte-identical with py) |
| memory safety | **5 fuzz harnesses** (parse, parse_batch, plan, dump, edit), ASan/UBSan/TSan lanes, depth caps, stderr-silence test; the nightly found + killed a real one-byte overread in v0.3.14 (datetime HH:MM at EOF, heap-exact regression) | inherits engine | inherits engine | inherits engine | inherits engine (wasm build warning-gated) |
| ergonomics | stable C ABI, opaque handles, **format-preserving edit (v3)**: `teptris_edit_scalar` rewrites one value's bytes; comments/formatting/CRLF survive; multiline + inline targets splice parse-verified | tomlib-shaped + Descriptor + `TOML.edit` | tomllib-shaped + Descriptor + `teptris.edit` | value model + Descriptor crate + `edit_scalar` + borrowed AoT reads at full-schema parity | ES module + .d.ts + Descriptor + `edit()`; dumps byte-canonical with py |
| packaging | pkg-config/cmake, 12-lane CI | fat gems, every platform, OIDC | 60 wheels + sdist, PGO | crates.io TP | npm TP, wasm, provenance |
| truthfulness | v0.3.17 anchor | 0.3.17.0 = engine 0.3.17 | 0.3.17.0 = engine 0.3.17 | 0.3.17 = engine 0.3.17 (exact match) | 0.3.21 (compressed {major.minor}.{iteration} law) |

### The format-preserving edit dimension (2026-11-01)

The editor is an ENGINE capability, not per-language glue: one C
implementation (fuzz-harnessed, 126 unit cases) exposed byte-identically
on all four runtimes. Cross-runtime parity battery — 8 cases (plain,
section-scoped, dotted line, quoted key, multiline target, inline
target, CRLF source, AoT dotted-under-section) run against the
REGISTRY artifacts of all four runtimes: **8/8 × 4 runtimes, 2026-11-01**.
This is the inline-preservation lead: competitors reshape the whole
document on write; teptris edits one value and touches nothing else.

## The plan to close every gap

Each phase carries an acceptance gate. A phase is done when its gate
holds, not when its code merges.

### Phase 1 — kill every unmeasured cell ✅ (2026-10-19, run 5)
1. ✅ Ruby vs tomlrb: 9/9 parse, 19–184× (tomlrb has no serializer —
   dump N/A).
2. ✅ C emit vs toml11/tomlplusplus: 3.1–57× tomlplusplus, every
   shape.
3. ✅ Rust emit vs the toml crate: 9/9, 2.9–5×.
4. ✅ JS per-shape vs smol-toml + @iarna: @iarna beaten everywhere;
   smol-toml GAP cells recorded.
5. ✅ Python dumps re-confirmed: rtoml wins — GAP cell (re-measured 2026-10-22 on the 3.10-floor build: rtoml 5/9 → 4/9 teptris after borrowed keys shipped everywhere; the residual is rtoml's PyO3 value extraction, structural).

**Gate:** no cell in the ledger reads "unmeasured". Two measured-loss
cells (py dumps, JS) now carry optimization levers.

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
10. ~~serde support~~ SHIPPED (owner GO 2026-10-22; teptris-rs 0.1.6 — from_str/to_string + Value/Table/Datetime traits, the toml-crate datetime convention; the derive cell above). ~~was:~~ serde support (Serialize/Deserialize over the value model) behind
    a feature flag. **This adds a dependency — an owner decision, not
    an engineering one.** Proposed; awaiting the owner's call.

**Gate:** the rust surface is idiomatic by the ecosystem's own
conventions, or the owner records the scope decision here.

### Phase 5 — ergonomics and error quality
11. Error-message audit vs competitors: every error carries line:col
    (already enforced); review message quality against native
    references per language.
12. API review per surface against native conventions.

**Gate: MET (2026-10-23)** — [API-CONVENTIONS.md](API-CONVENTIONS.md)
documents per-surface alignment and deliberate divergences;
[ERRORS.md](ERRORS.md) covers the error half (with the duplicate-key
context improvement shipped in v0.3.8).

### Phase 6 — ledger discipline
13. Bench lanes post summaries on every release; every release PR
    refreshes the affected cells here (date + host + competitor
    version).

**Gate:** every release ships with a refreshed BEST-EVER.md.

## The native roundtrip dimension (2026-10-20)

Reserialization WITHOUT host objects: loads_doc/dumps_doc (py),
loadsDoc/dumpDoc (js) - the engine document retained behind a handle,
emitted at C speed. vs the competitors' own loads+dumps roundtrip:

| surface | teptris native | competitor dict roundtrip | result |
| --- | --- | --- | --- |
| py vs rtoml | 1.25-10.2 ms per shape (1.2 MB/s scalar_float - 10.2 MB/s array) | 5.99-35.12 ms per shape | **best 9/9, 3.0-10.1x** |
| js vs smol | 3.1-22.9 ms per shape | 6.1-56.9 ms per shape | **best 9/9, 1.4-4.7x** |
| ruby vs tomlib | 0.99-8.3 ms per shape (245-364 MB/s; scalar_int 5.0 ms) | 13.6-16895 ms per shape (tomlib collapses on flat shapes) | **best 9/9, 16.5-3751x** |

Every shape, both surfaces: the native roundtrip is the fastest
reserialization path in each ecosystem.

## New-competitor coverage (2026-10-20, X1)

| comparator | tier | teptris result |
| --- | --- | --- |
| qtoml (py, pure-python parse) | loads | **9/9, 60-145x** (0.9-2.6 MB/s) |
| tomli-w (py, write-only) | dumps | **9/9, 5-13x** (13.2-82.6 ms) |
| the toml crate's serde path (rust, derive) | serde in/out | **4.9× derive-deserialization, 3.9× serialization** (teptris 0.1.6 vs toml 0.8, 415 KB struct-shaped doc, M1 Max 2026-10-22, best-of-20 — the derive layer preserves the Value-model margin) |
| @ltd/j-toml (js, ESM TOML 1.0) | loads + dumps | **9/9 + 9/9, 1.6-5.6x loads / 1.6-20x dumps** (rejects heterogeneous arrays) |

rtoml (py) and smol-toml (js) remain the only close comparators in
their ecosystems.

## B4 - the js dict-dumps architectural record (2026-10-20)

One bounded attempt (per-value batched op writes) improved scalar_int
~15% (26.3 to 22.3 ms) but the mega-flat shapes stay 1.02-1.6x behind
smol-toml stringify. **Recorded architectural reason:** smol
stringifies a structure it parsed into its own representation; teptris
walks host objects it did not create and crosses the wasm boundary
once per dump - that walk + crossing is the floor for ANY dict-API
dump. The native-doc API (loadsDoc/dumpDoc, 9/9) is the sanctioned
fast path for the reserialize shape; the dict cell keeps the
batched-writes improvement and the record closes B4.

## E5 - error spans (2026-10-21)

teptris_error carries start_offset/end_offset (point spans, byte
offsets into the parse input; append-only, ABI-safe). Engine v0.3.7;
py TOMLDecodeError.span carries it end-to-end (E5 gate met on the py
surface); ts/ruby follow on their next releases.

## What "done" means

The mission is achieved when this table has no GAP cells, every BEST
cell has fresh evidence, and each of the four surfaces is the
default recommendation for TOML in its ecosystem on every dimension
at once. Then we keep it that way: every new competitor number is a
gap until beaten.
