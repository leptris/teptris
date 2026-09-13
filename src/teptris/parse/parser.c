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

    const char *pos = (at != NULL) ? at : ps->p;
    if (pos >= ps->bol && pos <= ps->end) {
        doc->err.line = ps->line;
        doc->err.column = (size_t)(pos - ps->bol) + 1;
    } else {
        /* Position on an earlier line (e.g. unterminated string): rescan. */
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
    }
    return code;
}

void tep_adv(teptris_parser *ps, size_t n)
{
    size_t avail = (size_t)(ps->end - ps->p);
    if (n > avail) {
        n = avail;
    }
    /* memchr pays off only on long spans; short tokens take the loop. */
    if (n >= 16 && memchr(ps->p, '\n', n) == NULL) {
        ps->p += n;
        return;
    }
    for (size_t i = 0; i < n; i++) {
        if (*ps->p == '\n') {
            ps->line++;
            ps->bol = ps->p + 1;
        }
        ps->p++;
    }
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
        uint64_t h;
        teptris_entry *e = teptris_dom_table_find_h(t, part.ptr, part.len, &h);
        if (e == NULL) {
            teptris_node *child = teptris_dom_new_table(doc, TBL_DOTTED);
            if (child == NULL) {
                return TEPTRIS_ERR_ALLOC;
            }
            teptris_status st = teptris_dom_table_insert_h(doc, t, part, h, child);
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
    uint64_t hlast;
    if (teptris_dom_table_find_h(t, last.ptr, last.len, &hlast) != NULL) {
        return tep_fail_at(ps, NULL, TEPTRIS_ERR_SEMANTIC, "duplicate key '%.*s'",
                           (int)last.len, last.ptr);
    }
    return teptris_dom_table_insert_h(doc, t, last, hlast, value);
}

/* ------------------------------------------------------------- headers -- */

static teptris_status resolve_header(teptris_parser *ps, const teptris_view *parts,
                                     size_t count, bool aot, teptris_node **out)
{
    teptris_document *doc = ps->doc;
    teptris_node *t = doc->root;

    for (size_t i = 0; i + 1 < count; i++) {
        teptris_view part = parts[i];
        uint64_t h;
        teptris_entry *e = teptris_dom_table_find_h(t, part.ptr, part.len, &h);
        if (e == NULL) {
            teptris_node *child = teptris_dom_new_table(doc, TBL_IMPLICIT);
            if (child == NULL) {
                return TEPTRIS_ERR_ALLOC;
            }
            teptris_status st = teptris_dom_table_insert_h(doc, t, part, h, child);
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
            if (v->flags & TBL_DOTTED) {
                return tep_fail_at(ps, NULL, TEPTRIS_ERR_SEMANTIC,
                                   "cannot extend table '%.*s' defined by a "
                                   "dotted key",
                                   (int)part.len, part.ptr);
            }
            t = v;
        } else {
            return tep_fail_at(ps, NULL, TEPTRIS_ERR_SEMANTIC,
                               "key '%.*s' is not a table", (int)part.len,
                               part.ptr);
        }
    }

    teptris_view last = parts[count - 1];
    uint64_t hlast;
    teptris_entry *e = teptris_dom_table_find_h(t, last.ptr, last.len, &hlast);

    if (aot) {
        teptris_node *arr;
        if (e == NULL) {
            arr = teptris_dom_new_array(doc, ARR_AOT);
            if (arr == NULL) {
                return TEPTRIS_ERR_ALLOC;
            }
            teptris_status st = teptris_dom_table_insert_h(doc, t, last, hlast, arr);
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
        teptris_status st = teptris_dom_table_insert_h(doc, t, last, hlast, tbl);
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
    teptris_view *parts;
    size_t count;
    st = teptris_parse_key_path(ps, &parts, &count);
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

static teptris_status parse_keyval(teptris_parser *ps)
{
    teptris_view *parts;
    size_t count;
    teptris_status st = teptris_parse_key_path(ps, &parts, &count);
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
        teptris_status st = tep_skip_ws_nl(ps);
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
        st = teptris_parse_value(ps, &v);
        if (st != TEPTRIS_OK) {
            return st;
        }
        st = teptris_dom_array_push(doc, arr, v);
        if (st != TEPTRIS_OK) {
            return st;
        }
        st = tep_skip_ws_nl(ps);
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

    bool need_member = false;
    for (;;) {
        teptris_status st = tep_skip_ws(ps);
        if (st != TEPTRIS_OK) {
            return st;
        }
        if (ps->p >= ps->end) {
            return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX,
                               "unterminated inline table");
        }
        if (*ps->p == '}') {
            if (need_member) {
                return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX,
                                   "trailing comma in inline table");
            }
            tep_adv(ps, 1);
            break;
        }

        teptris_view *parts;
        size_t count;
        st = teptris_parse_key_path(ps, &parts, &count);
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

        st = tep_skip_ws(ps);
        if (st != TEPTRIS_OK) {
            return st;
        }
        if (ps->p < ps->end && *ps->p == ',') {
            tep_adv(ps, 1);
            need_member = true;
            continue;
        }
        if (ps->p < ps->end && *ps->p == '}') {
            tep_adv(ps, 1);
            break;
        }
        if (ps->p < ps->end && (*ps->p == '\n' ||
                                (*ps->p == '\r' && ps->p + 1 < ps->end &&
                                 ps->p[1] == '\n'))) {
            return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX,
                               "newline not allowed in inline table");
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
    ps.bol = data + off;
    ps.line = 1;
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
        st = (c == '[') ? parse_header(&ps) : parse_keyval(&ps);
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
