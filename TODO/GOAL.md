# The goal prompt

Paste-ready driver for a fresh session. Keep under 4000 chars; bump
the scoreboard line when the ledger moves.

---

Continue the teptris program (leptris TOML engine family: C engine
`teptris`; bindings `teptris-ruby`, `teptris-py`, `teptris-rs`,
`teptris-ts` = npm `teptris`).

MISSION: teptris must be the best TOML parser/builder ever made - in
C, Ruby, Rust, and JS - measured, not claimed. "Best" is a SUSTAINED
INVARIANT across six dimensions (correctness, speed, safety,
ergonomics, packaging, truthfulness). READ FIRST: BEST-EVER.md (the
honest ledger: standings + gap cells) and the TODO/ files (the
terminal checklist; every item carries an acceptance gate - a gate is
MET, never checked off). TODO.md is historical.

SCOREBOARD (verify, don't trust): C best everywhere (parse 4.2-6.8x,
emit 3.1-57x, toml-test 100% both views, differential zero-divergence,
fuzz 5 targets + post-fuzz differential). Ruby 9/9 parse+dump. Rust
9/9 loads+dumps. Python 9/9 parse; dict dumps 5/9 vs rtoml (the
cp39-abi3 symbol-set price - AsUTF8* is 3.10+; per-key AsUTF8String
alloc is the floor unless the OWNER moves python_requires >= 3.10);
native roundtrip 9/9 at 3.0-10.1x. JS native roundtrip 9/9 at
1.4-4.7x vs smol; loads wins flat/datetime shapes; dict dumps
1.1-1.8x behind.

WORK, in order (each item = branch + PR rebase-merge + release per
family law; consult the matching TODO file for its gate):
1. B1 ruby native-doc (load_doc/dump_doc over the retained DOM; the
   doc OWNS a private copy of its input - values are views into it)
   + specs + native_tier; gem release + registry smoke.
2. B2 rust: tier roundtrip cell vs toml + toml_edit.
3. B5 binding-level differential CI (py vs tomllib, ts vs smol, rs vs
   toml, over a shared corpus; zero-divergence gate).
4. X1 competitor coverage: tomli-w + qtoml, @ltd/j-toml tiers; X4
   badges; X6 site pages for native-doc/writer APIs.
5. E2 continuous fuzzing (OSS-Fuzz integration; fallback nightly
   extended lane with public corpus artifact).
6. E3 entry-point coverage CI check; E4 roofline note in LEDGER; E5
   diagnostics spans; E6 canonical-form reference.
7. B4 one bounded js dict-dumps attempt (pre-sized buffers + int
   cache); close or record the architectural reason. B3: no work
   under abi3 - it is the owner's floor decision.
8. X2 reproducible bench container; X3 provenance survey; X5 release
   pages.

LAWS (violate none): all changes via branch + PR rebase-merge - never
commit or push main directly, never force-push; family law: binding
version = {engine}.{iteration}, pins move in the release PR; every
release is gated by a PUBLISHED-ARTIFACT smoke (registry install ->
engine_version assert -> roundtrip -> zero stderr); test-only changes
never bump the engine; every new C entrypoint ships a fuzz harness +
unit tests (+ pyi stubs when exposed); measurements are best-of->=40
with date+host+competitor version, only >20% gaps are ledgered; never
`git add -A`, never AI attribution, never external filings outside
leptris orgs.

OWNER DECISIONS - flag, never take: serde (B2), python_requires floor
(B3), the 0.3.1.0 yank (X7), TOML 1.1 stance (#102), every version
number that starts a new minor/major line.

DONE = no unchecked gate in TODO/; no ledger gap cell without either
a win or a recorded architectural reason; all five registries current
and smoked; CI green everywhere; then keep it that way - every new
competitor number is a gap until beaten.
