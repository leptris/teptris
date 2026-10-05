# teptris (Rust bindings)

TOML 1.1 for Rust at libteptris speed — hand-maintained FFI over the
C99 engine, no fallback. Modeled on the leptris bindings/rust layout.

```rust
use teptris::{loads, dump};

let v = loads(b"name = \"demo\"\nport = 8080\n").unwrap();
assert_eq!(v.get("name").and_then(teptris::Value::as_str), Some("demo"));

let toml = dump(&v).unwrap(); // canonical; parse(dump(v)) == v
```

## Surface

- `loads(&[u8]) -> Result<Value, ParseError>` — the whole tree as an
  owned `Value` (insertion-ordered tables, all four datetime shapes
  with real `i64`/`u32` fields).
- `dump(&Value) -> Result<String, Error>` — canonical TOML through the
  engine's builder (the emitter stays the single source of truth).
- `Document` — parse / `to_toml_string` / `to_json_string` (toml-test
  conformance dialect) / `to_value`.
- `ParseError` carries `message` + 1-based `line`/`column`.

## Zero-copy note

`teptris_parse` NUL-terminates strings in the input buffer, so the
safe `Document` owns a writable copy of the input for its lifetime
(the C buffer-lifetime contract is contained). `parse_buf(Vec<u8>)`
takes ownership without the extra copy.

## Build

The shared library is resolved by `build.rs`: set `TEPTRIS_LIB_PATH`
to a directory containing `libteptris.{dylib,so,dll}` (CI builds it
from source) or install libteptris on the linker path.

```sh
cmake -B ../../build-shared -S ../.. -DCMAKE_BUILD_TYPE=Release \
  -DTEPTRIS_BUILD_SHARED=ON -DTEPTRIS_BUILD_CLI=OFF
cmake --build ../../build-shared
TEPTRIS_LIB_PATH=$PWD/../../build-shared/src cargo test
```
