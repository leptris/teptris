# Changelog

All notable changes to libteptris. The release workflow renames the
`[Unreleased]` section to the version being cut — write entries here
as they land on main.

## [0.3.32] - 2026-11-23

## [Unreleased]

## [0.3.32] - 2026-11-23

### Fixed
- The IDNA A-label decoder rides `noipa` under GCC 15: the refreshed
  ubuntu image's optimizer models the punycode store (provably
  bounded by the max_out gates) as a size-0 write and flags it
  `-Wstringop-overflow` from a stage the diagnostic pragma cannot
  reach; the scratch pair also hoists to one declaration per domain
  walk. The warning-class false positive filtered exactly, in the
  release lane, with the drop condition dated.

### Changed
- The release automation lands: the two-phase flow (bump + CHANGELOG
  section rename -> release PR -> tag + notes extracted from the
  section), the engine-lockstep tooling, and the version part-macros
  move with the string again.

## [0.3.31] - 2026-11-13

### Fixed
- The ryu s2d `floor_log2` gains the 32-bit MSVC path: the gate was
  `_MSC_VER` alone, so a win32 (x86) build referenced
  `_BitScanReverse64`, an intrinsic that only exists off 64-bit x86
  (LNK2001 `__BitScanReverse64` on the teptris-py cp312-win32 wheel
  leg). The `_WIN64` build keeps the 64-bit intrinsic; the x86 split
  takes the hi word then the lo word via `_BitScanReverse`, with the
  zero case returning 64 as before.

## [0.3.30] - 2026-11-12

### Added
- The schema layer ships at zero divergence: the unified JSON Schema
  suite gate reads 1789/1789 — the 172-case format-precision campaign
  closed family by family (RFC 5322/6531/3986/3987/6570/3339/4122/
  5891/5892/5893/3492 + UTS #46 with generated Unicode tables, NFC,
  punycode — hostname and idn-hostname at full IDNA conformance).
- Windows test and CLI binaries carry an 8MB stack so the walk's
  512-hop depth cap holds its frame budget under MSVC/LTO.

### Changed
- CMake project VERSION aligned to version.h (they had drifted).
