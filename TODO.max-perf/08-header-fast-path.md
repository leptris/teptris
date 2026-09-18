# 08 — Header fast path (`try_header_fast`)

Status: **shipped (v0.1.21, PR #61), exactly per this design.**
First cut had the single-caller static INLINE into the main loop
(the 8th-lever anti-pattern); TEP_NOINLINE restored it — then
parse-positive on every shape (cargo +3.3%, string +3.9%) at an
accepted -3-7% emit layout cost. The risk profile below was right:
adding a NEW per-header function worked; extending the loop did not.

Design: mirror try_keyval_fast — scan the bracketed bare path inline
with FNV fused per segment into a small stack array, expect `]`
immediately, then resolve via the v0.1.20 fused insert path. Rewind
and fall back to parse_header on quotes/ws/`[[`.

Risk profile: the 8th-lever lesson says do NOT extend existing hot
functions; this ADDS a new one called once per header. try_keyval_fast
proves the pattern; the skeleton attempt proves the anti-pattern.
