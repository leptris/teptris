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

Status: **SHIPPED (v0.1.23, PR #66, main 7ea4bba) — lane WIN on both
cells.** find_probe deleted with the swap. Gates: 73/73 unit + ASan,
toml-test 714/0, duplicate-key differential across the 30-key
rebuild boundary identical. Lane verdict (medians, spreads checked):
ubuntu int +3.2 / float +2.6 / mixed +1.5 / cargo +1.0 (tight,
non-overlapping rounds); macos mixed +8.9 / cargo +9.8 / datetime
+15.2 / int +6.2. Emit deltas on the parse-only diff were layout
scatter. The table-management class is now closed with a WIN, not
just the infrastructure-era data point — the 2026-09-16 revert was
a tooling artifact (cross-TU call), exactly as hypothesized.
