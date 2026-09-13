# 01 — Bootstrap: skeleton, build, CI, CLI registry

Depends: —. Status: v1 complete (build + tests + CLI + validate.sh green).

## Goal

A repo skeleton that builds and tests warning-clean on macOS/Linux with
the yeptris conventions, plus the CLI registry with a working
`version` command and stub subcommands filled in by items 03–05.

## Design

- Root `CMakeLists.txt`: C11 (`CMAKE_C_STANDARD 11`, extensions off),
  C++11 for tests, options `BUILD_TESTING`, `TEPTRIS_BUILD_CLI`,
  `TEPTRIS_BUILD_STATIC`/`_SHARED`, `TEPTRIS_BUILD_BENCHMARKS`,
  `TEPTRIS_ENABLE_ASAN`/`_TSAN`/`_UBSAN`/`_FUZZING`,
  `TEPTRIS_WARNINGS_AS_ERRORS`, LTO default ON for Release — names and
  defaults mirror `YEPTRIS_*` one-for-one.
- Layers, top depends only on the layer below:
  - `cli/` — argument parsing, output formatting.
  - `src/include/teptris/` — public ABI-stable API, opaque handles.
  - `src/teptris/` — `common/`, `memory/`, `parse/`, `dom/`, `emit/`.
- Tests: `find_package(GTest CONFIG QUIET)` → FetchContent
  googletest v1.17.0 fallback (network confirmed available), targets
  under `build/test/`, `gtest_discover_tests`.
- `scripts/validate.sh` — pre-completion gate: clean
  warnings-as-errors build → ctest → CLI smoke → leak check
  (`leaks --atExit --` on macOS, `valgrind` on Linux).
- `.clang-format` (yeptris file copied verbatim), `.gitignore`
  (build*/, compile_commands.json, corpora caches).

## Files

`CMakeLists.txt`, `src/CMakeLists.txt`, `cli/CMakeLists.txt`,
`cli/main.c`, `test/CMakeLists.txt`, `scripts/validate.sh`,
`.clang-format`, `.gitignore`, `README.md`.

## Acceptance gates

- `cmake -B build -S . && cmake --build build && ctest --test-dir
  build --output-on-failure` green.
- `-DTEPTRIS_WARNINGS_AS_ERRORS=ON` build clean.
- `./scripts/validate.sh` exits 0.

## Ledger

- CI workflow (GitHub Actions build+ctest matrix, ASAN, nightly
  libFuzzer) — port from yeptris `.github/` when the repo gets a
  remote; not blocking.
- git init / remote / first PR — USER decision.
