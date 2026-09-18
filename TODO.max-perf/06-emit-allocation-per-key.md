# 06 — Emit: kill the malloc per emitted key and per section header

Status: **shipped (v0.1.21, PR #59).** Stack buffers (256B keys,
512B section headers, one buffer for the whole header) with malloc
fallback for giant/quoted keys; key_to_buf byte-identical. Lane
verdict: emit +13-33% by shape. The follow-on cost center is 17
(the single-byte chain).

`emit_key` mallocs `6*len+4` bytes per emitted key, then usually
memcpy's a bare key of a dozen bytes; the section-header path mallocs
TWO buffers (`ks` + `hdr`) per `[table]` emitted. A 12000-key document
pays 24000+ allocator round trips on emit.

Design: stack buffers (256B) for the common sizes; malloc fallback for
giant/quoted keys only. The section path assembles into one stack
buffer. Zero semantic change — key_to_buf is byte-identical.

Verdict via the lane's emit A/B (bench_emit exists and self-validates
sub-1%).
