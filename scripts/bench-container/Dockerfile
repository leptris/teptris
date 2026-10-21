# The X2 reproducible-benchmark image: one command reproduces the
# engine-tier LEDGER numbers on any host — pinned toolchain (this
# image), pinned competitor sources (the clones below, the same pins
# the bench lane uses), deterministic corpus (scripts/gen_bench_corpus.py).
#
#   docker build -t teptris-bench -f scripts/bench-container/Dockerfile .
#   docker run --rm teptris-bench
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build python3 git ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work

COPY . /work/teptris

# Competitor pins: identical to .github/workflows/bench.yml. A pin
# change lands in BOTH places in one PR.
RUN mkdir -p /work/external \
 && git clone -q https://github.com/cktan/tomlc99.git /work/external/tomlc99 \
 && git -C /work/external/tomlc99 checkout -q 29076df \
 && git clone -q --depth 1 -b v4.2.0 https://github.com/ToruNiina/toml11.git /work/external/toml11 \
 && git clone -q --depth 1 -b v3.4.0 https://github.com/marzer/tomlplusplus.git /work/external/tomlplusplus \
 && git clone -q https://github.com/cktan/tomlc17.git /work/external/tomlc17 \
 && git -C /work/external/tomlc17 checkout -q d7e91db \
 && git clone -q --depth 1 -b v0.1.1 https://github.com/skystrife/cpptoml.git /work/external/cpptoml

CMD ["/work/teptris/scripts/bench-container/run.sh"]
