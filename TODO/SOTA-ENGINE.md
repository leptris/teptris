# SOTA TODO - the C engine (terminal checklist)

Each item: gate = the observable condition that closes it. Test-only
changes do not bump the engine; new C entrypoints always carry a fuzz
harness, unit tests, and (if exposed) pyi stubs.

## E1 - TOML 1.1 finalization tracking (issue #102, OWNER decision)
When upstream finalizes 1.1: diff the grammar, re-run both views,
decide the superset surface.
Gate: the engine documents the exact 1.1 final grammar it implements;
toml-test pin refreshed past the release.

## E2 - Continuous public fuzzing (OSS-Fuzz)
Integrate the five harnesses with OSS-Fuzz (or, if the project is not
accepted, a nightly extended-budget lane publishing the corpus and any
findings as artifacts).
Gate: fuzzing runs continuously WITHOUT this repo's own runners, with
a public history link; every finding triaged to fix-or-document.

## E3 - Entry-point coverage check
CI check: every TEPTRIS_API symbol is exercised by at least one fuzz
harness or unit test (grep the API list vs harness/test symbols).
Gate: the check fails CI when a new public entrypoint ships without
coverage; currently green for the v0.3.6 surface.

## E4 - Roofline publication
Measure parse and emit as a percent of memory bandwidth on the bench
host; publish next to the LEDGER matrices.
Gate: LEDGER.md states "teptris parses at X% of the host's measured
streaming bandwidth" per shape class - the academic floor claim.

## E5 - Machine-readable diagnostics
Errors carry line/column (done). Add byte-offset spans to the public
error struct for tooling (minor version; py/ts/ruby surfaces follow).
Gate: every error carries a machine-readable span end-to-end in at
least one binding; documented in DESCRIPTOR_ABI/README.

## E6 - Canonical-form reference artifact
Publish the emitter's output as the reference "canonical TOML" form
(byte-stable, re-emission equal) alongside the grammar note.
Gate: README/DESCRIPTOR_ABI state the canonical-form contract with a
golden artifact; the property test (emit o parse = id) runs in CI.

## Done (for context - keep honest)
toml-test 100% both views; differential-vs-tomllib CI job + post-fuzz
differential over the mutated corpus; writer fuzz replay with the
reparse invariant; stderr-silence regression test; BE coverage (s390x
wheel smoke caught the LE decode); plan depth cap; dom clone UB
guards; streaming writer; native-doc lifetime semantics (the doc owns
a private input copy - values are views into it).
