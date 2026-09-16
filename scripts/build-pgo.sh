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

# TEPTRIS_PGO_SHARED=1 builds the shared lib instead of the static
# archive (wheel pipelines bundle the dylib/DLL chain). TEPTRIS_PGO_LTO=1
# keeps Release-default LTO on (the measured PGO+LTO config; ruby keeps
# it off for the mkmf link).
lib_kind=(-DTEPTRIS_BUILD_STATIC=ON -DTEPTRIS_BUILD_SHARED=OFF)
[ "${TEPTRIS_PGO_SHARED:-0}" = "1" ] && lib_kind=(-DTEPTRIS_BUILD_STATIC=OFF -DTEPTRIS_BUILD_SHARED=ON)
lto=-DTEPTRIS_ENABLE_LTO=OFF
[ "${TEPTRIS_PGO_LTO:-0}" = "1" ] && lto=-DTEPTRIS_ENABLE_LTO=ON
common=(-DCMAKE_BUILD_TYPE=Release "${lib_kind[@]}" -DBUILD_TESTING=OFF
        -DTEPTRIS_BUILD_CLI=ON "$lto"
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
    MSVC)
        echo "build-pgo.sh: MSVC PGO is orchestrated by the py workflow's cmd" >&2
        echo "steps (vcvars + pgomgr live there), not this bash script" >&2
        exit 1 ;;
    *) echo "build-pgo.sh: PGO not wired for compiler '$cc'; use a plain build" >&2; exit 1 ;;
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
