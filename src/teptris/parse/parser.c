#include "teptris/parse/parse.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "teptris/common/chartype.h"
#include "teptris/memory/arena.h"

teptris_status tep_fail_at(teptris_parser *ps, const char *at, teptris_status code,
                           const char *fmt, ...)
{
    teptris_document *doc = ps->doc;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(doc->err_msg, sizeof(doc->err_msg), fmt, ap);
    va_end(ap);
    doc->err.status = code;
    doc->err.message = doc->err_msg;

    /* Position state is lazy: hot paths never maintain line/bol, so
     * every position is computed by one scan src -> pos (error paths
     * already abort the parse; this is the only line/column source). */
    const char *pos = (at != NULL) ? at : ps->p;
    size_t line = 1;
    const char *bol = ps->src;
    for (const char *q = ps->src; q < pos; q++) {
        if (*q == '\n') {
            line++;
            bol = q + 1;
        }
    }
    doc->err.line = line;
    doc->err.column = (size_t)(pos - bol) + 1;
    return code;
}

void tep_adv(teptris_parser *ps, size_t n)
{
    size_t avail = (size_t)(ps->end - ps->p);
    if (n > avail) {
        n = avail;
    }
    ps->p += n;
}

teptris_status tep_skip_ws(teptris_parser *ps)
{
    while (ps->p < ps->end && tep_is_ws((unsigned char)*ps->p)) {
        ps->p++;
    }
    return TEPTRIS_OK;
}

static teptris_status skip_comment(teptris_parser *ps)
{
    tep_adv(ps, 1); /* '#' */
    while (ps->p < ps->end) {
        unsigned char c = (unsigned char)*ps->p;
        if (c == '\n') {
            break;
        }
        if (c == '\r') {
            if (ps->p + 1 < ps->end && ps->p[1] == '\n') {
                break; /* EOL handled by the caller */
            }
            return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX,
                               "lone carriage return in comment");
        }
        if ((c < 0x20 && c != '\t') || c == 0x7F) {
            return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX,
                               "control character in comment");
        }
        ps->p++;
    }
    return TEPTRIS_OK;
}

teptris_status tep_skip_ws_nl(teptris_parser *ps)
{
    for (;;) {
        if (ps->p >= ps->end) {
            return TEPTRIS_OK;
        }
        unsigned char c = (unsigned char)*ps->p;
        if (tep_is_ws(c)) {
            tep_adv(ps, 1);
        } else if (c == '\n') {
            tep_adv(ps, 1);
        } else if (c == '\r' && ps->p + 1 < ps->end && ps->p[1] == '\n') {
            tep_adv(ps, 2);
        } else if (c == '#') {
            teptris_status st = skip_comment(ps);
            if (st != TEPTRIS_OK) {
                return st;
            }
        } else {
            return TEPTRIS_OK;
        }
    }
}

bool tep_at_eol(const teptris_parser *ps)
{
    if (ps->p >= ps->end) {
        return true;
    }
    if (*ps->p == '\n') {
        return true;
    }
    return *ps->p == '\r' && ps->p + 1 < ps->end && ps->p[1] == '\n';
}

teptris_status tep_finish_line(teptris_parser *ps)
{
    teptris_status st = tep_skip_ws(ps);
    if (st != TEPTRIS_OK) {
        return st;
    }
    if (ps->p < ps->end && *ps->p == '#') {
        st = skip_comment(ps);
        if (st != TEPTRIS_OK) {
            return st;
        }
    }
    if (ps->p >= ps->end) {
        return TEPTRIS_OK;
    }
    if (*ps->p == '\n') {
        tep_adv(ps, 1);
        return TEPTRIS_OK;
    }
    if (*ps->p == '\r' && ps->p + 1 < ps->end && ps->p[1] == '\n') {
        tep_adv(ps, 2);
        return TEPTRIS_OK;
    }
    return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX,
                       "expected newline after value");
}

/* ---------------------------------------------------------------- UTF-8 -- */

static size_t utf8_invalid_at(const char *data, size_t len)
{
    static const uint32_t mins[3] = {0x80, 0x800, 0x10000};
    size_t i = 0;
    while (i < len) {
        /* SWAR ASCII skip: 8 bytes per iteration while every high bit
         * is clear (all-ASCII corpora never pay the byte loop). A
         * word with a high bit falls through; the multibyte walker
         * resumes this loop after each sequence. */
        while (i + 8 <= len) {
            uint64_t w;
            memcpy(&w, data + i, 8);
            if ((w & 0x8080808080808080ULL) != 0) {
                break;
            }
            i += 8;
        }
        if (i >= len) {
            break;
        }
        unsigned char c = (unsigned char)data[i];
        if (c < 0x80) {
            i++;
            continue;
        }
        size_t need;
        uint32_t cp;
        if ((c & 0xE0) == 0xC0) {
            need = 1;
            cp = c & 0x1F;
        } else if ((c & 0xF0) == 0xE0) {
            need = 2;
            cp = c & 0x0F;
        } else if ((c & 0xF8) == 0xF0) {
            need = 3;
            cp = c & 0x07;
        } else {
            return i;
        }
        if (i + need >= len) {
            return i; /* truncated */
        }
        for (size_t k = 1; k <= need; k++) {
            unsigned char cc = (unsigned char)data[i + k];
            if ((cc & 0xC0) != 0x80) {
                return i;
            }
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (cp < mins[need - 1] || (cp >= 0xD800 && cp <= 0xDFFF) ||
            cp > 0x10FFFF) {
            return i;
        }
        i += need + 1;
    }
    return len;
}

static teptris_status validate_utf8(teptris_document *doc, const char *data,
                                    size_t len)
{
    size_t bad = utf8_invalid_at(data, len);
    if (bad == len) {
        return TEPTRIS_OK;
    }
    size_t line = 1;
    const char *bol = data;
    for (size_t i = 0; i < bad; i++) {
        if (data[i] == '\n') {
            line++;
            bol = data + i + 1;
        }
    }
    doc->err.status = TEPTRIS_ERR_ENCODING;
    doc->err.line = line;
    doc->err.column = bad - (size_t)(bol - data) + 1;
    snprintf(doc->err_msg, sizeof(doc->err_msg), "invalid UTF-8 sequence");
    doc->err.message = doc->err_msg;
    return TEPTRIS_ERR_ENCODING;
}

/* -------------------------------------------------------- key insertion -- */

static teptris_status insert_dotted(teptris_parser *ps, teptris_node *table,
                                    const teptris_view *parts, size_t count,
                                    teptris_node *value)
{
    teptris_document *doc = ps->doc;
    teptris_node *t = table;

    for (size_t i = 0; i + 1 < count; i++) {
        teptris_view part = parts[i];
        uint64_t h = teptris_dom_key_hash(part.ptr, part.len);
        size_t slot;
        teptris_entry *e =
            teptris_dom_table_probe_slot(t, h, part.ptr, part.len, &slot);
        if (e == NULL) {
            teptris_node *child = teptris_dom_new_table(doc, TBL_DOTTED);
            if (child == NULL) {
                return TEPTRIS_ERR_ALLOC;
            }
            teptris_status st =
                teptris_dom_table_insert_slot(doc, t, part, h, child, slot);
            if (st != TEPTRIS_OK) {
                return st;
            }
            t = child;
            continue;
        }
        teptris_node *v = e->value;
        if (v->kind != TEPTRIS_TABLE) {
            return tep_fail_at(ps, NULL, TEPTRIS_ERR_SEMANTIC,
                               "key '%.*s' is not a table", (int)part.len,
                               part.ptr);
        }
        if (v->flags & TBL_INLINE) {
            return tep_fail_at(ps, NULL, TEPTRIS_ERR_SEMANTIC,
                               "cannot extend inline table '%.*s'", (int)part.len,
                               part.ptr);
        }
        if (v->flags & (TBL_IMPLICIT | TBL_EXPLICIT)) {
            return tep_fail_at(ps, NULL, TEPTRIS_ERR_SEMANTIC,
                               "cannot extend table '%.*s' defined by a header",
                               (int)part.len, part.ptr);
        }
        t = v;
    }

    teptris_view last = parts[count - 1];
    uint64_t hlast = teptris_dom_key_hash(last.ptr, last.len);
    size_t last_slot;
    if (teptris_dom_table_probe_slot(t, hlast, last.ptr, last.len,
                                     &last_slot) != NULL) {
        return tep_fail_at(ps, NULL, TEPTRIS_ERR_SEMANTIC, "duplicate key '%.*s'",
                           (int)last.len, last.ptr);
    }
    return teptris_dom_table_insert_slot(doc, t, last, hlast, value, last_slot);
}

/* ------------------------------------------------------------- headers -- */

static teptris_status resolve_header(teptris_parser *ps, const teptris_view *parts,
                                     size_t count, bool aot, teptris_node **out)
{
    teptris_document *doc = ps->doc;
    teptris_node *t = doc->root;

    for (size_t i = 0; i + 1 < count; i++) {
        teptris_view part = parts[i];
        uint64_t h = teptris_dom_key_hash(part.ptr, part.len);
        size_t slot;
        teptris_entry *e =
            teptris_dom_table_probe_slot(t, h, part.ptr, part.len, &slot);
        if (e == NULL) {
            teptris_node *child = teptris_dom_new_table(doc, TBL_IMPLICIT);
            if (child == NULL) {
                return TEPTRIS_ERR_ALLOC;
            }
            teptris_status st =
                teptris_dom_table_insert_slot(doc, t, part, h, child, slot);
            if (st != TEPTRIS_OK) {
                return st;
            }
            t = child;
            continue;
        }
        teptris_node *v = e->value;
        if (v->kind == TEPTRIS_ARRAY) {
            if (!(v->flags & ARR_AOT)) {
                return tep_fail_at(ps, NULL, TEPTRIS_ERR_SEMANTIC,
                                   "cannot extend plain array '%.*s'",
                                   (int)part.len, part.ptr);
            }
            teptris_node *last =
                v->as.array.items[v->as.array.len - 1];
            if (last->kind != TEPTRIS_TABLE) {
                return tep_fail_at(ps, NULL, TEPTRIS_ERR_STATE,
                                   "corrupt array of tables");
            }
            t = last;
        } else if (v->kind == TEPTRIS_TABLE) {
            if (v->flags & TBL_INLINE) {
                return tep_fail_at(ps, NULL, TEPTRIS_ERR_SEMANTIC,
                                   "cannot extend inline table '%.*s'",
                                   (int)part.len, part.ptr);
            }
            /* TBL_DOTTED intermediates are walkable: the spec allows
             * [table] headers to define sub-tables within dotted-key
             * tables ([fruit.apple.texture] after apple.color = ...).
             * Exact redefinition is still rejected at the final part. */
            t = v;
        } else {
            return tep_fail_at(ps, NULL, TEPTRIS_ERR_SEMANTIC,
                               "key '%.*s' is not a table", (int)part.len,
                               part.ptr);
        }
    }

    teptris_view last = parts[count - 1];
    uint64_t hlast = teptris_dom_key_hash(last.ptr, last.len);
    size_t last_slot;
    teptris_entry *e = teptris_dom_table_probe_slot(t, hlast, last.ptr,
                                                    last.len, &last_slot);

    if (aot) {
        teptris_node *arr;
        if (e == NULL) {
            arr = teptris_dom_new_array(doc, ARR_AOT);
            if (arr == NULL) {
                return TEPTRIS_ERR_ALLOC;
            }
            teptris_status st = teptris_dom_table_insert_slot(doc, t, last,
                                                              hlast, arr,
                                                              last_slot);
            if (st != TEPTRIS_OK) {
                return st;
            }
        } else {
            arr = e->value;
            if (arr->kind != TEPTRIS_ARRAY || !(arr->flags & ARR_AOT)) {
                return tep_fail_at(ps, NULL, TEPTRIS_ERR_SEMANTIC,
                                   "cannot redefine '%.*s' as an array of "
                                   "tables",
                                   (int)last.len, last.ptr);
            }
        }
        teptris_node *elem = teptris_dom_new_table(doc, TBL_EXPLICIT);
        if (elem == NULL) {
            return TEPTRIS_ERR_ALLOC;
        }
        teptris_status st = teptris_dom_array_push(doc, arr, elem);
        if (st != TEPTRIS_OK) {
            return st;
        }
        *out = elem;
        return TEPTRIS_OK;
    }

    if (e == NULL) {
        teptris_node *tbl = teptris_dom_new_table(doc, TBL_EXPLICIT);
        if (tbl == NULL) {
            return TEPTRIS_ERR_ALLOC;
        }
        teptris_status st = teptris_dom_table_insert_slot(doc, t, last, hlast,
                                                          tbl, last_slot);
        if (st != TEPTRIS_OK) {
            return st;
        }
        *out = tbl;
        return TEPTRIS_OK;
    }

    teptris_node *v = e->value;
    if (v->kind == TEPTRIS_TABLE && (v->flags & TBL_IMPLICIT)) {
        v->flags = (uint8_t)((v->flags & ~TBL_IMPLICIT) | TBL_EXPLICIT);
        *out = v;
        return TEPTRIS_OK;
    }
    return tep_fail_at(ps, NULL, TEPTRIS_ERR_SEMANTIC,
                       "table '%.*s' defined more than once", (int)last.len,
                       last.ptr);
}

static teptris_status parse_header(teptris_parser *ps)
{
    tep_adv(ps, 1); /* '[' */
    bool aot = false;
    if (ps->p < ps->end && *ps->p == '[') {
        aot = true;
        tep_adv(ps, 1);
    }

    teptris_status st = tep_skip_ws(ps);
    if (st != TEPTRIS_OK) {
        return st;
    }
    teptris_view kbuf[8];
    teptris_view *parts;
    size_t count;
    st = teptris_parse_key_path(ps, kbuf, 8, &parts, &count);
    if (st != TEPTRIS_OK) {
        return st;
    }
    st = tep_skip_ws(ps);
    if (st != TEPTRIS_OK) {
        return st;
    }

    if (aot) {
        if (ps->end - ps->p < 2 || ps->p[0] != ']' || ps->p[1] != ']') {
            return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX, "expected ']]'");
        }
        tep_adv(ps, 2);
    } else {
        if (ps->p >= ps->end || *ps->p != ']') {
            return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX, "expected ']'");
        }
        tep_adv(ps, 1);
    }

    teptris_node *target;
    teptris_status rst = resolve_header(ps, parts, count, aot, &target);
    if (rst != TEPTRIS_OK) {
        return rst;
    }
    ps->cur = target;
    return TEPTRIS_OK;
}

/* Fast path for the dominant header shape: [bare.bare.bare] with no
 * ws, no quoted segments, no [[. The dotted path is scanned inline
 * (FNV fused into the scan, like try_keyval_fast) and resolved via
 * the fused insert path. Returns false BEFORE any mutation for every
 * other shape; when it returns true, *st_out carries the verdict and
 * *out the target table (errors tear down the document, so implicit
 * tables created before an error are harmless). */
/* noinline: single-caller statics inline into the main loop by
 * default, and the loop is per-LINE code — the 8th-lever lesson */
__attribute__((noinline)) static bool try_header_fast(teptris_parser *ps,
                                                     teptris_status *st_out,
                            teptris_node **out)
{
    const char *start = ps->p;
    if (start >= ps->end || *start != '[') {
        return false;
    }
    const char *q = start + 1;
    if (q >= ps->end || *q == '[') {
        return false; /* array-of-tables: general path */
    }
    teptris_view parts[8];
    uint64_t hs[8];
    size_t count = 0;
    for (;;) {
        if (q >= ps->end || !tep_is_barekey((unsigned char)*q)) {
            return false; /* empty/quoted segment or ws: general path */
        }
        if (count == 8) {
            return false;
        }
        uint64_t h = 1469598103934665603ULL;
        const char *seg = q;
        while (q < ps->end && tep_is_barekey((unsigned char)*q)) {
            h ^= (unsigned char)*q;
            h *= 1099511628211ULL;
            q++;
        }
        parts[count].ptr = seg;
        parts[count].len = (size_t)(q - seg);
        hs[count] = h;
        count++;
        if (q < ps->end && *q == '.') {
            q++;
            continue;
        }
        break;
    }
    if (q >= ps->end || *q != ']') {
        return false;
    }

    /* shape confirmed — commit the resolution */
    teptris_document *doc = ps->doc;
    teptris_node *t = doc->root;
    for (size_t i = 0; i + 1 < count; i++) {
        size_t slot;
        teptris_entry *e =
            teptris_dom_table_probe_slot(t, hs[i], parts[i].ptr,
                                         parts[i].len, &slot);
        if (e == NULL) {
            teptris_node *child = teptris_dom_new_table(doc, TBL_IMPLICIT);
            if (child == NULL) {
                *st_out = TEPTRIS_ERR_ALLOC;
                return true;
            }
            teptris_status st = teptris_dom_table_insert_slot(
                doc, t, parts[i], hs[i], child, slot);
            if (st != TEPTRIS_OK) {
                *st_out = st;
                return true;
            }
            t = child;
            continue;
        }
        teptris_node *v = e->value;
        if (v->kind == TEPTRIS_ARRAY) {
            if (!(v->flags & ARR_AOT)) {
                ps->p = q + 1; /* error position matches parse_header */
                *st_out = tep_fail_at(
                    ps, NULL, TEPTRIS_ERR_SEMANTIC,
                    "cannot extend plain array '%.*s'", (int)parts[i].len,
                    parts[i].ptr);
                return true;
            }
            t = v->as.array.items[v->as.array.len - 1];
        } else if (v->kind == TEPTRIS_TABLE) {
            if (v->flags & TBL_INLINE) {
                ps->p = q + 1; /* error position matches parse_header */
                *st_out = tep_fail_at(
                    ps, NULL, TEPTRIS_ERR_SEMANTIC,
                    "cannot extend inline table '%.*s'", (int)parts[i].len,
                    parts[i].ptr);
                return true;
            }
            t = v;
        } else {
            ps->p = q + 1; /* error position matches parse_header */
            *st_out = tep_fail_at(ps, NULL, TEPTRIS_ERR_SEMANTIC,
                                  "key '%.*s' is not a table",
                                  (int)parts[i].len, parts[i].ptr);
            return true;
        }
    }
    size_t last_slot;
    teptris_view last = parts[count - 1];
    if (teptris_dom_table_probe_slot(t, hs[count - 1], last.ptr, last.len,
                                     &last_slot) != NULL) {
        return false; /* exact-redefinition / implicit-convert: general */
    }
    teptris_node *tbl = teptris_dom_new_table(doc, TBL_EXPLICIT);
    if (tbl == NULL) {
        *st_out = TEPTRIS_ERR_ALLOC;
        return true;
    }
    teptris_status st = teptris_dom_table_insert_slot(doc, t, last,
                                                      hs[count - 1], tbl,
                                                      last_slot);
    if (st != TEPTRIS_OK) {
        *st_out = st;
        return true;
    }
    ps->p = q + 1;
    *st_out = TEPTRIS_OK;
    *out = tbl;
    return true;
}

/* Fused fast path for the dominant line shape: bare key, optional
 * ws, '=', optional ws, value — no dots, no quoted key. Scans the key
 * inline, parses the value, and does a single find+insert; avoids the
 * parts-array arena allocation and the general dotted-key machinery.
 * Rewinds and returns false for anything else. */
static bool try_keyval_fast(teptris_parser *ps, teptris_node *tbl,
                            teptris_status *st_out)
{
    const char *start = ps->p;
    if (start >= ps->end || !tep_is_barekey((unsigned char)*start)) {
        return false;
    }
    /* FNV-1a fused into the scan: the bytes are already loaded here,
     * so the hash comes free instead of a second pass in find */
    uint64_t h = 1469598103934665603ULL;
    const char *kend = ps->p;
    while (kend < ps->end && tep_is_barekey((unsigned char)*kend)) {
        h ^= (unsigned char)*kend;
        h *= 1099511628211ULL;
        kend++;
    }
    ps->p = kend;
    /* a '.' or quote means dotted/quoted: general path (rewind) */
    while (ps->p < ps->end && (*ps->p == ' ' || *ps->p == '\t')) {
        ps->p++;
    }
    if (ps->p >= ps->end || *ps->p != '=') {
        ps->p = start;
        return false;
    }
    ps->p++; /* '=' */
    while (ps->p < ps->end && (*ps->p == ' ' || *ps->p == '\t')) {
        ps->p++;
    }
    teptris_view key = {start, (size_t)(kend - start)};

    teptris_node *value;
    teptris_status st = teptris_parse_value_fast(ps, &value);
    if (st != TEPTRIS_OK) {
        *st_out = st;
        return true;
    }
    if (teptris_dom_table_find_probe(tbl, h, key.ptr, key.len) != NULL) {
        *st_out = tep_fail_at(ps, NULL, TEPTRIS_ERR_SEMANTIC,
                              "duplicate key '%.*s'", (int)key.len, key.ptr);
        return true;
    }
    *st_out = teptris_dom_table_insert_h(ps->doc, tbl, key, h, value);
    return true;
}

static teptris_status parse_keyval(teptris_parser *ps)
{
    teptris_status fast;
    if (try_keyval_fast(ps, ps->cur, &fast)) {
        return fast;
    }

    teptris_view kbuf[8];
    teptris_view *parts;
    size_t count;
    teptris_status st = teptris_parse_key_path(ps, kbuf, 8, &parts, &count);
    if (st != TEPTRIS_OK) {
        return st;
    }

    st = tep_skip_ws(ps);
    if (st != TEPTRIS_OK) {
        return st;
    }
    if (ps->p >= ps->end || *ps->p != '=') {
        return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX, "expected '=' after key");
    }
    tep_adv(ps, 1);
    st = tep_skip_ws(ps);
    if (st != TEPTRIS_OK) {
        return st;
    }

    teptris_node *value;
    st = teptris_parse_value(ps, &value);
    if (st != TEPTRIS_OK) {
        return st;
    }
    return insert_dotted(ps, ps->cur, parts, count, value);
}

/* -------------------------------------------------- arrays, inline tbls -- */


/* Array separator fast path: the overwhelmingly common shapes are
 * ", v" and ",\n  v". Consume those with inline loops; only a '#'
 * comment or CR forces the full skipper (which validates them). */
static teptris_status skip_array_gap(teptris_parser *ps)
{
    for (;;) {
        while (ps->p < ps->end && (*ps->p == ' ' || *ps->p == '\t')) {
            ps->p++;
        }
        if (ps->p >= ps->end) {
            return TEPTRIS_OK;
        }
        if (*ps->p == '\n') {
            ps->p++;
            continue;
        }
        if (*ps->p == '#') {
            return tep_skip_ws_nl(ps);
        }
        if (*ps->p == '\r') {
            return tep_skip_ws_nl(ps);
        }
        return TEPTRIS_OK;
    }
}

teptris_status teptris_parse_array(teptris_parser *ps, teptris_node **out)
{
    teptris_document *doc = ps->doc;
    teptris_node *arr = teptris_dom_new_array(doc, 0);
    if (arr == NULL) {
        return TEPTRIS_ERR_ALLOC;
    }
    tep_adv(ps, 1); /* '[' */
    ps->depth++;
    if (ps->depth > ps->max_depth) {
        return tep_fail_at(ps, NULL, TEPTRIS_ERR_DEPTH, "nesting too deep");
    }

    for (;;) {
        teptris_status st = skip_array_gap(ps);
        if (st != TEPTRIS_OK) {
            return st;
        }
        if (ps->p >= ps->end) {
            return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX,
                               "unterminated array");
        }
        if (*ps->p == ']') {
            tep_adv(ps, 1);
            break;
        }
        teptris_node *v;
        st = teptris_parse_value_fast(ps, &v);
        if (st != TEPTRIS_OK) {
            return st;
        }
        if (arr->as.array.len < arr->as.array.cap) {
            arr->as.array.items[arr->as.array.len++] = v;
        } else {
            st = teptris_dom_array_push(doc, arr, v);
            if (st != TEPTRIS_OK) {
                return st;
            }
        }
        st = skip_array_gap(ps);
        if (st != TEPTRIS_OK) {
            return st;
        }
        if (ps->p < ps->end && *ps->p == ',') {
            tep_adv(ps, 1);
            continue;
        }
        if (ps->p < ps->end && *ps->p == ']') {
            tep_adv(ps, 1);
            break;
        }
        return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX, "expected ',' or ']'");
    }

    ps->depth--;
    *out = arr;
    return TEPTRIS_OK;
}

teptris_status teptris_parse_inline(teptris_parser *ps, teptris_node **out)
{
    teptris_document *doc = ps->doc;
    teptris_node *tbl = teptris_dom_new_table(doc, TBL_INLINE);
    if (tbl == NULL) {
        return TEPTRIS_ERR_ALLOC;
    }
    tep_adv(ps, 1); /* '{' */
    ps->depth++;
    if (ps->depth > ps->max_depth) {
        return tep_fail_at(ps, NULL, TEPTRIS_ERR_DEPTH, "nesting too deep");
    }

    /* TOML 1.1 grammar: newlines (with comments) between members and
     * a trailing comma before '}' are accepted; the 1.0-strict
     * versions of those documents live in the versioned corpus's
     * invalid/ tree for parsers targeting 1.0 only. */
    for (;;) {
        teptris_status st = skip_array_gap(ps);
        if (st != TEPTRIS_OK) {
            return st;
        }
        if (ps->p >= ps->end) {
            return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX,
                               "unterminated inline table");
        }
        if (*ps->p == '}') {
            tep_adv(ps, 1);
            break;
        }

        teptris_status fast;
        if (try_keyval_fast(ps, tbl, &fast)) {
            if (fast != TEPTRIS_OK) {
                return fast;
            }
        } else {
            teptris_view kbuf[8];
            teptris_view *parts;
            size_t count;
            st = teptris_parse_key_path(ps, kbuf, 8, &parts, &count);
            if (st != TEPTRIS_OK) {
                return st;
            }
            st = tep_skip_ws(ps);
            if (st != TEPTRIS_OK) {
                return st;
            }
            if (ps->p >= ps->end || *ps->p != '=') {
                return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX,
                                   "expected '=' after key");
            }
            tep_adv(ps, 1);
            st = tep_skip_ws(ps);
            if (st != TEPTRIS_OK) {
                return st;
            }

            teptris_node *value;
            st = teptris_parse_value(ps, &value);
            if (st != TEPTRIS_OK) {
                return st;
            }
            st = insert_dotted(ps, tbl, parts, count, value);
            if (st != TEPTRIS_OK) {
                return st;
            }
        }

        st = skip_array_gap(ps);
        if (st != TEPTRIS_OK) {
            return st;
        }
        if (ps->p < ps->end && *ps->p == ',') {
            tep_adv(ps, 1);
            continue;
        }
        if (ps->p < ps->end && *ps->p == '}') {
            tep_adv(ps, 1);
            break;
        }
        return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX, "expected ',' or '}'");
    }

    ps->depth--;
    *out = tbl;
    return TEPTRIS_OK;
}

/* ------------------------------------------------------------ main loop -- */

teptris_status teptris_parser_run(teptris_document *doc, const char *data,
                                  size_t len)
{
    teptris_status st = validate_utf8(doc, data, len);
    if (st != TEPTRIS_OK) {
        return st;
    }
    size_t off = 0;
    if (len >= 3 && (unsigned char)data[0] == 0xEF && (unsigned char)data[1] == 0xBB &&
        (unsigned char)data[2] == 0xBF) {
        off = 3;
    }

    teptris_parser ps;
    memset(&ps, 0, sizeof(ps));
    ps.doc = doc;
    ps.src = data;
    ps.p = data + off;
    ps.end = data + len;
    ps.max_depth = doc->max_depth ? doc->max_depth : 512;

    doc->root = teptris_dom_new_table(doc, TBL_EXPLICIT);
    if (doc->root == NULL) {
        doc->err.status = TEPTRIS_ERR_ALLOC;
        snprintf(doc->err_msg, sizeof(doc->err_msg), "out of memory");
        doc->err.message = doc->err_msg;
        return TEPTRIS_ERR_ALLOC;
    }
    ps.cur = doc->root;

    for (;;) {
        st = tep_skip_ws(&ps);
        if (st != TEPTRIS_OK) {
            return st;
        }
        if (ps.p >= ps.end) {
            break;
        }
        unsigned char c = (unsigned char)*ps.p;
        if (c == '\n') {
            tep_adv(&ps, 1);
            continue;
        }
        if (c == '\r') {
            if (ps.p + 1 < ps.end && ps.p[1] == '\n') {
                tep_adv(&ps, 2);
                continue;
            }
            return tep_fail_at(&ps, NULL, TEPTRIS_ERR_SYNTAX,
                               "lone carriage return");
        }
        if (c == '#') {
            st = skip_comment(&ps);
            if (st != TEPTRIS_OK) {
                return st;
            }
            continue;
        }
        if (c == '[') {
            teptris_node *target = NULL;
            if (try_header_fast(&ps, &st, &target)) {
                if (st == TEPTRIS_OK) {
                    ps.cur = target;
                }
            } else {
                st = parse_header(&ps);
            }
        } else {
            st = parse_keyval(&ps);
        }
        if (st != TEPTRIS_OK) {
            return st;
        }
        st = tep_finish_line(&ps);
        if (st != TEPTRIS_OK) {
            return st;
        }
    }
    return TEPTRIS_OK;
}
