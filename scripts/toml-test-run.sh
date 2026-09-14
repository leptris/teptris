#!/usr/bin/env bash
# toml-test conformance runner (TODO.impl/07).
#
# teptris targets the TOML 1.1 draft grammar — a strict superset of
# 1.0. toml-test master carries tests for both versions; the `copy`
# command materializes a version-filtered view, and we run the view
# matching our grammar level (TOML_VERSION, default 1.1). The 1.0
# view's valid/ tests all pass too; its invalid/ tree contains the
# 1.1-legalized constructs (inline-table newlines, trailing commas,
# optional seconds, \e/\xNN escapes) we deliberately accept.
set -uo pipefail
cd "$(dirname "$0")/.."
CORPUS=${1:-corpus/toml-test}
VERSION=${TOML_VERSION:-1.1}
VIEW=/tmp/teptris-toml-test-$VERSION
PIN=v2.2.0
if [ ! -d "$CORPUS/tests/valid" ]; then
    mkdir -p "$(dirname "$CORPUS")"
    git clone -q https://github.com/toml-lang/toml-test.git "$CORPUS" 2>/dev/null
    git -C "$CORPUS" checkout -q "$PIN"
fi
CLI=build/cli/teptris
if ! command -v go >/dev/null 2>&1; then
    echo "go toolchain required to materialize the version view" >&2
    exit 2
fi
(cd "$CORPUS" && go build -o /tmp/teptris-toml-test-bin ./cmd/toml-test)
rm -rf "$VIEW"
/tmp/teptris-toml-test-bin copy -toml="$VERSION" "$VIEW"

pass=0; fail=0
while IFS= read -r f; do
    if $CLI parse "$f" 2>/dev/null > /tmp/tt.json && python3 scripts/json_eq.py /tmp/tt.json "${f%.toml}.json"; then
        pass=$((pass+1)); else fail=$((fail+1)); echo "FAIL valid: $f"; fi
done < <(find "$VIEW/valid" -name "*.toml" | sort)
while IFS= read -r f; do
    if $CLI validate "$f" >/dev/null 2>&1; then
        fail=$((fail+1)); echo "FAIL invalid (accepted): $f"; else pass=$((pass+1)); fi
done < <(find "$VIEW/invalid" -name "*.toml" | sort)
echo "toml-test @$PIN (view $VERSION): $pass pass, $fail fail"
[ "$fail" -eq 0 ]
