# 12 — Dev-hardware ratio confirmation (measurement task)

The 4x mandate lives on dev hardware (darwin/arm64). v0.1.19 + v0.1.20
projections from lane deltas: mixed ~4.2-4.5x, cargo ~4.1x. The
confirming serialbench/bench_parse run on THIS machine has been
blocked all cycle by load 13-22 (188 login sessions, background
ruby/kimi processes).

Do when quiet: interleaved reps-30 teptris-only vs the v0.1.14-era
binaries, mixed + cargo + deep + table, medians; record in
benchmarks/LEDGER.md next to the lane numbers. Until then the lane's
ubuntu cell (sub-1% scatter) is the trusted referee — both cells
agreed on the shipped levers' target-shape gains.
