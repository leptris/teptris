# 10 — Incremental re-parsing (tree-sitter / gpeg)

Research: tree-sitter re-parses on every keystroke; gpeg (SLE'21)
keeps packrat memories incremental.

Status: **API-surface idea, not a bench lever.** One-shot parse
benchmarks gain nothing. If an editor-integration consumer ever
appears, the plan: keep the document + arena alive, re-parse only
changed lines' subtree ranges, reuse unchanged table nodes. Blocked on
a consumer asking for it.
