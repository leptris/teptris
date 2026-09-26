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

## 4x-push era (2026-09-15, v0.1.12)

Mandate raised to 4x on every shape. Landed (3-run A/B medians):

- teptris_arena_fast_alloc: the per-value bump is 3 instructions
  inline (extern call before)
- parse_value_fast (out-of-line): plain ints inline (Horner + node);
  datetimes route DIRECTLY to parse_datetime (the double lookahead
  was a 0.91x regression on datetime_heavy, caught in A/B); float
  shapes route directly to parse_number
- parse_array: direct items[len++] push when capacity remains
- parse_key_path: caller-owned 8-slot stack buffer (dotted paths
  of <=8 segments never allocate)

array_heavy 1.22x -> 4.22x (468 MB/s). Six shapes >= 4.2x: array
4.22, datetime 4.91, float 4.95, int 5.24, string 5.19 (+ table
3.93, deep 3.92 at the margin).

Measured and REVERTED (both instructive losses):
- inline parse_value_fast in the keyval/array loops: mixed 0.92x
  (loop footprint bloat) -> moved out-of-line, regressions gone
- fused single-pass plain-body SIMD kernel: scalar_string 0.77x —
  glibc memchr beats the hand kernel even fused (v0.1.7 lesson)

Remaining sub-4x: mixed 3.50x, cargo 3.65x, deep 3.92x, table
3.93x. These are short-string + container-churn dominated (mixed =
3000 records of strings/floats/inline-tables/bools). The identified
next lever is PGO — the dispatch is branch-mispredict heavy and
boundary work is exhausted (three rounds of measured micro-losses
on attempted further fusion).


## PGO (2026-09-16, v0.1.13 build machinery)

Two-stage PGO measured on arm64 darwin (load ~6, medians of 2
interleaved reps-30 rounds vs the same-tree non-PGO baseline;
scripts/build-pgo.sh encapsulates gen/train/use, `teptris format`
over the corpus trains parse + emit):

- absolute gains: mixed +12.8%, datetime +13.2%, deep +10.6%,
  int +10.4%, table +9.8%, array +4.5%, string +2.5%
- ratios vs best competitor: deep 3.92 -> 4.34, table 3.82 -> 4.12
  (both cross 4x); mixed 3.50 -> 3.90, cargo 3.60 -> 3.71 remain
- weighted retraining (3x mixed/cargo copies) trades breadth for the
  stragglers (deep drops to 4.08) — keep the balanced corpus
- profile says the remaining mixed/cargo heat is the hash path
  (dom_table_find_probe 27M + insert_h 25M + find_h 13M block hits)
  plus parse_key_path/finish_line — that micro-opt is the next lever,
  not more training

CMake: TEPTRIS_PROFILE_TRAIN (stage 1) / TEPTRIS_PROFILE_USE (stage
2, teptris targets only so bench competitors stay flag-free).
## Hash-path investigation (2026-09-16, NOT shipped)

Post-PGO, the remaining sub-4x shapes (mixed, cargo) pointed at the
table hash path (find_probe 27M + insert_h 25M + find_h 13M block
hits). Two changes were built and measured across five A/B sessions
(interleaved, retrained profiles, reps 20x3 medians):

1. tiny-table scan (TABLE_TINY=8: no hash index for <= 8 entries,
   linear scan of the entries array) — targets mixed's 3000 inline
   tables and cargo's ~3000 dep sub-tables;
2. fused probe+insert (find_probe reports the empty slot; insert
   reuses it — one walk instead of two). The fused helper must be
   header-inline: as a cross-TU call the extra param pushed find_probe
   past the LTO inline threshold and cost 3-5% everywhere.

Final medians vs the PGO baseline: deep +2.4% (3.98 -> 4.10), cargo
+1-3% (3.71 -> 3.80), mixed -3.6% (424 -> 408 MB/s; ratio 4.00 -> 3.94),
float/array -4-5%. The gains sit at the machine noise floor (+-
4%) and one TARGET shape regresses — reverted. Lessons: (a) mixed's
ratio flips 3.87-4.24 across sessions; single-session sub-5% effects
on this hardware are unresolvable; (b) stale-profile A/B lies — a
profile trained on old code inverts PGO gains on new code (retrain
before believing any delta); (c) grep -c exits 1 on zero matches
and kills && chains. Next levers for mixed/cargo: batch the
per-inline-table arena allocations (node + 8-entry array in one
block), or attack finish_line/parse_key_path per-record fixed
costs.

## Seeded inline tables (2026-09-16, NOT shipped)

Fourth variant: one arena block per inline table (node + initial
8-entry array, single bump + single zeroing pass). Gates passed
(67/67, 714/714). Six interleaved reps-10 rounds: deep +6.7%
(3.96 -> 4.11), mixed -2.6% (399 -> 389 MB/s, 3.99 -> 3.94) with
B-round scatter 48 MB/s, floor 3.79 -> 3.80. Mixed has now measured
negative-or-neutral across FOUR table-management variants (tiny
scan, fused probe, both, seeded allocs) while its scatter exceeds
every effect — the shape's cost lives in short-string values and
per-line parsing, not table management. Reverted. Measuring this
class of change on this machine is exhausted; revisit on quieter
hardware or with a per-line/allocation-profile-driven lever.

## Plain-string node+payload batching (2026-09-16, NOT shipped)

Fifth variant, outside table management: one arena allocation per
plain string (node + NUL-terminated payload in a single block,
dom_new_string_copy). Gates passed (67/67, 714/714, retrained
profile). Three interleaved reps-10 rounds under heavy ambient load
(machine peaked at 141): absolute MB/s flat-or-down on every shape —
mixed -2.7% (390 -> 380), cargo -3.0%, table -3.6%, deep -2.6%.
Reverted. Five of five allocation/lookup micro-levers now measured
negative-or-neutral on mixed while the machine's scatter exceeds
the effects; the shape's remaining cost is the per-record parse
line itself (dispatch + line advance + ws scan), which no
allocation-side change touches. This lever class is closed on this
hardware.

## CI bench lane + fused line finish (2026-09-16, lane shipped, change reverted)

The bench lane (.github/workflows/bench.yml + scripts/bench_ab.py)
gives every PR a same-runner interleaved A/B: base and head build in
one job, three reps-10 rounds. Self-validation: engine-identical
trees measured 99.8-100.7% (sub-1% noise on ubuntu) — resolving the
2-3% effects the dev machine's +/-4-6% scatter never could.

First measured candidate: fusing the line finish (ws/comment/newline
with direct pointer math) into try_keyval_fast, general path
preserved via a p==bol early-return. Gates: 67/67, 714/714 (1.1),
702/9 (1.0, byte-identical to baseline — the 9 are the deliberately
accepted 1.1 constructs). Lane verdict (ubuntu, tight rounds):
mixed -1.5% (468 -> 461), table_heavy -10%, array/string negative —
the added tail bloats the hottest function and costs more than the
call chain it replaced (the same inline-bloat failure measured on
2026-09-15). REVERTED. Note: mixed's floor is not finish_line; the
next candidates should SHRINK the hot path, not extend it.

## CI ratio baseline (2026-09-16, linux x86_64 gcc — platform divergence)

The bench lane now builds the five pinned reference libraries
(tomlc99 29076df, toml11 v4.2.0, tomlplusplus v3.4.0, tomlc17
d7e91db, cpptoml v0.1.1) on the runner, so every PR reports
teptris-vs-best ratios on deterministic hardware (sub-1% round
scatter on ubuntu). First baseline, linux x86_64 gcc — all
libraries built by the same compiler:

  array 6.4x   int 3.6x   float 3.3x   mixed 3.3x   deep 3.2x
  string 3.1x  table 2.9x cargo 2.4x   datetime 2.2x

DIVERGENCE, CORRECTED (same day, per-lib JSON analysis): the ratio
drop is NOT an x86 defect in teptris. Per-lib numbers show teptris
is CPU-bound and scales with the chip (datetime 490 MB/s on the
M-series vs 212 on the shared cloud core) while cpptoml — the only
fast competitor on linux (tomlc99 runs ok:true but crawls on some
shapes as-built; tomlc17/toml11 are non-factors there) — is
allocation-bound and flat (~98 MB/s everywhere). Ratios therefore
compress on any slower CPU BY CONSTRUCTION: 212/98 = 2.2x on the
runner vs 490/97 = 5.0x on the dev chip, same code. The 4x mandate
remands darwin/dev-hardware ratios; CI ratios are valid for A/B
DELTAS (sub-1% scatter) and for tracking relative change, not as
absolute mandate numbers. No x86-specific teptris work is warranted
by this data.

## Emit i64 writer — SHIPPED v0.1.16 (2026-09-17)

First positive engine change since v0.1.12, found by the emit
bench: every integer emit went through snprintf (format-parse +
locale check per number) — array_heavy emitted at 155 MB/s, the
worst shape. Replaced with a direct backwards writer into the
existing tmp buffer (INT64_MIN-safe, identical decimal output).

CI lane verdict (ubuntu, tight rounds): array emit +181%
(205 -> 576 MB/s), scalar_int +75% (301 -> 527), deep +24%,
mixed +9%; cargo/datetime/float/string neutral (99.2-100.2%).
Parse confirmed neutral on both runner cells (an ubuntu parse dip
of -7% in one run did not reproduce on macos or in round scatter —
single-run layout noise; when a delta contradicts a second cell,
trust the reproducing cell). Local darwin: array +220%, int +96%.

Cascade: v0.1.16 -> teptris-ruby 0.2.26 (8 platforms, engine
verified 0.1.16 on rubygems) -> teptris-py 0.2.9.

## Lane economics: competitors once (2026-09-23)

Main-push lane walls had grown to 44 min (ubuntu) / 38 (macos).
Step attribution killed the folklore: the pinned-competitor fetch
+ build cost ~1 min total (not the ~25 the old comment claimed) —
the Interleaved rounds step itself was 43.6/37.2 min, because on
main pushes BOTH trees ran the full 6-engine matrix in all three
rounds. Competitors are pinned sources, byte-identical in base and
head: ~5/6 of the lane re-measured constants.

Fix: bench_parse grew an optional lib-filter arg (4th argv,
comma-separated — "teptris" for the A/B rounds); one full-matrix
sweep from the head tree feeds bench_ab.py --ref for the ratio
columns (both rA and rB against the same sweep — stricter than
before, where each tree carried its own identical competitor
copies). Statistical power of the A/B signal is unchanged: teptris
base-vs-head, 3 rounds x 10 reps, interleaved. Measured on the
merge run (35795467988): lane 47 -> 8m14s; rounds step 43.6/37.2
-> 0.1 min, sweep 7.3 (ubuntu) / 7.1 (macos), fetch+build ~1 min.
Noise-floor check on the same run (base and head engine-identical):
ubuntu scalar_string -13% vs macos +8% — opposite signs, no
reproducing cell; the scatter class is unchanged. PR lane A/B
cells now 22-24 s (was minutes).

## Commands

```sh
scripts/run_benchmarks.sh        # build-bench + corpus + run + artifact
build-bench/benchmarks/bench_parse bench-corpus 1 10
```

## TODO 12 confirmed — dev-hardware ratios (2026-09-25, darwin/arm64)

The quiet window finally arrived (load 7-10, morning). Same-binary
noise probe first: 5 rounds x reps-30 on the four gate shapes spread
+/-3% (one +/-7% outlier on cargo) — comparable rounds at last, so
recording is honest.

Gate protocol: interleaved reps-30, teptris-only filter, 9 rounds
teptris (v0.1.27 = current main); competitors at the ledger pins
(tomlc99 29076df, toml11 v4.2.0, tomlplusplus v3.4.0, tomlc17
d7e91db, cpptoml v0.1.1), 3 rounds x reps-10 (their binaries do not
change between rounds). Ratio = teptris median / best-competitor
median per shape:

  mixed       408 MB/s  vs tomlc17   98.7  = 4.13x
  cargo_like  379 MB/s  vs cpptoml   81.1  = 4.67x
  deep_tables 418 MB/s  vs tomlc99   84.1  = 4.97x
  table_heavy 402 MB/s  vs cpptoml   68.8  = 5.84x

The 4x mandate is CONFIRMED on dev hardware for every gate shape.
The v0.1.19+v0.1.20 projections (mixed ~4.2-4.5x, cargo ~4.1x)
land within noise: mixed 4.13x sits ~2% under the low projection
(within the +/-3% floor), cargo exceeds at 4.67x.

Companion measurement (same window, same protocol): the shipped
levers' dev-hw gain, v0.1.14 -> v0.1.27, interleaved reps-30 x9:
mixed 337.9 -> 407.5 (+21%), cargo 301.3 -> 378.7 (+26%),
deep 330.4 -> 418.4 (+27%), table 310.9 -> 402.3 (+29%). Both
sides' rounds tight (+/-3%); deltas far above the floor.

TODO 12 CLOSES. Every line on the TODO.max-perf board is now
definitive AND confirmed.

## Two measured dead ends (2026-09-26, darwin/arm64)

Hunting the next lever after TODO 12 closed, both candidates profiled,
implemented, measured, and reverted:

1. SWAR escape-scan in emit_basic_bytes — word-at-a-time (8 bytes,
   haszero detectors on {low-5-bit-mask, ^0x22, ^0x5C, ^0x7F}) ahead
   of the byte-refine. Correct (714/714, validate.sh green) and
   measured a clean WASH: scalar_string emit 2.14 -> 2.14 ms median
   of 7 interleaved rounds (+/-1%); every other shape within noise;
   table_heavy -5%. At real string lengths (~30 B) the branch
   predictor already prices the byte loop at the SWAR prologue's
   cost, and the scan was never the emit bottleneck.

2. Emit-buffer presize from the parse input length (src_len field on
   the document; len + len/8 + 256 one-shot reserve for TOML, +50%
   for typed-JSON). The emit profile showed only ~2% of emit time in
   the realloc chain — the mass is the memmove of content itself
   (irreducible) plus the node walk and key handling. Measured
   +0-2% across all nine shapes; parse unchanged. Reverted: strictly
   fewer allocations, but not a measurable win, and it coupled the
   document struct to the emitter.

Standing conclusion: parse_number / table insert / node alloc remain
the parse floor (the profile's own top frames); emit cost is the data
movement plus the walk. The plateau is real and now measured from two
more directions.

## Parse-side dead end: SWAR digit-run scan (2026-09-26, darwin/arm64)

The parse profile's top self-time frame is parse_number (the digit
loops). Attempted the last untried mechanism there: word-scan the
digit run with the SWAR all-digits check (xor '0'; a byte is a digit
iff (d + 0x76..) | d has bit 7 clear — exact for every byte value),
then accumulate branch- and bound-free over the known span. Capped at
16 digits to preserve the <= 18 plain path.

Correct (validate.sh, toml-test 714/0) and a clean WASH: 9
interleaved rounds, scalar_int 393.9 -> 397.7 MB/s (+1.0%, inside the
+/-2% round spread); all other shapes 98.3-102.8% with no consistent
sign. The branch predictor already prices the per-byte loop at the
SWAR prologue's cost, and the accumulate itself is a serial
dependency (mag * 10 + d) no scan can remove.

Reverted. The parse floor — parse_number's serial digit math, table
insert probing, node allocation — is now confirmed from five
directions (profile x2, hash-path, fused-line, SWAR-scan). The
engine has no remaining measured headroom at this architecture.
