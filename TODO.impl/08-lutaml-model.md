# 08 — lutaml-model integration: `:teptris` TOML adapter

Depends: 06. Status: pending.

## Goal

lutaml-model's `toml.rb` resolves `:teptris` as a first-class adapter,
retiring the Windows tomllib-skip workaround.

## Design

- `Lutaml::Toml::Adapter::TeptrisAdapter` implementing the
  `TomlibAdapter` contract (`load`/`parse`/`dump`/`generate` shapes
  as consumed by `Lutaml::Toml::Adapter::Document`): read the current
  adapter's method surface first, mirror it exactly.
- Registration in the `ADAPTERS` map in lutaml-model `toml.rb` +
  `detect_toml_adapter` resolution chain: explicit config → gem
  presence → fallback chain. Default becomes teptris when the gem
  loads; tomllib/tomlrb remain selectable — no requirement changes,
  no gemspec hard dependency unless the USER decides (soft
  requirement in Gemfile dev group first, mirroring how leptris
  entered).
- Error mapping: `Teptris::ParseError` → the adapter's error type the
  mapping layer expects.
- Retire `toml.rb`'s "skip tomlib on Windows" branch: teptris is a
  prebuilt shared library, no segfault surface; keep the branch until
  teptris platform coverage includes Windows artifacts.

## Acceptance gates

- lutaml-model TOML adapter spec suite green with
  `Config.toml_adapter_type = :teptris`.
- Full lutaml-model run green with tomllib still default (no
  behavior change without opt-in), then a PR flipping the default —
  USER decision on timing.
- Roundtrip: `Model.from_toml(x.to_toml) == x` over the model specs.
