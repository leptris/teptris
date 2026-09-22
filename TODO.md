# teptris — TOML at library speed

Seed plan for a C11 TOML 1.0.0 parser/writer — the TOML sibling of
libleptris (XML) and libyeptris (YAML+JSON). Same rules: zero required
runtime dependencies, stable C ABI, opaque handles, pool memory, one
executed plan per item, phase gates must pass before an item closes,
performance claims need artifact numbers, dead ends go to the ledger.

Why standalone, not inside yeptris: TOML shares neither grammar nor
flow kernel with YAML/JSON (dotted keys, `[table]`/`[[aot]]` headers,
mandatory datetimes, no anchors/merge keys). Only the DOM/materializer
*patterns* port; the parse front-end and the corpus (toml-test) are
its own. It is the last uncovered lutaml-model format — `toml.rb`
still resolves `tomllib`/`tomlrb` and carries a "skip tomlib on
Windows due to segfaults" workaround.

Released: v0.1.0 tagged 2026-09-13 (USER-approved); gem + wheel
workflows live in leptris/teptris-ruby and leptris/teptris-py.
Versions remain USER release decisions.

## Verification matrix (the gates)

| corpus | pin | standing |
| --- | --- | --- |
| unit + inline corpus (offline) | checked in | 58/58 green (77 with batch/plan suites) |
| ASAN + fuzz smoke | local | clean; nightly libFuzzer lane (parse→emit→re-parse under ASan/UBSan) |
| toml-test (BurntSushi) | item 07 fetch | 714/714 on the 1.1 view, 711/711 on 1.0 (`scripts/toml-test-run.sh`) |
| differential vs tomlrb / tomlib / stdlib tomllib | item 07 | binding parity suites green (teptris-ruby ↔ tomllib, teptris-py ↔ tomllib/tomli); lang-tier lanes re-measure every main push |
| emitter goldens | item 07 | done — byte-stable goldens + teptris↔tomlib/tomlrb five-way differential (teptris-ruby spec/toml_dump_golden_spec.rb); caught the local-time fraction drop |
| TSAN | item 02 batch work | TSAN preset in-tree, suite green (single-threaded contract); CI TSAN lane not wired |

## Items

| # | Item | Depends | Status |
| --- | --- | --- | --- |
| 01 | [Bootstrap: skeleton, build, CI, CLI registry](TODO.impl/01-bootstrap.md) | — | v1 complete |
| 02 | [Common foundations: port, chartype, view, errors, memory](TODO.impl/02-foundations.md) | 01 | v1 complete |
| 03 | [Parser: TOML 1.0 grammar + define semantics](TODO.impl/03-parser.md) | 02, 04 | v1 complete (ledgered edge cases in the item file) |
| 04 | [DOM: compact nodes, ordered tables, O(1) lookup](TODO.impl/04-dom.md) | 02 | v1 complete (node = 64 B; assert in `dom.h`) |
| 05 | [Emitter: deterministic TOML + typed-JSON dump](TODO.impl/05-emitter.md) | 04 | v1 complete |
| 06 | [Bindings: teptris-ruby + teptris-py](TODO.impl/06-ruby-binding.md) | 05 | v1 complete — tomlib/tomllib API shapes, parity suites green |
| 07 | [Conformance: toml-test, differentials, roundtrip, fuzz](TODO.impl/07-conformance.md) | 03, 05 | inline corpus + fuzz smoke done; fetched corpora + differentials pending |
| 08 | [lutaml-model integration: `:teptris` adapter](TODO.impl/08-lutaml-model.md) | 06 | teptris-side complete (Descriptor plan ABI + recipe, load_batch/load_lazy_batch; teptris-ruby#108 closed) — adapter wiring lives in lutaml-model |
| 09 | [Benchmarks: matrix, CI, ledger](TODO.impl/09-benchmarks.md) | 07 | v1 complete — six-reference matrix, three recorded runs; 186–335 MB/s, 5.2–8.2× best competitor on every shape |
| 10 | [Packaging, ABI policy, release](TODO.impl/10-packaging.md) | all | v1: repos published (leptris/teptris + bindings), release workflows live; install targets + pkg-config shipped (CMake export + teptris.pc) |

Rules inherited from yeptris: `scripts/validate.sh` is the
pre-completion gate (warnings-as-errors build → full tests → CLI
smoke → leak check). All changes go through PRs; stage explicit file
paths; no AI attribution anywhere.
