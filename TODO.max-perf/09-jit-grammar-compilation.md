# 09 — JIT grammar compilation (XGrammar-2 style)

Research: XGrammar-2 (arXiv:2601.04426) gets 6x over interpretation
via JIT + tag-dispatched structure switching + cross-grammar caching.

Status: **not applicable to teptris.** The grammar is fixed (TOML);
there is nothing to compile at runtime and no grammar cache to reuse.
The applicable core of the paper — tag dispatch — is already the
engine's dispatch shape (kind switch in parse_value_fast). Static
specialization is 01.
