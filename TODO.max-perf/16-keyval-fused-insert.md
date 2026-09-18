# 16 — Keyval-path fused find+insert (the reverted one, retried on new rails)

THE remaining unimplemented parse lever (identified 2026-09-18).

History: the 2026-09-16 fusion was reverted with cargo +1-3% but
mixed -3.6% — attributed to the fused helper crossing the LTO inline
threshold as a cross-TU call. v0.1.20 changed the rails: probe_slot
is header-inline, insert_slot checks load factor FIRST so a carried
slot is safely invalidated, and try_header_fast proved the pattern
at +7% cargo (pre-noinline).

The keyval hot path still pays the double walk: try_keyval_fast's
find_probe walks the index to the empty slot (proving no duplicate),
then insert_h's index_insert re-walks to the same slot. Swapping to
probe_slot + insert_slot is same call count, one walk fewer — and
unlike 2026-09-16, no new parameter crosses a TU boundary.

Why it might STILL lose (why this wasn't just done): mixed has
measured negative-or-neutral on five table-management variants; the
shape's tables are tiny and the double walk may already be cache-
resident. The lane decides; if it loses, the class is closed with
the infrastructure-era data point.
