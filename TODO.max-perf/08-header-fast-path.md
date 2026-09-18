# 08 — Header fast path (`try_header_fast`)

UNMEASURED candidate. cargo_like = 4000 `[dependencies.word-N]`
headers; each pays parse_header's call chain (skip_ws x2, adv x2-3,
parse_key_path, resolve_header) even for the tightest possible shape:
`[` bare-key `.` bare-key `]`, no ws, no quotes.

Design: mirror try_keyval_fast — scan the bracketed bare path inline
with FNV fused per segment into a small stack array, expect `]`
immediately, then resolve via the v0.1.20 fused insert path. Rewind
and fall back to parse_header on quotes/ws/`[[`.

Risk profile: the 8th-lever lesson says do NOT extend existing hot
functions; this ADDS a new one called once per header. try_keyval_fast
proves the pattern; the skeleton attempt proves the anti-pattern.
