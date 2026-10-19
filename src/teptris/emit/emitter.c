#include "teptris/emit/emitter.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "emit_shared.h"

static void emit_inline_table(ebuf *b, const teptris_node *t);

static void emit_value(ebuf *b, const teptris_node *n)
{
    switch (n->kind) {
    case TEPTRIS_STRING:
        emit_string_value(b, n->as.str);
        break;
    case TEPTRIS_INTEGER:
        emit_i64(b, n->as.i);
        break;
    case TEPTRIS_FLOAT:
        emit_float(b, n->as.f);
        break;
    case TEPTRIS_BOOLEAN:
        eb_str(b, n->as.b ? "true" : "false");
        break;
    case TEPTRIS_DATETIME_OFFSET:
    case TEPTRIS_DATETIME_LOCAL:
    case TEPTRIS_DATE_LOCAL:
    case TEPTRIS_TIME_LOCAL:
        emit_dt(b, &n->as.dt, (teptris_kind)n->kind);
        break;
    case TEPTRIS_ARRAY: {
        /* first element hoisted: no per-element separator branch in
         * the loop (arrays are the element-densest shape) */
        size_t alen = n->as.array.len;
        eb_c(b, '[');
        if (alen > 0) {
            emit_value(b, n->as.array.items[0]);
            for (size_t i = 1; i < alen; i++) {
                eb_sep2(b);
                emit_value(b, n->as.array.items[i]);
            }
        }
        eb_c(b, ']');
        break;
    }
    case TEPTRIS_TABLE:
        emit_inline_table(b, n);
        break;
    default:
        break;
    }
}

static void emit_inline_table(ebuf *b, const teptris_node *t)
{
    eb_c(b, '{');
    for (size_t i = 0; i < t->as.table.len; i++) {
        if (i > 0) {
            eb_str(b, ", ");
        }
        emit_key(b, t->as.table.entries[i].key);
        eb_str(b, " = ");
        emit_value(b, t->as.table.entries[i].value);
    }
    eb_c(b, '}');
}

/* ------------------------------------------------------------- sections -- */

static bool is_section(const teptris_node *v)
{
    if (v->kind == TEPTRIS_TABLE) {
        return (v->flags & TBL_INLINE) == 0;
    }
    if (v->kind == TEPTRIS_ARRAY) {
        return (v->flags & ARR_AOT) != 0;
    }
    return false;
}

static void emit_table_children(ebuf *b, const char *prefix, size_t plen,
                                const teptris_node *t);

static void emit_section(ebuf *b, const char *prefix, size_t plen,
                         teptris_view key, const teptris_node *n)
{
    /* one stack buffer covers ordinary section paths (and the key is
     * rendered straight into it — one copy fewer than the old
     * ks + hdr pair) */
    char stack[512];
    size_t hcap = plen + key_buf_cap(key) + 2;
    char *hdr;
    if (hcap <= sizeof(stack)) {
        hdr = stack;
    } else {
        hdr = malloc(hcap);
        if (hdr == NULL) {
            b->st = TEPTRIS_ERR_ALLOC;
            return;
        }
    }
    size_t hl = 0;
    if (plen > 0) {
        memcpy(hdr, prefix, plen);
        hl = plen;
        hdr[hl++] = '.';
    }
    hl += key_to_buf(hdr + hl, key);

    if (n->kind == TEPTRIS_ARRAY) { /* array of tables */
        for (size_t i = 0; i < n->as.array.len; i++) {
            eb_str(b, "[[");
            eb_put(b, hdr, hl);
            eb_str(b, "]]\n");
            emit_table_children(b, hdr, hl, n->as.array.items[i]);
        }
    } else {
        eb_c(b, '[');
        eb_put(b, hdr, hl);
        eb_str(b, "]\n");
        emit_table_children(b, hdr, hl, n);
    }
    if (hdr != stack) {
        free(hdr);
    }
}

static void emit_table_children(ebuf *b, const char *prefix, size_t plen,
                                const teptris_node *t)
{
    for (size_t i = 0; i < t->as.table.len; i++) {
        const teptris_node *v = t->as.table.entries[i].value;
        if (!is_section(v)) {
            emit_key(b, t->as.table.entries[i].key);
            eb_str(b, " = ");
            emit_value(b, v);
            eb_c(b, '\n');
        }
    }
    for (size_t i = 0; i < t->as.table.len; i++) {
        const teptris_node *v = t->as.table.entries[i].value;
        if (is_section(v)) {
            emit_section(b, prefix, plen, t->as.table.entries[i].key, v);
        }
    }
}

teptris_status teptris_emit_document(const teptris_document *doc, char **buf,
                                     size_t *len)
{
    if (doc == NULL || doc->root == NULL) {
        return TEPTRIS_ERR_ARG;
    }
    ebuf b = {0, 0, 0, TEPTRIS_OK};
    eb_reserve(&b, 256);
    emit_table_children(&b, NULL, 0, doc->root);
    if (b.st != TEPTRIS_OK) {
        return b.st;
    }
    eb_c(&b, '\0');
    *buf = b.p;
    *len = b.len - 1;
    return TEPTRIS_OK;
}

/* ----------------------------------------------------------- typed JSON -- */

static void json_string(ebuf *b, teptris_view s)
{
    eb_c(b, '"');
    size_t i = 0;
    while (i < s.len) {
        size_t start = i;
        while (i < s.len) {
            unsigned char c = (unsigned char)s.ptr[i];
            if (c == '"' || c == '\\' || c < 0x20) {
                break;
            }
            i++;
        }
        eb_put(b, s.ptr + start, i - start);
        if (i == s.len) {
            break;
        }
        unsigned char c = (unsigned char)s.ptr[i++];
        switch (c) {
        case '"':
            eb_str(b, "\\\"");
            break;
        case '\\':
            eb_str(b, "\\\\");
            break;
        case '\b':
            eb_str(b, "\\b");
            break;
        case '\t':
            eb_str(b, "\\t");
            break;
        case '\n':
            eb_str(b, "\\n");
            break;
        case '\f':
            eb_str(b, "\\f");
            break;
        case '\r':
            eb_str(b, "\\r");
            break;
        default:
            if (c < 0x20) {
                char esc[8];
                snprintf(esc, sizeof(esc), "\\u%04X", c);
                eb_str(b, esc);
            }
        }
    }
    eb_c(b, '"');
}

static const char *json_type_name(teptris_kind k)
{
    switch (k) {
    case TEPTRIS_STRING:
        return "string";
    case TEPTRIS_INTEGER:
        return "integer";
    case TEPTRIS_FLOAT:
        return "float";
    case TEPTRIS_BOOLEAN:
        return "bool";
    case TEPTRIS_DATETIME_OFFSET:
        return "datetime";
    case TEPTRIS_DATETIME_LOCAL:
        return "datetime-local";
    case TEPTRIS_DATE_LOCAL:
        return "date-local";
    case TEPTRIS_TIME_LOCAL:
        return "time-local";
    default:
        return "";
    }
}

static void emit_json_value(ebuf *b, const teptris_node *n)
{
    switch (n->kind) {
    case TEPTRIS_TABLE:
        eb_c(b, '{');
        for (size_t i = 0; i < n->as.table.len; i++) {
            if (i > 0) {
                eb_c(b, ',');
            }
            json_string(b, n->as.table.entries[i].key);
            eb_c(b, ':');
            emit_json_value(b, n->as.table.entries[i].value);
        }
        eb_c(b, '}');
        break;
    case TEPTRIS_ARRAY:
        eb_c(b, '[');
        for (size_t i = 0; i < n->as.array.len; i++) {
            if (i > 0) {
                eb_c(b, ',');
            }
            emit_json_value(b, n->as.array.items[i]);
        }
        eb_c(b, ']');
        break;
    case TEPTRIS_STRING:
        eb_str(b, "{\"type\":\"string\",\"value\":");
        json_string(b, n->as.str);
        eb_c(b, '}');
        break;
    case TEPTRIS_INTEGER:
        eb_str(b, "{\"type\":\"integer\",\"value\":\"");
        emit_i64(b, n->as.i);
        eb_str(b, "\"}");
        break;
    case TEPTRIS_FLOAT:
        eb_str(b, "{\"type\":\"float\",\"value\":\"");
        emit_float(b, n->as.f);
        eb_str(b, "\"}");
        break;
    case TEPTRIS_BOOLEAN:
        eb_str(b, "{\"type\":\"bool\",\"value\":\"");
        eb_str(b, n->as.b ? "true" : "false");
        eb_str(b, "\"}");
        break;
    default:
        eb_str(b, "{\"type\":\"");
        eb_str(b, json_type_name((teptris_kind)n->kind));
        eb_str(b, "\",\"value\":\"");
        emit_dt(b, &n->as.dt, (teptris_kind)n->kind);
        eb_str(b, "\"}");
    }
}

/* Natural-JSON emit (#101 v1): host-shaped values instead of the
 * toml-test dialect - real numbers, booleans as JSON booleans,
 * datetimes as RFC 3339 strings (offset form carries its offset).
 * The two lossy spots are deliberate and documented in the header:
 * non-finite floats become null (JSON has no nan/inf), and the
 * datetime KIND is not distinguishable from a same-shaped string. */
static void emit_json_nat_value(ebuf *b, const teptris_node *n)
{
    switch (n->kind) {
    case TEPTRIS_TABLE:
        eb_c(b, '{');
        for (size_t i = 0; i < n->as.table.len; i++) {
            if (i > 0) {
                eb_c(b, ',');
            }
            json_string(b, n->as.table.entries[i].key);
            eb_c(b, ':');
            emit_json_nat_value(b, n->as.table.entries[i].value);
        }
        eb_c(b, '}');
        break;
    case TEPTRIS_ARRAY:
        eb_c(b, '[');
        for (size_t i = 0; i < n->as.array.len; i++) {
            if (i > 0) {
                eb_c(b, ',');
            }
            emit_json_nat_value(b, n->as.array.items[i]);
        }
        eb_c(b, ']');
        break;
    case TEPTRIS_STRING:
        json_string(b, n->as.str);
        break;
    case TEPTRIS_INTEGER:
        emit_i64(b, n->as.i);
        break;
    case TEPTRIS_FLOAT:
        if (isnan(n->as.f) || isinf(n->as.f)) {
            eb_str(b, "null");
        } else {
            emit_float(b, n->as.f);
        }
        break;
    case TEPTRIS_BOOLEAN:
        eb_str(b, n->as.b ? "true" : "false");
        break;
    default:
        eb_c(b, '"');
        emit_dt(b, &n->as.dt, (teptris_kind)n->kind);
        eb_c(b, '"');
    }
}

teptris_status teptris_emit_document_json_natural(
    const teptris_document *doc, char **buf, size_t *len)
{
    if (doc == NULL || doc->root == NULL) {
        return TEPTRIS_ERR_ARG;
    }
    ebuf b = {0, 0, 0, TEPTRIS_OK};
    eb_reserve(&b, 256);
    emit_json_nat_value(&b, doc->root);
    if (b.st != TEPTRIS_OK) {
        return b.st;
    }
    eb_c(&b, '\0');
    *buf = b.p;
    *len = b.len - 1;
    return TEPTRIS_OK;
}

teptris_status teptris_emit_document_json(const teptris_document *doc,
                                          char **buf, size_t *len)
{
    if (doc == NULL || doc->root == NULL) {
        return TEPTRIS_ERR_ARG;
    }
    ebuf b = {0, 0, 0, TEPTRIS_OK};
    eb_reserve(&b, 256);
    emit_json_value(&b, doc->root);
    if (b.st != TEPTRIS_OK) {
        return b.st;
    }
    eb_c(&b, '\0');
    *buf = b.p;
    *len = b.len - 1;
    return TEPTRIS_OK;
}
