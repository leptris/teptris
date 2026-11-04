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
3. **datetimes are strings with named formats.** TOML datetime values
   satisfy formats by kind, following the TOML-native format table
   (tombi's named formats, made canonical here): an offset datetime
   satisfies `date-time` only; a local datetime satisfies
   `date-time-local` only; a local time satisfies `time-local` only; a
   date satisfies `date`. Plain strings are shape-checked against the
   same shapes — `date-time` requires the RFC 3339 offset, `time`
   requires it too, the `-local` names forbid it. Because JSON cannot
   carry a datetime literal, `enum`/`const` entries written as
   canonical RFC 3339 strings match datetime values by text (both `Z`
   and `+00:00` spellings of the same instant match).
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
| `minLength`, `maxLength` | full, counted in code points; count keywords accept integral float tokens (`"maxLength": 3.0` is legal JSON Schema and enforced) |
| `pattern` | practical ECMA subset via a Thompson NFA (linear time, ReDoS-immune by construction): classes, quantifiers, groups, alternation, anchors, `\b/\B`, `\xNN/\uNNNN`, Unicode property escapes `\p{...}` / `\P{...}` / `\p{^...}` (general categories long+short, plus Any / ASCII / White_Space / Alphabetic / Uppercase / Lowercase — generated tables; an unknown property names itself at compile), a leading `(?i)` ASCII-fold. Lazy quantifiers are accepted (no boolean-match difference). Backreferences, lookahead and lookbehind are rejected at compile |
| `format` | enforced set (below); other names are annotations (accepted, not enforced). String values are shape+range checked; TOML datetime values satisfy formats by the policy-3 kind table |
| `contentEncoding`, `contentMediaType` | enforced pair, the draft 07 lens: `base64` is charset-checked (and decoded) before the `application/json` media check; unknown names on either side are no-ops |
| `minimum`, `maximum`, `exclusiveMinimum`, `exclusiveMaximum`, `multipleOf` | full; exact integer paths when the constraint is an exact integer token (no float drift on integers); `multipleOf` must be > 0 |
| `enum`, `const` | full, type-strict deep equality (policy 2/3) |
| `allOf`, `anyOf`, `oneOf`, `not`, `if`/`then`/`else` | full; failing trials are muted (they contribute no errors, only the verdict) |
| `$ref` | full reference machinery over the **resource model**: every `$id` opens a resource (in-memory or file-backed under the compile's base dir), relative references RFC-3986-merge against the current base (http-space `$id`s resolve in memory, never fetched), file resources load size-capped, and the official metaschemas (draft-07/2019-09/2020-12) ship as built-in resources. `#`, `#/pointer` (`~0`/`~1` + percent-decoding), `#anchor`, and cross-resource URIs all resolve. Recursive refs are memoized; a cycle that consumes no document structure errors as cyclic. Network fetches are still rejected — a parser library never touches the network |
| `$schema`, `$id`, `$comment`, `$defs`, `definitions`, `title`, `description`, `default`, `examples`, `deprecated`, `readOnly`, `writeOnly` | annotations/containers: accepted, ignored |
| unknown keywords | accepted (2020-12 extension rule) |
| `unevaluatedProperties`, `unevaluatedItems` | full, per the 2020-12 annotation model: annotations from adjacent keywords and from in-place applicators ($ref, allOf, anyOf/oneOf successful branches, if-then-else) evaluate the instance; cousins never share; failed trials annotate nothing |
| `$dynamicRef`, `$dynamicAnchor`, `$recursiveRef`, `$recursiveAnchor` | full, spec-exact (2020-12 §8.2.3.2 + appendix C): each applied schema object enters its subtree's anchors into the dynamic scope; `$dynamicRef` resolves to the outermost resource on the evaluation path that defines the fragment (nearest-`$id` ownership, document order within a resource), the initial resolution standing as fallback; `$recursiveRef` participates only when the ref's own resource carries a true `$recursiveAnchor`, resolving to the first anchor-carrying resource root on the path. URI- and pointer-form dynamic refs read exactly like a plain `$ref` |
| `contentSchema` | full: asserts the decoded content (the parsed value for `application/json`, the decoded text otherwise) |
| `$vocabulary` | enforced through the `$schema` metaschema: a custom metaschema's `$vocabulary` decides which standard vocabularies the dialect keeps (core is mandatory; unlisted or false vocabularies turn their keywords into annotations). Unresolvable `$schema` keeps all vocabularies |

The enforced format set (23 names — the taplo parity set plus
tombi's TOML-native pair): `date-time`, `date`, `time`,
`date-time-local` (alias `partial-date-time`), `time-local` (alias
`partial-time`), `email`, `idn-email`, `hostname`, `idn-hostname`,
`ipv4`, `ipv6`, `uri`, `iri`, `uri-reference`, `iri-reference`,
`json-pointer`, `relative-json-pointer`, `regex`, `uri-template`,
`uuid`, `duration`, and taplo's own `semver` /
`semver-requirement`.

## The 2026 SOTA audit

Three engines were audited: taplo's shipped validator (the
`jsonschema` crate at 0.17.1, draft-07 lens — see the taplo audit
history in git), **tombi** (Even Better TOML's successor, the
widely-deployed TOML schema validator of 2026, with its own
validator), and the modern `jsonschema` crate (0.60.x, the
spec-exact reference). The decisive evidence is the **official JSON
Schema test suite** (draft 2020-12) run through teptris:

    python3 scripts/sota_suite_check.py
    # passed 964/1011, skipped 287 (pinned suite commit ed0ebd31)

And — the decisive gate this round — tombi's own suite definition
("Definition A": the official suite at tombi's pin, required files
only, drafts 7 / 2019-09 / 2020-12, the exact harness tombi runs at
1173/1173):

    python3 scripts/suiteA_check.py
    # passed 1173/1173, skipped 2318 (non-representable only),
    # schema-rejected 0 — full parity with tombi's json-schema-test

Every representable case passes; there are no remaining misses and no
schema rejections. The metaschema cases (definitions/remote-ref/vocab)
pass through the vendored built-in metaschemas (draft-07, 2019-09,
2020-12 and their meta files), so even `"$ref":
"https://json-schema.org/draft/2020-12/schema"` resolves without a
network. The skips are TOML-unrepresentable instances (null,
non-table roots) — the harness classifies every skip.

Where teptris stands against tombi and the modern crate:

- **unevaluatedProperties/unevaluatedItems** — implemented per the
  2020-12 annotation model and now verified against the official
  suite (the cousin rule, contains-annotates-indices, trial
  discipline, the if-retention nuance). tombi implements a subset;
  teptris's verdicts match the suite and the crate everywhere the
  suite tests.
- **$dynamicRef/$dynamicAnchor/$recursiveRef/$recursiveAnchor** —
  spec-exact across all three drafts, including the full dynamic-scope
  resource model (appendix C's outermost-resource rule, the bookend
  fallback, multiple dynamic paths, skip-over-intermediate-resources,
  and the metaschema's own `#meta` recursion). Pointer- and URI-form
  dynamic refs read like a plain `$ref`, as the suite defines them.
- **contentSchema** — asserted (tombi parses it as a schema keyword;
  teptris enforces the decoded content against it).
- **`date-time-local` / `time-local`** — tombi's TOML-native named
  formats, adopted here (aliases `partial-date-time`/`partial-time`);
  with the policy-3 kind table teptris's datetime/format mapping now
  agrees with tombi's exactly.
- **Deliberate divergences from tombi**: teptris follows the spec's
  implicit-`additionalProperties` semantics — tombi's lint defaults
  to strict mode (implicit `additionalProperties: false` at tables),
  which is a linter policy, not schema semantics; teptris stays
  spec-exact. teptris asserts `content`/`format` names tombi leaves
  as annotations. teptris never touches the network (tombi fetches
  schemas over HTTP); a schema carrying `$id` resources or remote
  `$ref`s is named at compile instead.
- **taplo remains a strict subset**: everything the 0.3.21 taplo
  audit established still holds (the draft-07 lens cannot reach
  `dependentRequired`/`dependentSchemas`/`prefixItems`/
  `minContains`/`maxContains`/`uuid`/`duration` or any 2020-12
  keyword; teptris enforces all of it, plus the taplo-only
  `semver`/`semver-requirement` formats).

This round also landed the **resource model** (`refs.c`): URI
joining/merging (RFC 3986 5.3), file-backed resources under the
compile's base directory, the built-in metaschema table, and
`$vocabulary`-gated keyword enforcement — the surface tombi's
validator actually exercises.

The audit also surfaced and fixed two engine bugs the suite exposed:
`jstring`'s escape path dropped the plain prefix of any JSON string
carrying an escape (`"foo\nbar"` decoded as `"\nbar"` — masked by
`properties`, caught by `required`), and count constraints written
as integral floats (`"maxLength": 3.0`) were silently ignored.

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

`test/unit/test_schema.cpp` — 75 tests: the four policies, every
keyword class, the recursive-ref memo, the error envelope, the taplo
parity surface, the 2026 SOTA surface (unevaluated* semantics,
dynamic refs, contentSchema, the local datetime formats), and two
vendored real-world manifests (`test/fixtures/schema/`) validated
against conforming and violating documents.
`scripts/sota_suite_check.py` — the official JSON Schema test suite
(draft 2020-12, pinned) through teptris: 964/1011 with every miss a
documented policy stance and every skip classified.
`test/fuzz/fuzz_schema.c` — the E3 harness: schema/doc slicing, the
buffer contracts, compile/free loops under ASan/UBSan.
