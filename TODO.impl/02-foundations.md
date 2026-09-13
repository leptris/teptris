# 02 — Common foundations: port, chartype, view, errors, memory

Depends: 01. Status: v1 complete.

## Goal

The machinery every other module stands on. Port the *patterns* from
yeptris items 02–03, sized down for TOML (no SIMD scan layer in v1 —
TOML's grammar does not have a line-start/indentation table to mine;
revisit with numbers in item 09).

## Design

- `common/port.h` — `TEPTRIS_UNUSED`, `TEPTRIS_API`, static-assert
  macros, `bool` via `<stdbool.h>`, fixed-width ints.
- `common/view.h` — `teptris_view { const char *ptr; size_t len; }`.
  Invariant: every *string value* handed out is NUL-terminated
  (arena copies get the +1); views into the raw input may not be.
- `common/chartype.h` — declared-once truth tables: bare-key char
  (`[A-Za-z0-9_-]`), space/tab, decimal/hex digit, key-path punctuation.
  Single lookup tables, no scattered `is*` calls in the parser.
- `common/error.h` — `teptris_status` codes (`OK`, `ERR_ALLOC`,
  `ERR_SYNTAX`, `ERR_SEMANTIC`, `ERR_ENCODING`, `ERR_DEPTH`,
  `ERR_ARG`, `ERR_STATE`) and `teptris_error { status; line; col;
  char msg[192]; }` — line and column are 1-based, message is
  document-owned (one per document, no allocation on the error path).
- `memory/arena.{h,c}` — per-document bump arena. 64 KiB blocks,
  16-byte alignment, `arena_alloc`, `arena_try_grow` (grow the most
  recent allocation only — the table-entry vector growth pattern).
  `teptris_document_free` walks the block list and frees in one pass;
  nothing else in the library calls malloc for document data.
- `memory/pool.{h,c}` — fixed-size object pool for nodes, carved from
  the arena, so node vectors stay contiguous and `free` stays O(1)
  overall. (If measurement shows no benefit over arena bump-alloc,
  ledger it and drop the pool — do not keep dead machinery.)

## Acceptance gates

- Unit: arena alloc/grow/alignment/block-boundary; error formatting
  at line 1 col 1 and deep offsets; chartype table spot checks.
- Leak check under the validate gate.
