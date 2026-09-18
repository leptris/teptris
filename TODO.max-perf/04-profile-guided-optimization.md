# 04 — PGO (two-stage build)

Status: **shipped (v0.1.13 era).** TEPTRIS_PROFILE_TRAIN/USE cmake
options + scripts/build-pgo.sh; all clang/gcc/musl release cells train
(Windows MSVC stays plain — LTCG-bound, ledgered). Measured +9.8 to
+13.2% across shapes; deep/table crossed 4x on it.

Remaining: retrain the profile after v0.1.19/v0.1.20 changed the hot
mix (lazy position + header fusion) — the stale-profile A/B lesson
says deltas invert on new code. Cheap, mechanical, do with the next
release train.
