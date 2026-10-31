# teptris — TOML at library speed

[![ci](https://github.com/leptris/teptris/actions/workflows/ci.yml/badge.svg)](https://github.com/leptris/teptris/actions/workflows/ci.yml) [![fuzz](https://github.com/leptris/teptris/actions/workflows/fuzz.yml/badge.svg)](https://github.com/leptris/teptris/actions/workflows/fuzz.yml)

**Mission: be the best TOML parser and builder ever made — in C, in
Ruby, in Rust, and in JavaScript — measured, not claimed.** The bar,
the honest ledger, and the plan live in
[BEST-EVER.md](BEST-EVER.md); "parity" and "unmeasured" are gaps, not
wins.

An ultra-performance TOML 1.1 parser/writer in pure C99 — the TOML
sibling of [libleptris](https://github.com/leptris/leptris) (XML) and
[libyeptris](https://github.com/leptris/yeptris) (YAML+JSON). Zero
required runtime dependencies, stable C ABI, opaque handles.

Status: items 01–05, 07 (inline corpus) of the board are complete —
see `TODO.md` + `TODO.impl/`. Shipped: toml-test runner at 100%
(714/714, TOML 1.1 view), Ruby/Python bindings (teptris-ruby,
teptris-py), benchmarks, packaging.

## Build, test, validate

```sh
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release   # LTO on by default
cmake --build build
ctest --test-dir build --output-on-failure
```

`scripts/validate.sh` is the pre-completion gate: clean
warnings-as-errors build → full tests → CLI smoke → leak check.

Sanitizer presets: `build-asan`, `build-ubsan` (TODO: tsan once the
thread contract lands with the batch API).

## The TOML contract (the default, declared)

teptris implements the **TOML 1.1 draft as a strict superset of
1.0** — and that is the library's default contract, by owner decision
(2026-10-22, closing the #102 question):

- Every TOML 1.0 document parses unchanged.
- The draft's extensions are accepted by default: newlines and
  trailing commas inside inline tables, optional seconds in
  time-of-day, and the `\e` escape. (An earlier revision of this
  contract also claimed Unicode bare keys; the pinned toml-test 1.1
  grammar keeps them **invalid** — `invalid/key/special-character.toml`
  — and the parser correctly rejects them. The claim is withdrawn,
  owner-noted 2026-10-30.)
- Conformance is measured, not claimed: **712/712** on the toml-test
  1.1 view (pin `ff49d109`), and the 1.0 view is 700/9 — the nine
  accepts are exactly the 1.1-legalized constructs above, listed in
  `scripts/toml-test-run.sh`.
- Value-level correctness beyond toml-test is differential-tested
  against the reference implementation with zero divergences
  (266/266 valid cases + the 9-shape corpus, every CI push).

When the TOML specification finalizes 1.1, this contract is already
it; if the final text diverges from the draft, the change is an
engine patch-line event, measured and released like any other.

## The release law: every engine release ships every runtime

An engine release is not complete until **all four bindings publish
in the same wave**: teptris-ruby, teptris-py, teptris-rs, and
teptris-ts (owner rule, 2026-10-28). No exemptions — the "source +
link" binding ships too, carrying the engine-ref bump and whatever
honest content the wave has (a regression test, a bench tier), so
consumers can read the release as "validated against engine X.Y.Z".

Each binding follows the family versioning law (a binding version is
`{engine version}.{binding iteration}`; crates.io **matches the engine
version exactly** — teptris-rs 0.3.9 rides engine v0.3.9, owner
ruling 2026-10-30 — and npm keeps the compressed
`{engine major.minor}.{binding iteration}` form, since npm's own 0.3.9
is already published and the line cannot drop) and moves its engine
pin **in the same PR**. Every wave closes with a smoke of the
published artifact from its registry, asserting the engine version
inside and silence on stdout/stderr.

## The verification matrix (current standing)

| check | standing |
| --- | --- |
| unit + corpus suites | 124/124 green (scalars, datetimes, table define-semantics, errors with line:col + byte spans, emit goldens incl. the canonical-form reference artifact, roundtrip property) |
| roundtrip property | parse → emit → parse tree equality + emit byte-stability over the corpus |
| ASAN | full suite green + fuzz smoke (truncations, mutations, random bytes) clean |
| parse matrix vs C/C++ references | teptris 367–541 MB/s, fastest on every shape vs tomlc99, tomlc17, cpptoml, toml11, tomlplusplus (4.2–6.8× the best competitor, run 5, 2026-10-19) — `benchmarks/LEDGER.md` |
| emit matrix | 264–768 MB/s, 3.1–57× tomlplusplus / 3.4–430× toml11 (run 5) — `benchmarks/LEDGER.md` |
| bindings | teptris-ruby, teptris-py, teptris-rs, teptris-ts — the tier verdicts live in `BEST-EVER.md` |
| toml-test (BurntSushi) | **100%** — 712/712 on the 1.1 view (pin ff49d109); the 1.0 view is 700/9 — the 9 accepts are TOML 1.1-legalized constructs, documented in `scripts/toml-test-run.sh` |
| differential vs the reference implementation | **zero divergence** — `scripts/differential.py` (CLI natural-JSON vs a tomllib value-tree oracle): 266/266 toml-test valid cases + the 9-shape corpus; wired into the ci and fuzz lanes |
| reproducible benchmarks | one container command reproduces the engine-tier numbers on any host — `scripts/bench-container/`, and a report-only CI job runs it on every main push |

Every engine-tier number reproduces from one container command
(pinned toolchain, pinned competitors, deterministic corpus —
`benchmarks/LEDGER.md`):

```sh
docker build -t teptris-bench -f scripts/bench-container/Dockerfile . && docker run --rm teptris-bench
```

## Fuzzing

Four per-entrypoint libFuzzer harnesses under `test/fuzz/`, built with
`-fsanitize=fuzzer,address,undefined` and run nightly on a shared
generated corpus (budget split evenly; `workflow_dispatch` takes a
`minutes` override):

| target | surface |
| --- | --- |
| `fuzz_parse` | parse (input-driven `max_depth`) → full DOM walk through every typed accessor + `table_get` probes → emit → re-parse |
| `fuzz_parse_batch` | `teptris_parse_batch`: N-doc slicing, zeroing contract, per-document-failure docs, ERR_ALLOC path |
| `fuzz_plan` | plan build → walk → every result accessor drained |
| `fuzz_dump` | builder op sequences → all three emitter views → flatten |

Found so far: two NULL-source `memcpy` UBs in dom clone growth
(v0.3.3), plus the harness's own OOB reads. Crash artifacts are
uploaded by the lane on failure.

## Descriptor plan ABI

Fused schema-descriptor materialization for framework adapters
(lutaml-model et al.): compile a plan once, walk a document once,
materialize only the planned keys. C header `src/include/teptris/plan.h`;
consumer contract and Ruby recipe in [`DESCRIPTOR_ABI.md`](DESCRIPTOR_ABI.md).

## API shape

```c
teptris_document *doc = NULL;
if (teptris_parse(data, len, NULL, &doc) != TEPTRIS_OK) {
    const teptris_error *e = teptris_document_error(doc);
    /* e->line, e->column (1-based), e->message */
}
const teptris_node *v = teptris_node_table_get(teptris_document_root(doc),
                                               "key", 3);
teptris_document_free(doc);
```

Key views point into the parse input (zero-copy); keep the input buffer
alive for the document's lifetime. Emitted TOML is canonical:
`parse(emit(d)) == d` and `emit(parse(emit(d))) == emit(d)`. The
reference artifact - the emitter's fixed point over every TOML
construct - is checked in at
[test/golden/canonical.toml](test/golden/canonical.toml) (contract +
regeneration rules in [canonical.md](test/golden/canonical.md)); CI
enforces byte-stable re-emission of that file and of the whole emit
surface (E6).

## CLI

```
teptris parse|to-json <file>   # typed-JSON tree (toml-test wire shape)
teptris format <file>          # canonical TOML
teptris validate <file>        # exit 1 with line:col on error
teptris version
```
