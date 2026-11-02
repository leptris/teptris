# Schemas — validating TOML against JSON Schema (2020-12 practical subset)

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
2. **integer is TOML-native.** `1.0` is a float and never validates as
   `integer` (exactly the 2020-12 rule); `integer` accepts TOML
   integers only, `number` accepts both, keeping integer identity.
   `enum`/`const` compare type-strictly: the schema's `1` does not
   match `1.0`.
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
| `prefixItems`, `items`, `contains`, `minContains`, `maxContains` | full |
| `uniqueItems`, `minItems`, `maxItems` | full |
| `minLength`, `maxLength` | full, counted in code points |
| `pattern` | practical ECMA subset via a Thompson NFA (linear time, ReDoS-immune by construction): classes, quantifiers, groups, alternation, anchors, `\b/\B`, `\xNN/\uNNNN`, a leading `(?i)` ASCII-fold. Lazy quantifiers are accepted (no boolean-match difference). Backreferences, lookahead and lookbehind are rejected at compile |
| `format` | `date-time`, `date`, `time`; other format names are annotations (accepted, not enforced) |
| `minimum`, `maximum`, `exclusiveMinimum`, `exclusiveMaximum`, `multipleOf` | full; exact integer paths when the constraint is an exact integer token (no float drift on integers); `multipleOf` must be > 0 |
| `enum`, `const` | full, type-strict deep equality (policy 2/3) |
| `allOf`, `anyOf`, `oneOf`, `not`, `if`/`then`/`else` | full; failing trials are muted (they contribute no errors, only the verdict) |
| `$ref` | local only: `#`, JSON Pointer `#/definitions/...` (`~0`/`~1` unescaped), plain-name `#anchor` fragments from `$anchor`. Recursive refs are memoized; a cycle that consumes no document structure errors as cyclic. Remote/URL refs are rejected — a parser library never touches the network |
| `$schema`, `$id`, `$comment`, `$defs`, `definitions`, `title`, `description`, `default`, `examples`, `deprecated`, `readOnly`, `writeOnly` | annotations/containers: accepted, ignored |
| unknown keywords | accepted (2020-12 extension rule) |
| `unevaluatedProperties`, `unevaluatedItems`, `$dynamicRef`, `$dynamicAnchor`, `$recursiveRef`, `$recursiveAnchor`, `$vocabulary`, `contentEncoding`, `contentMediaType` | **rejected at compile** — never silently ignored; the diag JSON names the keyword |

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

`test/unit/test_schema.cpp` — 45 tests: compile-time rejections, the
four policies, every keyword class, the recursive-ref memo, the error
envelope, and two vendored real-world manifests
(`test/fixtures/schema/`: a cargo-like and a pyproject-like schema)
validated against conforming and violating documents.
`test/fuzz/fuzz_schema.c` — the E3 harness: schema/doc slicing, the
buffer contracts, compile/free loops under ASan/UBSan.
