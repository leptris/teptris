# teptris descriptor ABI (v1, shipped)

Fused schema-descriptor materialization — the TOML sibling of the
yeptris Schema ABI (yeptris#238) and the leptris plan walk. A
framework compiles a descriptor once per model and runs it on every
document; unplanned keys are never materialized.

**Header:** `src/include/teptris/plan.h` (`TEPTRIS_PLAN_ABI_VERSION 1`)
**Ruby surface:** `Teptris::Descriptor` / `Teptris::TOML.load_schema`
(teptris-ruby ≥ 0.2.29)
**C entry points:** `teptris_plan_build` → `teptris_plan_walk` →
row accessors / `teptris_plan_result_row_view` /
`teptris_plan_result_array_entry_at`

## Row kinds

| kind | C enum | meaning |
| --- | --- | --- |
| scalar | `TEPTRIS_PLAN_SCALAR` (1) | any TOML value at this key |
| collection | `TEPTRIS_PLAN_COLLECTION` (2) | array of scalars |
| nested | `TEPTRIS_PLAN_NESTED` (3) | recurse via `sub` plan index; also spans arrays of tables |
| raw | `TEPTRIS_PLAN_RAW` (4) | escape hatch — the untouched `teptris_node *` subtree |

Absent (or type-mismatched) planned keys read as `TEPTRIS_PLAN_MISSING`.

## Spec layout (flat, positional, C-friendly)

```
teptris_plan_spec {
  abi_version,                 /* TEPTRIS_PLAN_ABI_VERSION */
  plan_count,
  plans[],                     /* concatenation of every plan's rows */
  plan_first_row[plan_count+1] /* plan p owns [first_row[p], first_row[p+1]) */
}

teptris_plan_row {
  name,   /* table key (TOML wire name) */
  kind,   /* SCALAR | COLLECTION | NESTED | RAW */
  sub     /* sub-plan index when kind == NESTED; else 0 */
}
```

Plan numbering is pre-order; each plan's row range is **disjoint**.
Interleaved sibling ranges were the 0.2.29 binding bug — the builder
must reserve a contiguous range per plan before appending later
siblings (see teptris-ruby `Teptris::Descriptor.build`).

## Contract (mirrors yeptris#238 / lutaml-model needs)

1. **Escape hatch.** `kind: RAW` returns the document subtree; the
   host finishes just those fields. The bulk of the model fuses.
2. **The descriptor is an ABI, not a schema language.** Renames,
   two-names-one-slot merges, `when_attribute`, and polymorphic
   partitions are the caller's compiled plan's job.
3. **Versioned + lockstep.** `TEPTRIS_PLAN_ABI_VERSION` is the C
   gate; bindings refuse mismatched specs.
4. **Whole-document results.** Walk once; drain by row index. Never
   stream individual values across the FFI boundary mid-walk.
5. **TOML carries the type.** Unlike yeptris Schema's typed columns
   (`:str/:int/...`), plan rows do not declare a type tag — the
   value kind comes from the document, and host bindings materialize
   with their normal datetime/number contract.

## Ruby recipe (lutaml-model MappingHash → instantiate kwargs)

The Ruby walk returns a **nested Hash with String keys and
already-cast Ruby values** — closer to `Serializable.instantiate`
kwargs than yeptris Schema's column arrays:

```ruby
DESC = Teptris::Descriptor.build(
  children: [
    { name: "id",    kind: :scalar },
    { name: "name",  kind: :scalar },
    { name: "tags",  kind: :collection },
    { name: "items", kind: :nested, plan: {
        children: [
          { name: "sku", kind: :scalar },
          { name: "qty", kind: :scalar },
        ] } },
    { name: "extra", kind: :raw },
  ])

h = Teptris::TOML.load_schema(toml, DESC)
# => {"id"=>1, "name"=>"widget", "tags"=>[...],
#     "items"=>[{"sku"=>"x","qty"=>2}, ...], "extra"=>...}
# unplanned keys never appear; absent planned keys are nil

# framework half: wire→attr rename + symbolize, then
# Model.instantiate(**kwargs)
```

Compile the descriptor once per model class; reuse across documents.
Full consumer notes live in the teptris-ruby README ("Descriptor plan
path").

## Out of scope (track separately)

- Batch / many-small (`load_batch`) — same shape as the yeptris-ruby
  lutaml-model TODO; not blocking for single-document plan path.
- Typed column tags on the C plan row — not required for TOML; reopen
  only if a consumer needs reject-on-type-mismatch without reading
  the value kind after the walk.
