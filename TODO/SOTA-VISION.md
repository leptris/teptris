# SOTA Vision — the purpose, the thesis, the terminal state

## The audit: what teptris is for

TOML configuration is parsed billions of times a day by a fragmented
ecosystem: roughly twenty independent implementations across the major
languages, each re-solving correctness (spec version drift, edge-case
bugs), each 10-100x off the achievable performance floor, and almost
none of them fuzzed, sanitized, or differentially tested. Every format
ecosystem with per-language rewrites looks like this.

teptris exists to collapse that fragmentation. Its thesis:

**Correctness, speed, and safety are not tradeoffs - they compound
once, in one place.** A single memory-safe, spec-exact,
performance-floor C kernel (the "parser kernel", the position Lua
holds for embedding languages), delivered natively to every host
language, propagates each fix, each optimization, and each audit to
every ecosystem simultaneously. One bug fixed once; every language
inherits it. One benchmark run; every language publishes it. One fuzz
lane; every binding's core is covered.

teptris is the TOML proof of the leptris family thesis (libleptris =
XML, libyeptris = YAML+JSON): serialization libraries should be
kernels, not per-language rewrites. If teptris reaches its terminal
state here, the pattern generalizes.

## The terminal state of each dimension

"State of the art" is not a point; it is a sustained invariant. The
competition improves, the spec evolves - best must be maintained under
adversarial time. Each dimension therefore has a CONTINUOUS mechanism,
not a finish line:

1. **Correctness** - 100% on toml-test (1.1 + 1.0 views); differential
   agreement with independent references over every input class
   (corpus, fuzz-mutated seeds, real-world files); the grammar and the
   canonical emitted form published as reference artifacts; the 1.1
   stance tracked to finalization (#102).
2. **Speed** - fastest parse AND emit on every corpus shape vs every
   serious competitor, with the roofline stated (percent of memory
   bandwidth - how close to the physical floor), reproduced by anyone
   from a container.
3. **Memory safety** - zero known UB; one fuzz harness per public
   entrypoint; continuous third-party fuzzing with public history;
   sanitizer-clean on LE and BE; a library that never writes to
   stdout/stderr on normal paths (regression-tested).
4. **Ergonomics** - native shapes per language (tomllib/tomlib/serde/
   TS types); a native-document path so reserialization never pays the
   host-object walk; machine-readable diagnostics.
5. **Packaging** - prebuilt artifacts on every registry and platform,
   self-publishing pipelines, provenance/attestation, and every
   release smoked FROM the registry.
6. **Truthfulness** - a version number is a claim about the engine
   inside; every performance claim carries date, host, competitor
   version, and a public reproduction path; the ledger
   (BEST-EVER.md) is the single source of standing.

## The definition of done

DONE is reached - and then maintained - when:

- every ledger cell is either a measured win (with fresh evidence) or
  a recorded architectural reason why the cell is structurally closed
  (e.g. the abi3 symbol-set price);
- every TODO gate below is met (a gate is MET, not checked off);
- all five registries are current and smoked;
- the continuous mechanisms (fuzzing, differential, benches) run
  without human attention.

## Standing scoreboard snapshot (2026-10-20, engine v0.3.6)

- C: parse 4.2-6.8x the best C/C++ competitor on every shape
  (367-541 MB/s); emit 3.1-57x tomlplusplus; 714/714 + 711/711
  toml-test; differential vs tomllib zero divergences.
- Ruby: 9/9 parse (19-184x tomlrb, 7-370x tomlib), 9/9 dump (6-10x).
- Rust: 9/9 loads (6.8-13.5x), 9/9 dumps (2.9-5x).
- Python: 9/9 parse (16-26x tomllib, up to 7.3x rtoml); dict dumps
  4-5/9 vs rtoml (the abi3 symbol-set price - see
  SOTA-BINDINGS.md B3); native roundtrip 9/9, 3.0-10.1x.
- JS: native roundtrip 9/9, 1.4-4.7x vs smol-toml; loads wins the
  flat-scalar and datetime shapes; dict dumps parity-to-1.4x behind.
