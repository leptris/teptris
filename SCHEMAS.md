# Schemas — validating TOML against JSON Schema (2020-12 practical subset,
# taplo-audited)

teptris validates TOML documents against JSON Schemas. One engine
implementation (C99), exposed on every runtime. Compile once, validate
many: a compiled schema is an immutable, arena-backed handle reusable
across documents.

```c
teptris_schema *sch = NULL;
char *diag = NULL; size_t diag_len = 0;
teptris_status st = teptris_schema_compile(json, json_len, &sch,
                                           &diag, &diag_len);
teptris_diag_free(diag);            /* one malloc'd {"error":...} on failure */
if (st == TEPTRIS_OK) {
    char *errs = NULL; size_t errs_len = 0;
    teptris_schema_validate(sch, toml, toml_len, &errs, &errs_len);
    /* errs is a malloc'd JSON array of
     * {"path", "schemaPath", "message"} objects; "[]" = valid */
    free(errs);
    teptris_schema_free(sch);
}
```

The CLI rides the same engine path:
`teptris validate --schema schema.json file.toml` — exit 0 valid,
1 invalid (error list on stderr), 2 usage or unusable schema.

## The four policies (where TOML and JSON disagree)

1. **null matches no TOML value.** TOML has no null. A bare `null`
   inside `"type"` arrays means *the key may be absent*:
   `{"type": ["string", null]}` is "a string, or don't set it".
   Optionality otherwise stays the JSON Schema way: don't list the key
   in `required`. A quoted `"null"` is just an unknown type name and is
   rejected at compile.
2. **integer is TOML-native.** TOML has real int64 values, so
   `integer` accepts TOML integers only and `1.0` (a TOML float)
   never validates as `integer`; `number` accepts both, keeping
   integer identity. `enum`/`const` compare type-strictly: the
   schema's `1` does not match `1.0`. This is a **deliberate
   divergence from the 2020-12 spec** (which counts floats with a
   zero fractional part as integers — the rule taplo follows through
   its JSON round trip) and from taplo on the same schema: teptris
   rejects `port = 1.0` for `type: integer`, taplo accepts it. The
   TOML reading keeps the document's own types honest.
3. **datetimes are strings with formats.** TOML offset/local datetimes,
   dates and times type-check as `string` and satisfy `format`
   `date-time` / `date` / `time` by kind (a datetime satisfies only
   `date-time`; a date only `date`). Plain strings are shape-checked
   against the same RFC 3339 shapes. Because JSON cannot carry a
   datetime literal, `enum`/`const` entries written as canonical
   RFC 3339 strings match datetime values by text.
4. **nan and inf are floats.** They pass `type: "number"`; `nan` fails
   every ordered comparison (minimum, maximum, exclusive bounds);
   `inf` sits above every finite lower bound and violates every upper
   bound.

## Keyword support

| keyword | support |
| --- | --- |
| `type` | full (6 names + arrays with bare `null`) |
| `properties`, `patternProperties`, `additionalProperties` | full (`false` / schema) |
| `required`, `minProperties`, `maxProperties` | full |
| `propertyNames`, `dependentRequired`, `dependentSchemas` | full |
| `prefixItems`, `items`, `contains`, `minContains`, `maxContains` | full; array-form `items` (draft 07) reads as tuple prefixes |
| `additionalItems` | full (`false` / schema); applies beyond the tuple |
| `dependencies` | full (draft 07): array form as dependentRequired, schema form as dependentSchemas |
| `uniqueItems`, `minItems`, `maxItems` | full |
| `minLength`, `maxLength` | full, counted in code points |
| `pattern` | practical ECMA subset via a Thompson NFA (linear time, ReDoS-immune by construction): classes, quantifiers, groups, alternation, anchors, `\b/\B`, `\xNN/\uNNNN`, a leading `(?i)` ASCII-fold. Lazy quantifiers are accepted (no boolean-match difference). Backreferences, lookahead and lookbehind are rejected at compile |
| `format` | enforced set (below); other names are annotations (accepted, not enforced). String values are shape+range checked; TOML datetime values satisfy formats by kind (policy 3) |
| `contentEncoding`, `contentMediaType` | enforced pair, the draft 07 lens: `base64` is charset-checked (and decoded) before the `application/json` media check; unknown names on either side are no-ops |
| `minimum`, `maximum`, `exclusiveMinimum`, `exclusiveMaximum`, `multipleOf` | full; exact integer paths when the constraint is an exact integer token (no float drift on integers); `multipleOf` must be > 0 |
| `enum`, `const` | full, type-strict deep equality (policy 2/3) |
| `allOf`, `anyOf`, `oneOf`, `not`, `if`/`then`/`else` | full; failing trials are muted (they contribute no errors, only the verdict) |
| `$ref` | local only: `#`, JSON Pointer `#/definitions/...` (`~0`/`~1` unescaped), plain-name `#anchor` fragments from `$anchor`. Recursive refs are memoized; a cycle that consumes no document structure errors as cyclic. Remote/URL refs are rejected — a parser library never touches the network |
| `$schema`, `$id`, `$comment`, `$defs`, `definitions`, `title`, `description`, `default`, `examples`, `deprecated`, `readOnly`, `writeOnly` | annotations/containers: accepted, ignored |
| unknown keywords | accepted (2020-12 extension rule) |
| `unevaluatedProperties`, `unevaluatedItems`, `$dynamicRef`, `$dynamicAnchor`, `$recursiveRef`, `$recursiveAnchor`, `$vocabulary` | **rejected at compile** — never silently ignored; the diag JSON names the keyword |

The enforced format set (every name taplo's validation engine asserts
on, plus `uuid` and `duration` which taplo's shipped build cannot
reach): `date-time`, `date`, `time`, `email`, `idn-email`,
`hostname`, `idn-hostname`, `ipv4`, `ipv6`, `uri`, `iri`,
`uri-reference`, `iri-reference`, `json-pointer`,
`relative-json-pointer`, `regex`, `uri-template`, `uuid`,
`duration`, and taplo's own `semver` / `semver-requirement`.

## The taplo audit

taplo validates through the Rust `jsonschema` crate pinned at 0.17.1
with `default-features = false` — a **draft-07 lens**: drafts
2019-09/2020-12 are not compiled in, so every 2020-12-only keyword is
silently ignored, and the crate's own tests show draft-06/07 formats
validate by default. A differential battery (188 cases over every
keyword class, run against taplo's exact pipeline: DOM → serde JSON →
the crate) found teptris's enforced surface is a **strict superset**:

- teptris enforces keywords taplo silently ignores:
  `dependentRequired`, `dependentSchemas`, `prefixItems`,
  `minContains`/`maxContains` (the crate has no 2019-09 build),
  `uuid`/`duration` formats.
- teptris names unsupported constructs at compile; taplo ignores them
  and validates the document as if absent — a verdict that can
  contradict the schema's intent. teptris refuses loudly instead
  (`unevaluated*`, `$dynamic*`, `$recursive*`).
- teptris's `multipleOf` uses exact decimal arithmetic (0.3 is a
  multiple of 0.1); the crate's binary-fraction math rejects that
  pair. teptris follows the spec's real-number reading.
- teptris resolves `$anchor` plain-name fragments; taplo's draft-07
  lens does not.
- Datetime `const`/`enum` entries match both `Z` and `+00:00`
  spellings of the same instant (taplo re-formats through the `time`
  crate, which spells UTC the second way).

Documented divergences (teptris strict where taplo is loose, each a
deliberate policy above): `1.0` is never `integer`; `nan`/`inf`
compare as the TOML values they are (taplo's JSON round trip turns
them into `null`, so numeric constraints silently skip them);
hostname/idn-hostname accept ASCII alphanumerics where the crate's
check is Unicode-wide; the `regex` format trials the string in
teptris's own NFA dialect (backreferences and lookaround reject here
even though the crate's engine takes them); `format: time` and
`format: date-time` accept TOML local time/local datetime values by
kind, which taplo cannot (its JSON text for them carries no offset).
A schema declaring draft-04 gets taplo's draft-04 semantics only in
taplo; teptris reads it with the same lens as any other schema
(boolean-form `exclusiveMinimum` is an annotation here).

## Error envelope

Validation hands back one malloc'd JSON array; free it with `free()`
(not `teptris_diag_free` — that owns the compile diag). Up to 256
errors, each:

```json
{"path": "tool.demo.ratio", "schemaPath": "#/properties.tool/properties.demo/properties.ratio", "message": "expected a matching type, found string"}
```

`path` is the materialized TOML path (dotted; keys that are not bare
are quoted, array items carry their index). `schemaPath` is the
position in the schema. Documents that do not parse report the parse
error through `teptris_schema_validate`'s status (`TEPTRIS_ERR_SYNTAX`
with no buffer).

## Runtime surfaces

Added in the same wave on all four bindings (versions ride the engine
pin):

- **C**: `teptris_schema_compile` / `teptris_schema_validate` /
  `teptris_schema_validate_doc` / `teptris_schema_free` /
  `teptris_diag_free` (`teptris/schema.h`)
- **Rust**: `TeptrisSchema::compile(&str) -> Result<TeptrisSchema, String>`,
  `schema.validate(&str) -> Result<Vec<SchemaError>, ...>`
- **JS**: `compileSchema(json)` / `schema.validate(toml)`
- **Ruby**: `Teptris::Schema.compile(json)` / `schema.validate(toml)`
- **Python**: `teptris.compile_schema(json)` / `schema.validate(toml)`
- **CLI**: `teptris validate --schema schema.json file.toml`

## Tests

`test/unit/test_schema.cpp` — 62 tests: compile-time rejections, the
four policies, every keyword class, the recursive-ref memo, the error
envelope, the taplo parity surface (dependencies, additionalItems,
tuple items, content checks, the enforced format set, semver), and
two vendored real-world manifests (`test/fixtures/schema/`: a
cargo-like and a pyproject-like schema) validated against conforming
and violating documents.
`test/fuzz/fuzz_schema.c` — the E3 harness: schema/doc slicing, the
buffer contracts, compile/free loops under ASan/UBSan.
