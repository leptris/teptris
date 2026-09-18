# 02 — SIMD byte classification (simdjson / Lemire vectorized scanning)

Research: simdjson (arXiv:1902.08318) and Lemire's ARM HTML scanning
(arXiv:2503.01662) reach GB/s by classifying input bytes in vector
registers instead of per-byte branches.

Status: **measured and reverted, twice, with the engine's own kernels.**
- v0.1.7: fused single-pass plain-body SIMD kernel for string bodies —
  0.77x on scalar_string; glibc/memchr-class library routines beat the
  hand kernel even fused (short TOML tokens amortize nothing).
- The tight scalar loops on short tokens (mixed = strings of ~10 bytes)
  are the losing regime for vectorization by construction.

Remaining idea ANALYZED AND CLOSED (2026-09-18): memchr for comment
line-ends gains nothing — the comment VALIDATION loop (control
chars, lone CR) must still touch every byte, so the terminator
search was never the cost. String closing-quote scanning is already
shipped (13). No unmeasured idea remains in this file.
