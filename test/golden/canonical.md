# THE canonical-form reference artifact (E6)

`canonical.toml` in this directory is the emitter's output for a
reference document exercising every TOML construct - the exact shape
teptris emits, and a FIXED POINT of re-emission.

## The contract (README restates it)

    parse(emit(d)) == d          (roundtrip value equality)
    emit(parse(emit(d))) == emit(d)   (byte-stable idempotence)

## Enforcement

- `Corpus.CanonicalIdempotence` (test/unit/test_corpus.cpp): the
  byte-stable half over the whole corpus, including this artifact.
- `Emit.CanonicalReferenceArtifact` (test/unit/test_emit.cpp): parses
  `canonical.toml`, re-emits, and requires byte equality with the
  checked-in file. CI runs both.

## Regenerating

Deliberately, never as a drive-by: `teptris format
test/golden/canonical.toml` - a diff against the checked-in file means
the canonical form CHANGED, which is a spec event (CHANGELOG + a
ledger note).
