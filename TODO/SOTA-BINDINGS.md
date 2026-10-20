# SOTA TODO - the bindings (terminal checklist)

Family law: a binding version is {engine version}.{binding iteration}
(npm/crates: 3-part); the engine pin moves in the same PR as the
bump; every release is gated by a published-artifact smoke (install
from the registry, assert engine_version, roundtrip, stderr silence).

## B1 - Ruby native-doc (the one missing surface)
TOML.load_doc(str) -> retained doc handle; TOML.dump_doc(handle) ->
str; the ext owns a private copy of the input (values are views into
it - the py/ts lesson). Specs + native_tier bench mirror.
Gate: native_tier.rb shows 9/9 vs the ruby dict roundtrip field; gem
release {engine}.{next} smoked from rubygems.

## B2 - Rust: publish the native cell + serde (serde = OWNER decision)
Document::to_toml_string IS the native path (the crate links the
engine) - add the tier cell vs toml + toml_edit roundtrip.
serde support: Serialize/Deserialize behind a feature flag - adds a
dependency; OWNER call recorded here before any build.
Gate: rust tier includes the roundtrip cell and toml_edit; serde only
after the owner records the decision in BEST-EVER.md.

## B3 - Python flat-scalar dict dumps (abi3 price - structural)
The remaining rtoml cells (scalar_int/float/array ~1.1-1.5x) sit on
the per-key PyUnicode_AsUTF8String allocation, which the cp39-abi3
symbol set REQUIRES (AsUTF8/AsUTF8AndSize are 3.10+; abi3audit
--strict rejected 0.3.6.2/0.3.6.3 - versions burned, nothing shipped).
Levers: (a) OWNER moves python_requires floor to >=3.10 -> borrow
legal; (b) accept + the native-doc API covers the reserialization
shape. No third path exists under cp39-abi3.
Gate: either the floor moves and the tier re-measures 9/9, or
BEST-EVER.md records (b) as the architectural close.

## B4 - JS dict-cell last mile (bounded attempt)
loads wins flat/datetime shapes; dumps 1.1-1.8x behind smol on
mega-flat shapes. ONE bounded attempt: pre-sized op buffers (count
before encode) + small-int decimal string cache. If the 9/9 gate does
not close, record the architectural reason: a wasm round-trip that
must materialize JS objects cannot beat a single-pass pure-JS encoder
on its own allocation turf; the native-doc path is the js answer.
Gate: 9/9 or the recorded reason in BEST-EVER.md.

## B5 - Per-binding differential CI
The engine differential (vs tomllib, incl. post-fuzz) is the semantic
gate; add per-binding asserts over a small shared corpus in py/ts/ruby
CIs (binding-specific bugs - the BE bug class - live outside the
engine).
Gate: zero divergences wired into every binding's PR lane.

## B6 - API docs for the new surfaces
loads_doc/dumps_doc (py), loadsDoc/dumpDoc (ts), B1's ruby API: site
pages + pyi/stubs current (mypy gates the py surface).
Gate: every public API documented on the site with a roundtrip
example.

## Done (context)
py/ts native-doc (9/9 both); ts TextDecoder loads + de-allocated
encoder; py writer dumps (5/9 dict; abi3 floor documented); ruby 9/9;
rust 9/9; all pins tagged; every release registry-smoked.
