# 15 — Tape DOM representation (simdjson tape)

Research: simdjson's tape (linear array of 64-bit elements) gives
cache-perfect traversal instead of pointer-chasing node graphs.

Status: **considered, declined.** The DOM here already arena-allocates
nodes contiguously (creation order approximates a tape); the emit and
walk paths are already linear-ish over entries. A true tape would
rewrite every consumer (bindings walk node kinds directly — the ruby/py
extensions, the flatten API, the plan ABI) for an uncertain win on
shapes whose residual cost is the parse line, not traversal. Revisit
only if a traversal-bound consumer profile appears (none has).
