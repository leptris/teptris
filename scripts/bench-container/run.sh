#!/usr/bin/env bash
# Runs inside the X2 image (see the Dockerfile): Release+LTO engine,
# every pinned competitor, deterministic corpus, both bench binaries.
set -euo pipefail

cmake -B /work/build -S /work/teptris -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DTEPTRIS_BUILD_BENCHMARKS=ON -DBUILD_TESTING=OFF \
    -DTEPTRIS_EXTERNAL_DIR=/work/external
cmake --build /work/build -j

python3 /work/teptris/scripts/gen_bench_corpus.py /work/bench-corpus

./build/benchmarks/bench_parse /work/bench-corpus 1 10
./build/benchmarks/bench_emit /work/bench-corpus 2 10
