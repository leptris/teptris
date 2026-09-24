# 12 — Dev-hardware ratio confirmation (measurement task)

The 4x mandate lives on dev hardware (darwin/arm64). v0.1.19 + v0.1.20
projections from lane deltas: mixed ~4.2-4.5x, cargo ~4.1x. The
confirming serialbench/bench_parse run on THIS machine has been
blocked all cycle by load 13-22 (188 login sessions, background
ruby/kimi processes).

STILL BLOCKED (2026-09-18, evening): load 48-116 all day (peaked
116). A 3-round noise probe of the SAME binary on mixed could not
even produce comparable rounds under that load — recording medians
in this state would falsify the ledger, so the gate stays honest.
Do when quiet (load < ~4): interleaved reps-30 teptris-only vs the
v0.1.14-era binaries, mixed + cargo + deep + table, medians; record
in benchmarks/LEDGER.md next to the lane numbers. Until then the
lane's ubuntu cell (sub-1% scatter) is the trusted referee — both
cells agreed on the shipped levers' target-shape gains.


CONFIRMED (2026-09-25, morning quiet window, load 7-10): same-binary
noise probe +/-3% on all four gate shapes; interleaved reps-30 x9
teptris vs competitors at the ledger pins (3 rounds x reps-10).
Dev-hw ratios: mixed 4.13x (tomlc17), cargo 4.67x (cpptoml),
deep 4.97x (tomlc99), table 5.84x (cpptoml) — the 4x mandate holds
on every gate shape; the v0.1.19/v0.1.20 projections land within
noise. Companion: v0.1.14 -> v0.1.27 lever gains +21-29% on the
same shapes. Full protocol and numbers in benchmarks/LEDGER.md
("TODO 12 confirmed"). CLOSED.
