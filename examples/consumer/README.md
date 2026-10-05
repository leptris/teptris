# Consumer example — the smallest real teptris program

Parse a TOML document, walk the tree, emit it back, free it. One file,
no dependencies beyond the engine.

## Build and run

### pkg-config (installed engine)

```sh
cc main.c $(pkg-config --cflags --libs teptris) -o demo && ./demo
```

### CMake (this repo)

```sh
cmake -B build -S ../.. -DCMAKE_BUILD_TYPE=Release -DTEPTRIS_BUILD_CLI=ON
cmake --build build
cc -I../../src/include main.c ../../build/src/libteptris.a -o demo && ./demo
```

Expected output: the parsed keys printed by kind, then the re-emitted
canonical TOML.

## The one rule that matters

The document's strings are **zero-copy views into the input buffer**.
The buffer must stay valid and unmodified for the document's entire
lifetime (`teptris_parse` NUL-terminates names and values in place, so
it must be writable — copy read-only bytes into a malloc'd buffer
first). Free the document before freeing the buffer.
