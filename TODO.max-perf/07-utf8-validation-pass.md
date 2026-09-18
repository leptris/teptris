# 07 — UTF-8 validation: remove the upfront full-document pass

Status: **shipped (v0.1.21, PR #60) — design (a), the SWAR ASCII
skip.** 8 bytes per iteration in `utf8_invalid_at`, byte loop on any
non-ASCII word; byte-identical error positions (5-case differential).
Lane verdict: parse +20-35% on EVERY shape — the validation pass was
a quarter of parse. Design (b) (fuse into the parse loop) stays
unmeasured and CLOSED: (a) already reduced the pass to ~1/8 of its
byte cost; fusing would re-couple scanners for a bounded residual.

`teptris_parser_run` calls `validate_utf8` BEFORE parsing — a full
per-byte branchy scan of the document that the parse then repeats.
For an all-ASCII corpus (every bench shape) this is pure overhead:
~1 extra pass over the input on every parse.

Design options, in preference order:
a) SWAR/word-at-a-time ASCII fast path over the buffer (Lemire's
   validate-utf-8 shape): 8 bytes per iteration, fall back to the
   byte loop on any non-ASCII word. Cheapest possible pass.
b) Fuse validation into the parse loop (validate token bytes as
   scanned) — bigger change, interacts with every scanner; only if (a)
   measures short.

Gates: the invalid-UTF-8 corpus (toml-test encoding tests) pins exact
error positions — they must survive byte-identically.
