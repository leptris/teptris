# 01 — Grammar-to-code specialization (CPython PEG / tree-sitter precedent)

Research: CPython's generated PEG parser, tree-sitter grammar-to-C, Pest
automata — the fastest engines compile the grammar instead of
interpreting dispatch tables.

Status: **shipped where it pays; class closed by measurement.**
- v0.1.12 specialized the dominant shapes by hand: `try_keyval_fast`
  (bare key = value), `parse_value_fast` (kind routing without the
  general value machinery), direct datetime/number routing.
- Attempt #7 (hot/cold split of try_keyval_fast) REFUTED by inspection:
  the LTO'd function has exactly 4 out-of-line calls — nothing to split.
- The 8th lever (inline line skeleton) measured -3.7% mixed / -3.4%
  array on both lane cells: adding straight-line code to the loop costs
  more than the calls it removes.

Remaining idea (unmeasured): a `try_header_fast` for the bare dotted
header path (`[a.b.c]`, no quotes/ws), fusing the key-path scan with
FNV hashing per segment the way try_keyval_fast does for single keys.
SHIPPED as 08 (v0.1.21, PR #61) — parse-positive on every shape after
a TEP_NOINLINE fix. Nothing unmeasured remains in this class.
