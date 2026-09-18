# 13 — Structural scanning via platform SIMD (simdjson core technique)

Research: simdjson (arXiv:1902.08318) classifies structural characters
at GB/s by letting vectorized library routines find them.

Status: **shipped, in two places.**
- String bodies: `teptris_try_plain_body` finds the closing quote with
  memchr (platform SIMD), then rejects escapes/controls with
  `tep_simd_str_stop`; bodies < 32 bytes keep the one-pass scalar loop
  (the v0.1.7 hand-kernel lesson: never pay vector setup on tiny
  tokens).
- UTF-8: word-at-a-time SWAR ASCII skip in `utf8_invalid_at` (PR #60)
  — 8 bytes per iteration, byte-identical error positions.
