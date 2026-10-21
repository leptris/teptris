#!/bin/bash
# E3: every public C entrypoint must be exercised by at least one
# fuzz harness AND one unit-test file. A new TEPTRIS_API without both
# fails this check (the charter: every new entrypoint ships a fuzz
# harness + unit tests).
set -euo pipefail
cd "$(dirname "$0")/.."

# function names only: the token immediately followed by '(' (types
# like teptris_error/teptris_kind appear as return types and would be
# phantoms)
apis=$(grep -hE '^TEPTRIS_API' src/include/teptris/*.h | grep -oE 'teptris_[a-zA-Z_]+\(' | tr -d '(' | sort -u)

missing=0
for fn in $apis; do
    in_fuzz=$(grep -rl "$fn" test/fuzz/ 2>/dev/null | wc -l | tr -d ' ' || true)
    in_unit=$(grep -rl "$fn" test/unit/ 2>/dev/null | wc -l | tr -d ' ' || true)
    if [ "$in_fuzz" = "0" ] || [ "$in_unit" = "0" ]; then
        echo "MISSING: $fn (fuzz files: $in_fuzz, unit files: $in_unit)"
        missing=1
    fi
done
if [ "$missing" = "0" ]; then
    echo "entrypoint coverage: every TEPTRIS_API symbol has a fuzz harness + unit coverage"
fi
exit "$missing"
