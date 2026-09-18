# 04 — PGO (two-stage build)

Status: **shipped (v0.1.13 era).** TEPTRIS_PROFILE_TRAIN/USE cmake
options + scripts/build-pgo.sh; all clang/gcc/musl release cells train
(Windows MSVC stays plain — LTCG-bound, ledgered). Measured +9.8 to
+13.2% across shapes; deep/table crossed 4x on it.

Retraining note CLOSED (2026-09-18): the binding release workflows
train a fresh profile on every build (ruby gems, py wheels), so
0.2.31+ artifacts already carry post-fusion profiles. Dev-side PGO
baselines retrain on demand via scripts/build-pgo.sh; nothing
standing.
