# Changelog

All notable changes to libteptris. The release workflow renames the
`[Unreleased]` section to the version being cut — write entries here
as they land on main.

## [Unreleased]

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
