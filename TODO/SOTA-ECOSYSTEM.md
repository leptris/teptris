# SOTA TODO - ecosystem, measurement, ops (terminal checklist)

## X1 - Competitor coverage completion
The tier benches must include EVERY serious TOML implementation per
language: py += tomli-w, qtoml (writers); js += @ltd/j-toml; rs +=
toml_edit (see B2). A competitor missing from a tier is an unmeasured
cell = a gap.
Gate: every tier bench lists the full field; ledger refreshed.

## X2 - Reproducible benchmarking
A container one-liner that reproduces every LEDGER/BEST-EVER number
on any host (pinned toolchains, pinned competitor versions, the
deterministic corpus).
Gate: the README states the command; a fresh run within the +-25%
cross-host band of the published numbers.

## X3 - Provenance/attestation survey
npm has provenance (signed, live). Survey and adopt: PyPI trusted
publishing attestations (twine/PyPI support), crates.io (rust publish
has no attestation - document), rubygems (gem signing - document the
decision).
Gate: each registry's verification story documented in the packaging
section of the site; adopt where supported.

## X4 - Badges + comparison page
toml-test 100% badge, fuzz-status badge, latest-tier badge; one
comparison page on the site with the current tier tables (generated
from the benches, dated).
Gate: badges live on all five READMEs; the page regenerates from CI.

## X5 - Release-page consistency
Every tag gets a GitHub Release whose body carries the changelog
section + the refreshed ledger cells (the release trains publish
artifacts; the pages should carry the numbers).
Gate: no tag without a populated Release page.

## X6 - Site pages for the native-doc + writer APIs
loads_doc/dumps_doc (py), loadsDoc/dumpDoc (ts), B1's ruby API, the
writer grammar reference: one page each with the roundtrip example
and the honest tier statement.
Gate: site regenerates; links from the binding READMEs.

## X7 - Ops (owner items tracked here so they are never lost)
- PyPI yank of teptris 0.3.1.0 - DONE (owner clicked 2026-10-22;
  registry confirms yanked: true, wrong-engine reason recorded).
- py floor >= 3.10 decision - DONE (owner 2026-10-22; shipped as
  teptris-py 0.3.7.3, the abi3 line retired).
- TOML 1.1 stance (#102) - DONE (owner 2026-10-22: the 1.1 draft
  superset IS the default contract; declared in the README's "The
  TOML contract", #102 closed with the ruling).
- serde (B2) - GO (owner 2026-10-22); in progress on teptris-rs.
Gate: this list stays; each item leaves when the owner records the
decision.
