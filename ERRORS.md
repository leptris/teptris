# Error-message audit (Phase 5, 2026-10-23)

Every diagnostic carries `line:column` inline, a specific reason, and
(teptris 0.3.8-dev) the enclosing table for duplicate keys. This file
records the cross-implementation audit that produced it.

## The matrix

14 canonical malformed inputs, parsed by teptris (engine CLI),
CPython's `tomllib` (3.13, the native reference for the py surface),
the `toml` crate 0.8 (the native reference for the rust surface), and
smol-toml (the js comparator). Full transcripts in the PR history.

Where implementations disagree, teptris's standing:

- **Diagnosis specificity: at or above every reference.** The cases
  where a generic message is the norm elsewhere — leading zeros
  (`leading zero in number`), out-of-range date parts (`invalid day`),
  hexadecimal scans (`expected digits`), duplicate keys (named, with
  the enclosing table since 0.3.8-dev) — teptris names the actual
  problem. `tomllib` reports generic statement-end errors for the
  numeric cases and leaks a tuple repr on duplicate tables;
  smol-toml wraps everything in one generic string and misdiagnoses
  unclosed strings/tables as "illegal character in key".
- **Presentation: the `toml` crate leads.** Its multi-line
  snippet-plus-caret layout is friendlier for humans. teptris keeps
  single-line library messages (the spans are the machine interface —
  every error carries byte offsets) and the CLI is the presentation
  layer ("CLI is tooling"): a snippet renderer there is a patch-line
  event, not a library change.
- **By-design accepts** (not errors): `\U` inside a literal string is
  legal TOML (escapes live in basic strings); type mismatches
  (`port = '8080'`) are valid TOML — type enforcement belongs to the
  host's schema layer (the Descriptor/plan API).

## The shipped improvement

Duplicate-key errors name the enclosing table, via an input-backed
header-path span kept in the parser (zero-copy, two pointer stores per
header):

```
a.b = 1
a.b = 2
```
→ `2:8: error: duplicate key 'b' in table 'a'`
