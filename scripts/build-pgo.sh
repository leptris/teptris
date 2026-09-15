#!/usr/bin/env bash
# Two-stage PGO build of libteptris (profile the parse+emit hot paths):
#   stage 1: -fprofile-generate build, CLI included
#   train:  `teptris format` over every corpus file (parse + re-emit)
#   stage 2: -fprofile-use rebuild in place
# Flavors: clang (profraw -> llvm-profdata merge) and gcc (.gcda land
# beside the objects; -fprofile-use=<build-dir>). MSVC is not wired —
# callers on MSVC should keep the plain (non-PGO) build.
#
# usage: build-pgo.sh <src-dir> <build-dir> <corpus-dir> [extra cmake args...]
# leaves the profiled static archive in <build-dir>/src
set -euo pipefail
src=$1; build=$2; corpus=$3; shift 3

common=(-DCMAKE_BUILD_TYPE=Release -DTEPTRIS_BUILD_STATIC=ON
        -DTEPTRIS_BUILD_SHARED=OFF -DBUILD_TESTING=OFF
        -DTEPTRIS_BUILD_CLI=ON -DTEPTRIS_ENABLE_LTO=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON)

# CMAKE_C_COMPILER_ID is not in the cache; it lives in the compiler info file
cc_info() {
    find "$1/CMakeFiles" -name CMakeCCompiler.cmake -print -quit 2>/dev/null || true
}
cc=$(sed -n 's/^set(CMAKE_C_COMPILER_ID "\(.*\)")/\1/p' "$(cc_info "$build")" 2>/dev/null || true)
if [ -z "$cc" ]; then
    cmake -S "$src" -B "$build" "${common[@]}" -DTEPTRIS_PROFILE_TRAIN=ON "$@" >/dev/null
    cc=$(sed -n 's/^set(CMAKE_C_COMPILER_ID "\(.*\)")/\1/p' "$(cc_info "$build")" || true)
fi
case "$cc" in
    Clang|AppleClang|GNU) ;;
    *) echo "build-pgo.sh: PGO not wired for compiler '$cc' (MSVC pending); use a plain build" >&2; exit 1 ;;
esac

cmake --build "$build" -j
cli="$build/cli/teptris"
test -x "$cli"

export LLVM_PROFILE_FILE="$build/train-%p.profraw"
for f in "$corpus"/*.toml; do "$cli" format "$f" >/dev/null; done

case "$cc" in
    Clang|AppleClang)
        profdata="$build/teptris.profdata"
        if command -v xcrun >/dev/null 2>&1 && xcrun --find llvm-profdata >/dev/null 2>&1; then
            prof=(xcrun llvm-profdata)
        else
            prof=(llvm-profdata)
        fi
        "${prof[@]}" merge -output="$profdata" "$build"/train-*.profraw
        use=(-DTEPTRIS_PROFILE_TRAIN=OFF "-DTEPTRIS_PROFILE_USE=$profdata")
        ;;
    GNU)
        # .gcda sit beside the stage-1 objects; point -fprofile-use at the tree
        use=(-DTEPTRIS_PROFILE_TRAIN=OFF "-DTEPTRIS_PROFILE_USE=$build")
        ;;
esac
cmake -S "$src" -B "$build" "${common[@]}" "${use[@]}" "$@" >/dev/null
cmake --build "$build" -j
