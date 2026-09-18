# 17 — Emit: single-byte chain and span-batched escaping

SIZED, code-verified (2026-09-18). The ledger's "eb_c per-byte
separator chain" candidate — the last unmeasured emit cost center
after 05 (integers) and 06 (stack buffers).

Every 1-byte emit (`eb_c`) routes through `eb_put` → `eb_reserve`
→ `memcpy(1)`: a status check + capacity arithmetic + call chain per
byte. Per TOML-emit record that chain runs for the closing quote(s),
array brackets, and the record's '\n'; per ESCAPED string byte it
runs for every clean byte too (`emit_basic_bytes` and `json_string`
switch per byte and `eb_c` the default case).

Design (two parts, one surface):
a) fast paths: `eb_c` writes directly when `len + 2 <= cap`;
   `eb_put` hoists the same check before the reserve call. The
   slow path (growth) stays where it is — shrink, don't extend.
b) span batching: scan to the NEXT byte needing escape, emit the
   clean span with one `eb_put`, handle the escape, continue.
   Byte-identical output; predicates differ per view (TOML escapes
   0x7F, JSON passes it raw).

Float re-render (ryu digits → TOML notation) was considered here and
skipped: <= 24 digits of scalar work after a shortest-round-trip
call, not a cost center; float rides at 4.1-5.0x.

Verdict via the lane's emit A/B.
