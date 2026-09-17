#ifndef TEPTRIS_PARSE_H
#define TEPTRIS_PARSE_H

#include "teptris/dom/dom.h"
#include "teptris/teptris.h"

typedef struct teptris_parser {
    teptris_document *doc;
    const char *src;  /* input start (error position rescans) */
    const char *p;    /* cursor */
    const char *end;  /* one past last input byte */
    uint32_t depth;
    uint32_t max_depth;
    teptris_node *cur; /* open table (root until the first header) */
} teptris_parser;

/* parser.c entry: validates UTF-8, skips BOM, runs the line loop. */
teptris_status teptris_parser_run(teptris_document *doc, const char *data,
                                  size_t len);

/* Record a failure into doc->err at the current cursor (or `at` when
 * non-NULL). Always returns `code` so callers `return tep_fail(...)`. */
teptris_status tep_fail_at(teptris_parser *ps, const char *at, teptris_status code,
                           const char *fmt, ...);

/* Advance the cursor n bytes (clamped to the end). Position state
 * (line/bol) is lazy: tep_fail_at computes it by rescan on error. */
void tep_adv(teptris_parser *ps, size_t n);

/* scalars.c: dispatch on the first byte; parses any value. */
teptris_status teptris_parse_value(teptris_parser *ps, teptris_node **out);
teptris_status teptris_parse_value_fast(teptris_parser *ps, teptris_node **out);
/* Number (int/float/inf/nan); signed_input: cursor at '+'/'-'. */
teptris_status teptris_parse_number(teptris_parser *ps, teptris_node **out,
                                    bool signed_input);

/* parser.c: composite values. */
teptris_status teptris_parse_array(teptris_parser *ps, teptris_node **out);
teptris_status teptris_parse_inline(teptris_parser *ps, teptris_node **out);

/* Shared by keys.c for quoted keys. Decode a basic string body (opening
 * quote(s) consumed by the caller) into `out` (NULL = count only). */
teptris_status tep_scan_basic(teptris_parser *ps, bool multiline, char *out,
                              size_t *out_len);
teptris_status tep_scan_literal(teptris_parser *ps, bool multiline, char *out,
                                size_t *out_len);

/* scalars.c: fast path — lookahead for a single-line string body with no
 * escapes and no control bytes (the common case). Does not move the
 * cursor; on success the caller copies span→arena and advances len+1. */
bool teptris_try_plain_body(teptris_parser *ps, char close, const char **content,
                            size_t *len);

/* keys.c: dotted key path. Parts are views (input or arena copies). */
/* Parses a dotted key path. `sbuf`/`scap` is a CALLER-OWNED buffer
 * (typically a stack array of 8): paths that fit never allocate, and
 * *parts points into it or into the document arena — valid until the
 * caller's frame ends or the document is freed. */
teptris_status teptris_parse_key_path(teptris_parser *ps,
                                      teptris_view *sbuf, size_t scap,
                                      teptris_view **parts, size_t *count);

/* datetime.c */
bool teptris_datetime_lookahead(const teptris_parser *ps);
teptris_status teptris_parse_datetime(teptris_parser *ps, teptris_node **out);

/* parser.c: skip space/tab (parser loop) — arrays also allow newlines
 * and comments. Both validate comment contents. */
teptris_status tep_skip_ws(teptris_parser *ps);
teptris_status tep_skip_ws_nl(teptris_parser *ps);
/* True at cursor when at '\n', "\r\n" or EOF. */
bool tep_at_eol(const teptris_parser *ps);
/* Consume ws, optional comment, and the EOL; error when trailing bytes. */
teptris_status tep_finish_line(teptris_parser *ps);

#endif /* TEPTRIS_PARSE_H */
