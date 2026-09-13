# 05 — Emitter: deterministic TOML + typed-JSON dump

Depends: 04. Status: v1 complete.

## Goal

`parse → emit → parse` yields the same tree, and
`emit(parse(emit(d))) == emit(d)` byte-stable — the leptris
canonical guarantee, adapted to TOML's formatting constraints.

## Design

**Ordering.** Insertion order everywhere (roundtrip-stable). Root
scalar/array keys first, then sub-tables. Per table: its key/values,
then its child tables — this satisfies TOML's "keys precede headers"
rule at every level. Array-of-tables emit `[[path]]` per member.

**Keys.** Bare when `[A-Za-z0-9_-]+` non-empty; else basic-quoted
with minimal escapes (literal-quoted when the key contains chars that
escape poorly but no `'` and no control chars).

**Strings.** Literal `'…'` when it contains no `'`, no control chars,
no newline; otherwise basic with minimal escapes (`\"`, `\\`, `\b`,
`\t`, `\n`, `\f`, `\r`, `\uXXXX` for remaining controls). Multi-line
only when the parser recorded a multi-line style — v1 emits
single-line escaped (roundtrip-safe, simpler; ledger: style
preservation as a follow-up).

**Numbers.** Integers normalized to decimal (hex/oct/bin input forms
emit as decimal). Floats: shortest-roundtrip via precision search
(`%.{1..17}g` + `strtod` verify), forced float marker (`.0` or
exponent) so a float never emits as an integer; `inf`/`-inf`/`nan`.

**Datetimes.** Canonical ISO from components: date `YYYY-MM-DD`,
time `HH:MM:SS`, frac printed when non-zero with trailing zeros
trimmed (1–9 digits), offset `Z` when 0 else `±HH:MM`.

**Typed-JSON** (`teptris_emit_json`) — the toml-test wire shape for
conformance diffing: scalars as `{"type": "integer", "value": "1"}`
(datetime kinds: `datetime`, `datetime-local`, `date`, `time`),
tables as objects, arrays as lists. Keys escaped per JSON; insertion
order (harness compares semantically).

Output is one arena-grown buffer owned by the call (`free` with
`free()`, or document arena — documented per function `Memory:`
comment).

## Acceptance gates

- Emit goldens (unit).
- Roundtrip property over the whole corpus: parse→emit→parse→json
  equal to parse→json, and emit byte-stability on re-emit.
- Every emitted document re-parses with zero errors (property test).
