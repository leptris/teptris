# 18 — JSON view: the integer writer parity gap

SIZED (2026-09-18). `emit_json_value` still pays `snprintf("%"
PRId64)` per integer (emitter.c) while the TOML view has had the
direct backwards writer since v0.1.21's predecessor (05). Extract
one `emit_i64` helper used by both views — the 05 lever applied to
its second surface.

Surface: the JSON emitter backs the CLI `parse` output and the
toml-test runner. `bench_emit` times `teptris_document_emit` only,
so the lane gives NO verdict here — the gate is byte-exactness:
toml-test 714/0 on the 1.1 view (the runner compares this emitter's
output) plus the 73/73 builder round-trips. Perf benefit accrues to
every JSON-view consumer (CLI, toml-test) rather than the bench.

Also folds in: `json_string` gets 17's span-batched escaping with
the JSON predicate (escape `"`, `\`, <0x20; 0x7F passes raw, as
today).
