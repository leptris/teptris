# 07 — UTF-8 validation: remove the upfront full-document pass

UNMEASURED candidate, sized and ready.

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
