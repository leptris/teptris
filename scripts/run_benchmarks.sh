#!/usr/bin/env bash
# teptris benchmark runner (TODO.impl/09): Release+LTO build, deterministic
# corpus, full parse matrix vs reference libraries, JSON artifact.
set -euo pipefail
cd "$(dirname "$0")/.."

cmake -B build-bench -S . -DCMAKE_BUILD_TYPE=Release \
    -DTEPTRIS_BUILD_BENCHMARKS=ON -DBUILD_TESTING=OFF
cmake --build build-bench -j

python3 scripts/gen_bench_corpus.py bench-corpus

mkdir -p benchmarks/results
./build-bench/benchmarks/bench_parse bench-corpus 1 10 \
    > benchmarks/results/bench_parse.json

echo "artifact: benchmarks/results/bench_parse.json"
python3 - <<'EOF'
import json
d = json.load(open("benchmarks/results/bench_parse.json"))
for f in d["files"]:
    t = f["libs"]["teptris"]
    print(f"{f['name']:<22} {f['bytes']//1024:>5}K  "
          f"teptris {t['mb_per_s']:7.1f} MB/s")
EOF
