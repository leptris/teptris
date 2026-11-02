# API conventions review (Phase 5, item 12 — 2026-10-23)

Per surface: the native convention each binding aligns to, what holds,
and the deliberate divergences. The gate (BEST-EVER Phase 5): every
surface documents its ergonomics decisions and divergences — this file
is that documentation; the error-message half lives in
[ERRORS.md](ERRORS.md).

## Python — aligned to `tomllib` (stdlib)

- Holds: `loads(data)` (str or bytes) → dict with `str` keys;
  `TOMLDecodeError` carrying line/column; `dumps` (the stdlib has no
  dumps — the name follows `json`/`tomli_w`).
- Deliberate divergences, all additive: `TOMLDecodeError.span` (byte
  offsets, beyond tomllib's line/column); `loads_batch`, `loads_lazy`,
  `loads_doc`/`dumps_doc` (the perf surfaces — no stdlib analogue);
  `Descriptor` (the schema layer — type enforcement is the host's job,
  matching tomllib's philosophy of parse-only); `dumps_json_natural`.
  No `tomllib.load(fp)` — `loads` covers both input types.

## Ruby — aligned to `Tomlib`

- Holds: `Teptris::TOML.load/dump/load_file` with Tomlib's datetime
  contract (Time with zone for offsets, local Time, Date, "HH:MM:SS"
  strings for local times).
- Deliberate divergences: the `Teptris::TOML` namespace (Tomlib mounts
  the module at the top level — a second TOML gem cannot own that
  name); additive `load_batch`/`load_lazy`/`load_doc`/`dump_doc`/
  `dump_json_natural`/Descriptor.

## Rust — aligned to the `toml` crate

- Holds (since 0.1.6): `from_str`/`to_string` with serde derive
  support — the ecosystem's dominant shape; the Datetime interchange
  convention (`$__toml_private_datetime`) matches so the crates are
  drop-in interchangeable.
- Deliberate divergences: `Value` is teptris's own model
  (insertion-ordered tables, real datetime fields) — not
  `toml::Value`; the engine requires ownership of the parse input
  (`parse_buf(Vec<u8>)`), so `from_str` copies — visible in the API,
  documented in the README; additive `loads`/`dump`/`dump_json_natural`
  and the Descriptor crate.

## JavaScript — no native TOML convention exists

- The nearest references disagree (`smol-toml`: `parse`/`stringify`,
  following `JSON`; `@iarna/toml`: `parse`/`stringify` too).
- Deliberate divergence: `loads`/`dump`/`dumpJson` — the family-wide
  verb set, so the five surfaces read the same; `init()` for the wasm
  instantiation (the one wasm-mandated difference), `loadsDoc`/
  `dumpDoc` for the native roundtrip. `parse`/`stringify` aliases were
  considered and not added: two names for one function is a cost, and
  the family verb set is the documented contract.

## C — the reference

- The stable ABI (`teptris.h`) is the native convention everything
  else aligns to: opaque handles, `line:col` + byte-span errors,
  insertion-ordered tables, the builder/writer pair. Conventions for
  C TOML do not exist to diverge from.

## The format-preserving edit — one contract, four native surfaces (2026-11-02)

The editor is an engine capability (`teptris_edit_scalar`): rewrite
exactly the value bytes of one `key = value` line; every other byte of
the source survives (comments, blank lines, formatting, CRLF endings).
Dotted paths address the key — a `[table]` prefix matches the open
section, a dotted key line may continue it; multiline strings and
inline collections splice with parse-verified spans; a repeated
`[[header]]` edits the FIRST match. The replacement is the value's
TEXT, validated by the engine by parsing `k = <replacement>`.

Per surface (the native naming, the shared contract):

- **C**: `teptris_edit_scalar(src, len, keypath, replacement, &buf,
  &out_len)` — `TEPTRIS_ERR_STATE` on an absent key, `TEPTRIS_ERR_ARG`
  on a malformed span or replacement; `*buf` malloc'd, caller frees.
- **Rust**: `teptris::edit_scalar(&str, &str, &str) ->
  Result<String, Error>` — `Error(ERR_STATE)` / `Error(ERR_ARG)`;
  interior NULs in keypath/replacement are rejected (the engine
  strlen's them).
- **JS**: `edit(source, keyPath, replacement) -> string` — throws
  `Error` (the shim carries the engine status); typed in `index.d.ts`.
- **Ruby**: `Teptris::TOML.edit(source, key_path, replacement) ->
  String` — raises `Teptris::Error`.
- **Python**: `teptris.edit(source, key_path, replacement) -> str` —
  raises `ValueError` (tomllib-shape: decode failures are ValueError
  subclasses there too).

Cross-runtime parity battery: 8 cases × 4 runtimes from the registry
artifacts, byte-exact — see BEST-EVER's edit-dimension section.
