# 05 — Emit-side integer writer

Status: **shipped (v0.1.16).** snprintf replaced with a direct
backwards decimal writer (INT64_MIN-safe). CI verdict: array emit
+181%, int +75%, deep +24%, mixed +9%. The win came from MEASURING A
NEW SURFACE (bench_emit), not more parse attempts.

This file is the template for emit-side levers: see 06 (allocation
per key) — same surface, still unmeasured.
