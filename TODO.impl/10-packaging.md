# 10 — Packaging, ABI policy, release

Depends: all. Status: pending.

## Design

- Install targets for the shared and static libs + public headers;
  pkg-config file; CMake package config (`teptris-config.cmake`,
  versioned). Same layout yeptris item 20 shipped.
- `ABI.md`: the stable-C-ABI contract — opaque handles, symbol list,
  soname policy (additive changes = minor, signature/layout-visible
  changes = major), the `Memory:` ownership comment convention on
  every public function.
- Version single-sourced (`scripts/bump-version.sh` touching
  `src/include/teptris/version.h` + CMake project version in
  lockstep).
- Release workflow: automated on tag; **version numbers, tags, and
  gem pushes are USER release decisions** — the workflow only builds
  and stages what the user tags. Brew tap and distro submissions are
  EXCLUDED (carried over from the yeptris/leptris user decision).
- Vendored platform binaries for the Ruby gem ride item 06.

## Acceptance gates

- `cmake --install` into a staging prefix produces a working
  pkg-config-driven compile+link of a smoke program, macOS + Linux.
- `ctest` green against the installed tree, not just the build tree.
