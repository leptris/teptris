# teptris — TOML at library speed

An ultra-performance TOML 1.0 parser/writer in pure C11 — the TOML
sibling of [libleptris](https://github.com/leptris/leptris) (XML) and
[libyeptris](https://github.com/leptris/yeptris) (YAML+JSON). Zero
required runtime dependencies, stable C ABI, opaque handles.

Status: items 01–05, 07 (inline corpus) of the board are complete —
see `TODO.md` + `TODO.impl/`. Pending: toml-test corpora runner (07),
Ruby binding `teptris-ruby` (06), benchmarks (09), packaging (10).

## Build, test, validate

```sh
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release   # LTO on by default
cmake --build build
ctest --test-dir build --output-on-failure
```

`scripts/validate.sh` is the pre-completion gate: clean
warnings-as-errors build → full tests → CLI smoke → leak check.

Sanitizer presets: `build-asan`, `build-ubsan` (TODO: tsan once the
thread contract lands with the batch API).

## The verification matrix (current standing)

| check | standing |
| --- | --- |
| unit + corpus suites | 58/58 green (scalars, datetimes, table define-semantics, errors with line:col, emit goldens, roundtrip property) |
| roundtrip property | parse → emit → parse tree equality + emit byte-stability over the corpus |
| ASAN | full suite green + fuzz smoke (truncations, mutations, random bytes) clean |
| parse matrix vs C/C++ references | teptris 186–335 MB/s, fastest on every shape vs tomlc99, tomlc17, cpptoml, toml11, tomlplusplus (5.2–8.2× the best competitor) — `benchmarks/LEDGER.md` |
| bindings | teptris-ruby (tomlib-shaped, parity specs) + teptris-py (tomllib-shaped, tomli parity) — see `TODO.impl/06` |
| toml-test (BurntSushi) | pending — runner lands with `TODO.impl/07` corpora fetch |
| differential vs tomlrb / tomllib | pending (needs item 06 binding) |

## API shape

```c
teptris_document *doc = NULL;
if (teptris_parse(data, len, NULL, &doc) != TEPTRIS_OK) {
    const teptris_error *e = teptris_document_error(doc);
    /* e->line, e->column (1-based), e->message */
}
const teptris_node *v = teptris_node_table_get(teptris_document_root(doc),
                                               "key", 3);
teptris_document_free(doc);
```

Key views point into the parse input (zero-copy); keep the input buffer
alive for the document's lifetime. Emitted TOML is canonical:
`parse(emit(d)) == d` and `emit(parse(emit(d))) == emit(d)`.

## CLI

```
teptris parse|to-json <file>   # typed-JSON tree (toml-test wire shape)
teptris format <file>          # canonical TOML
teptris validate <file>        # exit 1 with line:col on error
teptris version
```
