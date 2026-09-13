# 04 — DOM: compact nodes, ordered tables, O(1) lookup

Depends: 02. Status: v1 complete.

## Goal

The value tree the parser builds, the emitter consumes, and the FFI
binding reads — insertion-ordered, hash-indexed, arena-owned.

## Design

- `teptris_node` — kind tag + payload union. Kinds: `STRING`,
  `INTEGER` (int64), `FLOAT` (double), `BOOLEAN`, `DATETIME_OFFSET`,
  `DATETIME_LOCAL`, `DATE_LOCAL`, `TIME_LOCAL`, `ARRAY`, `TABLE`.
  Scalar payload is inline (view for strings; `teptris_datetime`
  components for the four datetime kinds). Target ≤ 56 B; static
  assert in the header.
- Tables: entry vector `{ teptris_view key; teptris_node *value; }`
  in insertion order (arena, grown via `arena_try_grow`), plus an
  open-addressing index (power-of-two capacity, FNV-1a on key bytes,
  linear probe, grow at 0.7 load) for `teptris_node_table_get` O(1).
  Duplicate detection rides the same probe.
- Arrays: node-pointer vector, same growth pattern.
- Flags from item 03 (`EXPLICIT/IMPLICIT/DOTTED/INLINE`) live in the
  table payload — parser-only state, invisible to readers.
- Public read API (`teptris.h`): `teptris_document_root`,
  `teptris_node_kind`, `_string/_integer/_float/_boolean/_datetime`,
  `_array_len/_array_at`, `_table_len/_table_at/_table_get`. All
  handles `const teptris_node *`; all views NUL-terminated where the
  value is a string; keys are views into the input.
- Ownership: everything reachable from the document lives in its
  arena; `teptris_document_free` is the single destructor, no
  refcounts.

## Acceptance gates

- Static assert on node size; unit tests for order preservation,
  lookup hits/misses after growth, empty table/array shapes.
- ASAN clean under full corpus run.
