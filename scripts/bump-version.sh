#!/usr/bin/env bash
# Bump the engine's version files in one shot and seed the CHANGELOG
# entry. The exact version always comes from the caller (the release
# workflow's next_version input) — never inferred here.
#
#   scripts/bump-version.sh 0.3.32
#
# Files bumped:
#   src/include/teptris/version.h  (TEPTRIS_VERSION_STRING — the SSOT
#                                  plus the public part macros)
#   CMakeLists.txt                 (project VERSION — must agree)
#   CHANGELOG.md                   (the release section: renamed from
#                                  ## [Unreleased], or MERGED into an
#                                  existing ## [V] section)
set -euo pipefail
cd "$(dirname "$0")/.."

V="${1:-}"
echo "$V" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+$' || {
  echo "usage: bump-version.sh X.Y.Z (got '${V}')" >&2
  exit 1
}

HEADER=src/include/teptris/version.h
CMAKE=CMakeLists.txt
CHANGELOG=CHANGELOG.md

CURRENT=$(grep -oE 'TEPTRIS_VERSION_STRING "[0-9.]+"' "$HEADER" | grep -oE '[0-9.]+' | head -1)
[ -n "$CURRENT" ] || { echo "cannot read the current version from $HEADER" >&2; exit 1; }
[ "$V" != "$CURRENT" ] || { echo "version $V is already current" >&2; exit 1; }

TODAY=$(date +%Y-%m-%d)
COLLISION=0
if [ -f "$CHANGELOG" ]; then
  # collision-aware (the 0.3.32/0.3.33 rehearsals): when a section
  # for V already exists (content pre-added under the final name),
  # the rename must MERGE - drop only the empty Unreleased header -
  # instead of inserting a duplicate empty section the notes gate
  # would trip over. Content must live SOMEWHERE before the cut;
  # both shapes land in one populated ## [V] section.
  if grep -q "^## \\[${V}\\]" "$CHANGELOG"; then
    COLLISION=1
  else
    grep -q '^## \[Unreleased\]' "$CHANGELOG" || {
      echo "CHANGELOG.md has no '## [Unreleased]' section — add one before releasing" >&2
      exit 1
    }
  fi
else
  echo "CHANGELOG.md missing — create it with an Unreleased section first" >&2
  exit 1
fi

# ---- version mutations (all validation has passed) ----
sed -i.bak "s/TEPTRIS_VERSION_STRING \"${CURRENT}\"/TEPTRIS_VERSION_STRING \"${V}\"/" "$HEADER"
sed -i.bak "s/^    VERSION ${CURRENT}\$/    VERSION ${V}/" "$CMAKE"
rm -f "$HEADER.bak" "$CMAKE.bak"

# the part macros are public installed surface (cmake --install
# ships this header) - they move with the string
IFS=. read -r VMAJ VMIN VPAT <<< "$V"
sed -i.bak "s/^#define TEPTRIS_VERSION_MAJOR [0-9]*$/#define TEPTRIS_VERSION_MAJOR $VMAJ/" "$HEADER"
sed -i.bak "s/^#define TEPTRIS_VERSION_MINOR [0-9]*$/#define TEPTRIS_VERSION_MINOR $VMIN/" "$HEADER"
sed -i.bak "s/^#define TEPTRIS_VERSION_PATCH [0-9]*$/#define TEPTRIS_VERSION_PATCH $VPAT/" "$HEADER"
rm -f "$HEADER.bak"

NEW=$(grep -oE 'TEPTRIS_VERSION_STRING "[0-9.]+"' "$HEADER" | grep -oE '[0-9.]+' | head -1)
CMAKE_V=$(grep -oE '    VERSION [0-9.]+' "$CMAKE" | head -1 | grep -oE '[0-9.]+')
[ "$NEW" = "$V" ] || { echo "version.h bump failed: got '$NEW', wanted '$V'" >&2; exit 1; }
[ "$CMAKE_V" = "$V" ] || { echo "CMakeLists.txt VERSION ($CMAKE_V) disagrees with $V — align them first" >&2; exit 1; }
grep -q "^#define TEPTRIS_VERSION_PATCH $VPAT$" "$HEADER" || {
  echo "TEPTRIS_VERSION_PATCH bump failed" >&2
  exit 1
}

# ---- the changelog mutation ----
if [ "$COLLISION" = "1" ]; then
  awk '/^## \[Unreleased\]$/ && !done { done = 1; next } { print }' \
    "$CHANGELOG" > "$CHANGELOG.new"
  mv "$CHANGELOG.new" "$CHANGELOG"
  grep -q "^## \\[${V}\\] - " "$CHANGELOG" || {
    echo "existing ## [${V}] section carries no date line - give it one" >&2
    exit 1
  }
  echo "merged into the existing ## [${V}] section"
else
  awk -v v="$V" -v today="$TODAY" '
    /^## \[Unreleased\]$/ && !done {
      print "## [" v "] - " today
      print ""
      print "## [Unreleased]"
      done = 1
      next
    }
    { print }
  ' "$CHANGELOG" > "$CHANGELOG.new" && mv "$CHANGELOG.new" "$CHANGELOG"
  grep -q "^## \\[${V}\\] - ${TODAY}" "$CHANGELOG" || {
    echo "CHANGELOG rename failed silently — refusing to continue" >&2
    exit 1
  }
fi

echo "bumped ${CURRENT} -> ${V} (version.h, CMakeLists.txt; CHANGELOG ${COLLISION:+merged}${COLLISION:-renamed})"
