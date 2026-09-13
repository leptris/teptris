#!/usr/bin/env bash
# teptris pre-completion gate: clean warnings-as-errors build → tests →
# CLI smoke → leak check. Mirrors libyeptris scripts/validate.sh.
set -euo pipefail
cd "$(dirname "$0")/.."

BUILD=build-validate

echo "== clean warnings-as-errors build =="
cmake -B "$BUILD" -S . -DCMAKE_BUILD_TYPE=Debug -DTEPTRIS_WARNINGS_AS_ERRORS=ON
cmake --build "$BUILD" -j

echo "== tests =="
ctest --test-dir "$BUILD" --output-on-failure

echo "== CLI smoke =="
CLI="$BUILD/cli/teptris"
printf 'title = "teptris"\n[owner]\nname = "t"\n' > /tmp/teptris_smoke.toml
"$CLI" validate /tmp/teptris_smoke.toml
"$CLI" parse /tmp/teptris_smoke.toml > /dev/null
"$CLI" format /tmp/teptris_smoke.toml > /dev/null
if echo 'x = [1,' | "$CLI" validate /dev/stdin 2>/dev/null; then
    echo "FAIL: invalid input accepted" >&2
    exit 1
fi

echo "== leak check =="
if command -v leaks >/dev/null 2>&1; then
    leaks --atExit -- "$CLI" parse /tmp/teptris_smoke.toml > /dev/null
elif command -v valgrind >/dev/null 2>&1; then
    valgrind --leak-check=full --error-exitcode=1 "$CLI" parse /tmp/teptris_smoke.toml > /dev/null
else
    echo "(no leaks/valgrind found — skipped)"
fi

echo "VALIDATE OK"
