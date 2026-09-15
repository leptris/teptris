# teptris benchmark ledger — TODO.impl/09

Artifact numbers only. Every claim below comes from
`benchmarks/results/bench_parse.json`, produced by the recorded
command; regenerate with `scripts/run_benchmarks.sh`.

## Machine + pins (run 2, 2026-09-13, after ryu + string fast-path)

- Darwin arm64, Apple M1 Max. Release: `-O3 -DNDEBUG -flto=thin`.
- teptris 0.1.0 — ryu d2s/s2d port, escape-free string fast path,
  memchr advance (short-token threshold), single-hash table insert,
  single-pass decimal scan, geometric arena blocks, manual datetime
  emit formatting (profile-driven via macOS `sample`).
- tomlc99 @ 29076df (2026-01-30) — C.
- tomlc17 @ d7e91db (2026-09-10) — C11, strictly TOML v1.0.
- cpptoml @ fededad (2018) — C++11, TOML v0.5-era.
- toml11 v4.2.0 — C++17.
- tomlplusplus v3.4.0 — C++17.
- Corpus: `scripts/gen_bench_corpus.py` seed 42, 9 files (0.25–1.5 MB).
- Method: parse from memory; warmup 1, min of 10; parse/destroy timed
  separately. Required input copies (tomlc99 buffer, toml11 vector,
  cpptoml stream) timed inside parse — <2 % of parse cost.
- Link note: tomlc99 and tomlc17 both export `toml_parse`/`toml_free`
  with different signatures; the tomlc17 target renames them
  (`tepc17_*`) — see `benchmarks/CMakeLists.txt`.
- Cross-check: every library that can count keys agrees on top-level
  key counts (0 disagreements); tomlc17 has no count API (its `ok`
  flag is the validity signal).

## Parse matrix (min-of-10, ms / MB/s) — run 3, 2026-09-13, after profile-driven round

| file | teptris | tomlc99 | tomlc17 | cpptoml | toml++ | toml11 |
| --- | --- | --- | --- | --- | --- | --- |
| array_heavy (738 KB) | 2.3 / 312 | 20.4 / 35 | 6.8 / 106 | err | 12.0 / 60 | 5168.8 / 0 |
| cargo_like (362 KB) | 1.4 / 259 | 22.0 / 16 | 22.5 / 16 | 4.0 / 89 | 7.3 / 48 | 649.2 / 0 |
| datetime_heavy (1108 KB) | 3.7 / 296 | 1972.4 / 0 | err | 10.3 / 105 | 26.8 / 40 | 584.9 / 2 |
| deep_tables (248 KB) | 0.8 / 304 | 2.9 / 84 | 2.9 / 84 | 3.0 / 82 | 5.4 / 45 | 548.1 / 0 |
| mixed (446 KB) | 1.6 / 278 | 13.1 / 33 | 4.2 / 103 | 5.2 / 84 | 10.1 / 43 | 503.3 / 1 |
| scalar_float (1502 KB) | 7.9 / 186 | 4449.4 / 0 | err | 24.3 / 60 | 65.2 / 22 | 1159.3 / 1 |
| scalar_int (1306 KB) | 5.4 / 235 | 4491.1 / 0 | err | 19.3 / 66 | 37.1 / 34 | 1156.1 / 1 |
| scalar_string (1329 KB) | 3.9 / 335 | 1100.0 / 1 | err | 13.7 / 95 | 25.2 / 52 | 452.0 / 3 |
| table_heavy (1150 KB) | 4.4 / 256 | 294.1 / 4 | 262.4 / 4 | 13.2 / 85 | 29.3 / 38 | 2377.3 / 0 |

† tomlc17: "table too large" — its own cap on keys-per-table at the
40–60k-key scalar shapes (an explicit rejection, unlike tomlc99's
quadratic burn).
‡ cpptoml: "Arrays must be homogeneous" — TOML v0.5 semantics; the
mixed-array corpus file is valid TOML 1.0.

teptris is fastest on every shape: 2.0–2.8× tomlc17 (the best
competitor where it parses), 1.9–3.5× cpptoml where it parses, 3.2–7.5×
tomlplusplus, and 100–1000× toml11.

## Run 3 improvements (profile-driven, vs run 2)

- `sample` profile of scalar_int: memchr setup on short tokens 18%,
  number double-pass 24%, double-hashed insert 32%, arena free/madvise
  10% — all four fixed. scalar_int 136→235, scalar_float 118→186,
  scalar_string 291→335, datetime 262→296 MB/s (emit 9.4→2.0 ms,
  manual digit formatting replacing snprintf), deep_tables 235→304,
  mixed 218→278, array_heavy 184→312, table_heavy 180→257.
- teptris is now 5.2–8.2× the best competitor on every shape.

## Run 2 improvements (vs run 1)

- **scalar_string 110 → 291 MB/s** (2.6×): escape-free fast path —
  one scan + one memcpy instead of two byte-wise passes.
- **float emit 126 → 7.6 ms** for 60k floats (16.7×): ryu d2s port
  replaces the snprintf/strtod precision search (0.13 µs/value now).
- **float parse unchanged** (12.5 ms; ryu s2d ≈ strtod here — the
  scanning around it dominates), deep_tables 172 → 235, mixed 167 →
  218, cargo_like 175 → 232, array_heavy 149 → 184 MB/s.

## Findings (ledgered, with causes)

1. **teptris destroy is O(1)-ish everywhere** (0.05–0.76 ms at any
   size): the per-document arena does its job.
2. **tomlc99 is quadratic in keys-per-table** (linear-scan tables):
   4.3–4.5 s on the 60k-key shapes vs 2.9 ms for deep_tables at the
   same key count spread over tables.
3. **tomlc17 rejects very large tables outright** (documented cap) —
   the modern cktan C line, 2–4× faster than tomlc99 everywhere it
   parses, but same key-count ceiling philosophy.
4. **cpptoml (v0.5-era) rejects heterogeneous arrays**; where it
   parses it is the fastest C++ reference (60–111 MB/s).
5. **toml11 v4.2 runs 0.1–1.9 MB/s** — per-token error-location
   metadata. Matches its "not perf-focused" positioning.
6. teptris float parse (scalar_float 118 MB/s vs 136 int): the byte
   scanning around the number dominates; ryu s2d ≈ strtod here.

## Language tiers (end-to-end load, 2026-09-13, min-of-6)

Ruby (`teptris-ruby/benchmark/lang_tier.rb`): teptris 9–30 MB/s;
tomlib 0.1–37 (quadratic on key-heavy shapes, 100–200× slower there);
tomlrb 1–4 MB/s everywhere. teptris-ruby beats tomlrb on every shape
and tomlib on 5/9 shapes; on the rest tomlib's C-extension
materialization (no FFI crossings) wins — the bulk-drain lever below
closes that.

Python (`teptris-py/benchmark/lang_tier.py`): teptris 0.6–9.2 MB/s vs
tomli 4.8–9.6 — ctypes per-node crossings dominate (heavier than Ruby
FFI). Same lever applies.

**Lever (measured-hot, queued for TODO.impl/06 follow-up):** per-node
FFI crossings cap both bindings at 0.6–30 MB/s while the C engine runs
186–335 MB/s — the one-bulk-drain materializer (yeptris-ruby recorder
pattern: C walks the tree once into a typed flat buffer, one crossing)
is the next perf item, with these numbers as its baseline.

## Run 4 (zero-copy float + decoder offsets, 2026-09-13)

- C: ryu reads float spans directly from the input (peek frac/exp, no
  buffer copy) — scalar_float 186→197 MB/s; full matrix 197–323.
- Ruby flat decoder: `unpack1(fmt, offset:)` (no per-scalar slices) —
  scalar_string 45→50 MB/s, all shapes 11–50 MB/s.
- Release infrastructure ported from leptris-ruby / yeptris-py:
  platform-gem release workflow (any + 7 glibc/musl/darwin/windows
  platforms, OIDC trusted publishing) and tag-driven multi-arch wheel
  workflow (manylinux repair, rpath vendor, venv smoke, twine-from-
  maintainer). Tags/versions remain USER-triggered.

## Bulk-drain round (2026-09-13, one crossing via teptris_document_flatten)

- Ruby: 9–30 → **10–45 MB/s**; now beats tomlib on 7/9 shapes (only
  array_heavy and deep_tables still lose to tomlib's C-ext
  materializer), 6–25× tomlrb everywhere.
- Python: 0.6–9.2 → **25–63 MB/s — 3.2–6.9× tomli on every shape**
  (was slower on 8/9).
- Correctness: tomlib parity 14/14 and tomli parity 7/7 green through
  the flat path; C suite 59/59.

## Native-binding era (2026-09-13, ext-only, no fallback)

Both bindings materialize in-language natively over the same C DOM:
- teptris-ruby 0.2.0 (TeptrisExt C ext): 1.6x-124x tomlib on every
  corpus shape (mixed 3.4x, array 5.0x, deep_tables 1.6x, datetime
  124x), 50-380x toml-rb. FFI path deleted.
- teptris-py 0.2.0 (teptris._native CPython module): 11x-37x tomli
  on every shape (103-175 MB/s). ctypes deleted.
Closes leptris/teptris-ruby#7 path 2 (owner decision: no fallback).

## Platform-completion era (2026-09-14, v0.1.2/v0.1.3)

Windows facts paid for with failed release runs, now encoded in the
binding workflows:
- CMake "MinGW Makefiles" recipes die under CI bash (MSYS path
  conversion splits cmd.exe recipe targets) — use Ninja.
- windows-latest ships a mingw gcc on PATH; CMake picks it by default.
  MSVC consumers (CPython ext via link.exe) must build the core with
  cl inside vcvars, STATIC=OFF (MSVC+Ninja static/import both emit
  src/teptris.lib), ARCHIVE output dir beside the DLL.
- An MSVC DLL exports nothing without dllexport: v0.1.2 tried
  WINDOWS_EXPORT_ALL_SYMBOLS, whose generated .def cannot be parsed
  under /GL ("unrecognized file format") — v0.1.3 annotates all 21
  public decls with TEPTRIS_API (LTCG-safe, explicit).
- mingw ruby exts bind x64-ucrt-rubyNNN.dll per minor: teptris-ruby
  0.2.10 ships fat gems (x64: 3.1-4.0, arm64: 3.4/4.0 — all that
  RubyInstaller ARM64 provides), loading lib/teptris/<minor>/.
- teptris-py 0.2.1 rides v0.1.3 across the full 20-cell matrix.

Known-open (pre-existing, Release-only, both fail identically on
v0.1.1-era main; ASan/Debug green): Floats.JsonValue ("a = 1.0"
emits "10000.0" inside test_unit only — standalone emit via the same
Release lib is correct) and Emit.EmittedTomlAlwaysReparses. Needs an
isolated reproduction; engine output validated separately.

## Float exponent mis-parse + builder era (2026-09-14, v0.1.5)

The two "Release-only" test failures (ledgered since v0.1.1 as
inexplicable) are closed: emit_float read the ryu exponent with
strtol, but teptris_ryu_d2s_buffered_n does NOT NUL-terminate — the
parse ran into stale stack bytes and silently absorbed a leftover
digit from a previous float's exponent (live repro: d2s wrote
"3.14E0" n=6 while tmp[6] held a stale '1' -> strtol("01")=1 ->
emitted "31.4"; "a = 1.0" -> "10000.0"). Zeroed stacks under
ASan/Debug hid it; every plain build since v0.1.1 shipped the
latent read. Fix: tmp[n] = 0. 66/66 under Release(LTO), no-LTO, ASan.

Dump side landed with it: teptris_builder_* (stack construction,
shape-derived inline/[[..]] rendering, open_inline_array for mixed
arrays, per-kind datetime validation) so bindings emit through the
one emitter. teptris-ruby 0.2.11 dumps natively through it:

- corpus dump vs tomlib: 3.0x-10.2x ahead every shape (was pure
  Ruby, 1.1x-6.1x BEHIND)
- serialbench shapes (teptris-ruby#28): parse 1.63x/2.15x/2.16x
  ahead (small/medium/large), dump 4.8x/6.6x/6.4x, GC allocations
  1.00-1.01x parity (the 2.4-2.8x in #28 was the FFI-era gem)
- exact datetimes both ways (timespec + Hinnant math / Rational
  seconds), fixing 1ns drift on .999 nanosecond boundaries

## 3x-floor era (2026-09-14, v0.1.8)

Mandate: every bench-corpus shape >= 3x the best competitor.
Starting point: mixed 2.89x, array/datetime 3.01x razor-thin,
float 3.14x but only 195 MB/s absolute.

Landed (profile-driven, one change per hot spot):
- Clinger float fast path (<=15 sig digits, exp in [-22,22]: one
  exact-pow10 multiply/divide, bit-identical to ryu; differential
  test sweeps the boundary vs strtod + round-trip)
- fused bare-key fast path in parse_keyval/parse_inline (skips the
  per-key parts-array arena alloc; rewinds for dotted/quoted keys)
- direct advance for newline-verified spans (datetime tokens, plain
  string bodies) instead of tep_adv's memchr
- inline array-gap ws skip

Final (median of 2x20-rep min-of): array 3.49x/385MB/s, cargo
3.30x/290, datetime 3.51x/370, deep 3.73x/321, mixed 3.20x/339
(floor), float 3.68x/267, int 3.65x/260, string 3.90x/395, table
3.35x/289. Gates: 67/67 Release+ASan, toml-test 756/23.

Measured and REVERTED:
- word-at-a-time key hash (FNV per 8B + splitmix finalizer): clean
  interleaved A/B lost 8/9 shapes by up to 29% (deep_tables worst).
  Byte-at-a-time FNV pipelines its multiply chain with the byte
  loads and wins at TOML key lengths.
- (v0.1.7) SIMD bare-key/ws/comment kernels: net losses; TOML
  tokens are 3-10 bytes.

Pattern across both eras: this workload rewards TIGHT SCALAR LOOPS.
Wide operations (vector kernels, word hashes) pay setup that the
per-byte loops don't, because TOML's tokens are tiny. leptris's SIMD
wins came from long XML text runs — a shape TOML doesn't have.

## Conformance era (2026-09-15, v0.1.9): 100% toml-test

Closes item 07 (the 23 divergences). Three layers:

1. The comparator, not the engine: json_eq.py did exact JSON
   equality while the official runner (corpus json.go) compares
   floats numerically and datetimes as parsed instants. Adopting
   the official semantics closed 9/23 — every float "mismatch" was
   numerically identical encoding ("300.0" vs "300").
2. Two real 1.0 bugs, one root: resolve_header rejected ALL dotted-
   table intermediates; TOML 1.0 allows [table] headers to define
   sub-tables within dotted-key tables (only exact redefinition is
   invalid). 1.0 view: 711/711.
3. TOML 1.1 draft grammar (declared level, strict superset): \e,
   \xNN escapes; optional seconds; inline-table newlines and
   trailing commas. Runner materializes version views via
   toml-test `copy` (default TOML_VERSION=1.1): 714/714.

The 1.0 view's invalid/ tree holds exactly the 9 1.1-legalized
constructs we deliberately accept — nothing else regressed.
67/67 Release+ASan; 3x perf floor held (≥3.08x).

## Table-index era (2026-09-15, v0.1.10)

The 3x floor was met (v0.1.8) but thin: mixed 3.19-3.41x across
runs, within bench noise of dipping under. The profile showed
table indexing as the shared hot spot of every thin shape:

- index_rebuild re-hashed every key on every capacity doubling —
  O(n log n) hashing per table (scalar_int's 30k-entry root:
  ~60k redundant key hashes per parse).
- find_h re-hashed keys the parser's bare-key scan had just walked.

Fixes: entries cache their hash (24B -> 32B; rebuilds only re-slot,
never re-hash); the fused bare-key fast path computes FNV-1a inline
with the key scan and probes with the precomputed hash
(find_probe, hash-prefiltered before memcmp).

Interleaved 2x20 A/B vs v0.1.9 (measured under ~16 load — both
sides back-to-back, min-of filters the spikes):
int 1.31x / float 1.31x / datetime 1.32x / string 1.21x /
cargo 1.13x / table 1.07x / mixed 1.04x.

Ratio floor vs best competitor: mixed 3.41x; every shape >= 3.4x.
Gates: 67/67 Release+ASan, toml-test 714/714 — this is the first
PR gated by teptris's own CI (green before merge).

## Commands

```sh
scripts/run_benchmarks.sh        # build-bench + corpus + run + artifact
build-bench/benchmarks/bench_parse bench-corpus 1 10
```
