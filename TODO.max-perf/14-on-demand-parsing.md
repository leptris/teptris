# 14 — On-demand / lazy DOM (simdjson's on-demand API)

Research: simdjson's second big win is parsing only what the consumer
asks for.

Status: **shipped for the planned-read case (teptris #46, v0.1.18).**
The plan-walk descriptor (teptris_plan_build/walk + ruby
Teptris::Descriptor) is exactly on-demand parsing: a schema-declared
row plan lets the consumer extract only wanted keys/values in one
document pass without materializing the full DOM. Unplanned keys are
skipped. Bindings: Teptris::TOML.load_schema.
