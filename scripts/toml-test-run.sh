#!/usr/bin/env bash
# toml-test conformance runner (TODO.impl/07)
set -uo pipefail
cd "$(dirname "$0")/.."
CORPUS=${1:-corpus/toml-test}
CLI=build/cli/teptris
PIN=master
if [ ! -d "$CORPUS/tests/valid" ]; then
  mkdir -p "$(dirname "$CORPUS")"
  git clone -q https://github.com/toml-lang/toml-test.git "$CORPUS" 2>/dev/null
  git -C "$CORPUS" checkout -q "$PIN"
fi
pass=0; fail=0
while IFS= read -r f; do
  if $CLI parse "$f" 2>/dev/null > /tmp/tt.json && python3 scripts/json_eq.py /tmp/tt.json "${f%.toml}.json"; then
    pass=$((pass+1)); else fail=$((fail+1)); echo "FAIL valid: $f"; fi
done < <(find "$CORPUS/tests/valid" -name "*.toml" | sort)
while IFS= read -r f; do
  if $CLI validate "$f" >/dev/null 2>&1; then
    fail=$((fail+1)); echo "FAIL invalid (accepted): $f"; else pass=$((pass+1)); fi
done < <(find "$CORPUS/tests/invalid" -name "*.toml" | sort)
echo "toml-test @$PIN: $pass pass, $fail fail"
[ "$fail" -eq 0 ]
