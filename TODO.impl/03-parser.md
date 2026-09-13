# 03 — Parser: TOML 1.0.0 grammar, single pass, line-oriented

Depends: 02, 04. Status: v1 complete (grammar + semantics; ledgered
edge cases below).

## Goal

One non-recursive-for-tables parser that turns bytes into the DOM with
the full TOML 1.0.0 grammar and the define-semantics enforced.

## Design

**Front-end.** UTF-8 validation of the whole input first (reject
overlong/surrogate/truncated; BOM skipped). Whitespace is space/tab
only. Newline is `\n` or `\r\n`; lone `\r` is a syntax error.
Comments run `#`→EOL and may not contain control chars other than
tab. Line/column tracked incrementally for error positions.

**Lines.** Each line is either blank, a comment, a table header, or a
key/value pair. After the first header, bare keys belong to that
header's table; root keys after a header are a semantic error.

**Keys.** Bare `[A-Za-z0-9_-]+`, basic-quoted (escapes), literal-
quoted; dotted paths join them. Key views point into the input
(zero-copy); uniqueness enforced by the table hash index.

**Define semantics** — flags on every table node:
`EXPLICIT` (named by a header), `IMPLICIT` (intermediate of a header
path), `DOTTED` (intermediate of a dotted key), `INLINE` (inline
table value), `AOT` members tracked by parent array.

- `[a.b]`: walk/create; intermediates become `IMPLICIT`; final
  implicit→explicit is legal, explicit→explicit is a redefine error;
  traversing/extending `INLINE` or `DOTTED` is an error; hitting a
  non-table is an error; `[t]` over a `[[t]]` array is an error.
- `[[a.b]]`: walk as above; final must be absent (create array) or an
  array created by a previous `[[...]]`; append a member table.
  Extending a plain array value is an error.
- `a.b = v`: within the *open* table, walk/create `DOTTED`
  intermediates; a later dotted key may add a sibling under the same
  prefix; redefining a leaf or traversing a scalar is an error.
- Duplicate key in one table → semantic error with position.

**Values.**

- *Strings.* basic `"…"` with `\b \t \n \f \r \" \\ \uXXXX
  \UXXXXXXXX` (surrogate/overlong rejected, scalar bounds enforced);
  raw control chars except tab rejected. Multi-line `"""` trims the
  first newline; backslash at EOL trims following whitespace/newlines.
  Literal `'…'` (no escapes). Multi-line `'''` trims first newline;
  1–2 internal quotes allowed; terminator is the first `'''` run not
  followed by more content-bearing quotes. Unescaped results are
  arena-copied, NUL-terminated.
- *Integers.* `[+-]?` decimal without leading zeros, `0x/0o/0b`
  radix forms; underscores must sit between digits; range int64;
  `-9223372036854775808` handled explicitly.
- *Floats.* decimal integer part + fraction and/or exponent
  (at least one required); `inf`, `nan` with optional sign;
  underscores as above; parsed with `strtod` on a copied buffer.
- *Booleans.* exactly `true` / `false`.
- *Datetimes.* Detected by `DDDD-` lookahead at value start. Offset
  datetime `YYYY-MM-DD(T|t|space)HH:MM:SS[.frac](Z|±HH:MM)`; local
  datetime (no offset); local date; local time `HH:MM:SS[.frac]`.
  Leap second `:60` accepted and preserved (toml-test has cases).
  Fraction accepts ≥1 digit, keeps the first 9 (ledger: reject-vs-
  truncate beyond 9 — truncating today, toml-test aligned). Calendar
  range validation (month 1–12, day per month, hour ≤ 23, etc.).
- *Arrays.* `[ … ]` with newlines and comments inside, trailing
  comma allowed, heterogeneous. Parsed recursively with the depth
  guard (default 512 → `ERR_DEPTH`, never a crash).
- *Inline tables.* `{ k = v, … }` single line, no trailing comma,
  no newlines; dotted keys inside allowed; members marked `INLINE`.

## Files

`src/teptris/parse/parser.c`, `parse/scalars.c`, `parse/keys.c`,
`parse/datetime.c` (+ internal headers).

## Acceptance gates

- Unit suites: every scalar family, every define-semantics rule (one
  error case each with line:col asserted), UTF-8 rejects, depth guard,
  lone `\r`, comment control-char.
- Corpus (item 07 inline v1): valid files parse to expected trees;
  invalid files produce the right status.

## Ledger

- secfrac > 9 digits: truncate (aligned with toml-test today).
- `+` sign on datetimes offsets, space separator accepted (1.0.0).
- Mixed `\r\n`/`\n` in one file is legal; only lone `\r` is not.
