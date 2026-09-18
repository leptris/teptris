#include "teptris/emit/emitter.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "teptris/common/chartype.h"
#include "teptris/common/ryu/ryu_d2s.h"
#include "teptris/dom/dom.h"

typedef struct {
    char *p;
    size_t len, cap;
    teptris_status st;
} ebuf;

static void eb_reserve(ebuf *b, size_t extra)
{
    if (b->st != TEPTRIS_OK) {
        return;
    }
    if (b->len + extra + 1 <= b->cap) {
        return;
    }
    size_t ncap = b->cap ? b->cap : 256;
    while (b->len + extra + 1 > ncap) {
        ncap *= 2;
    }
    char *np = realloc(b->p, ncap);
    if (np == NULL) {
        free(b->p);
        b->p = NULL;
        b->st = TEPTRIS_ERR_ALLOC;
        return;
    }
    b->p = np;
    b->cap = ncap;
}

static void eb_put(ebuf *b, const char *s, size_t n)
{
    eb_reserve(b, n);
    if (b->st != TEPTRIS_OK) {
        return;
    }
    memcpy(b->p + b->len, s, n);
    b->len += n;
}

static void eb_c(ebuf *b, char c)
{
    eb_put(b, &c, 1);
}

static void eb_str(ebuf *b, const char *s)
{
    eb_put(b, s, strlen(s));
}

/* ------------------------------------------------------------- scalars -- */

static void emit_float(ebuf *b, double f)
{
    if (isnan(f)) {
        eb_str(b, "nan");
        return;
    }
    if (isinf(f)) {
        eb_str(b, f < 0 ? "-inf" : "inf");
        return;
    }
    /* ryu shortest round-trip digits ("D[.DD]E[X]"), re-rendered in TOML
     * notation: fixed-point when compact, scientific otherwise. */
    char tmp[32];
    int n = teptris_ryu_d2s_buffered_n(f, tmp);
    /* the _n ryu variant does NOT terminate: bound every later read */
    tmp[n] = '\0';

    size_t i = 0;
    bool neg = false;
    if (tmp[0] == '-') {
        neg = true;
        i = 1;
    }
    char digits[24];
    size_t nd = 0;
    for (; i < (size_t)n && tmp[i] != 'E'; i++) {
        if (tmp[i] != '.') {
            digits[nd++] = tmp[i];
        }
    }
    long sci_exp = (i < (size_t)n) ? strtol(tmp + i + 1, NULL, 10) : 0;
    long e10 = sci_exp - (long)nd + 1; /* value = digits * 10^e10 */

    char out[44];
    size_t o = 0;
    if (neg) {
        out[o++] = '-';
    }
    if (e10 >= 0 && e10 + (long)nd <= 16) {
        memcpy(out + o, digits, nd);
        o += nd;
        for (long k = 0; k < e10; k++) {
            out[o++] = '0';
        }
        out[o++] = '.';
        out[o++] = '0';
    } else if (e10 < 0 && -e10 <= (long)nd) {
        long ip = (long)nd + e10;
        if (ip > 0) {
            memcpy(out + o, digits, (size_t)ip);
            o += (size_t)ip;
            out[o++] = '.';
            memcpy(out + o, digits + ip, nd - (size_t)ip);
            o += nd - (size_t)ip;
        } else {
            out[o++] = '0';
            out[o++] = '.';
            memcpy(out + o, digits, nd);
            o += nd;
        }
    } else if (e10 < 0 && -e10 - (long)nd <= 6) {
        out[o++] = '0';
        out[o++] = '.';
        for (long k = 0; k < -e10 - (long)nd; k++) {
            out[o++] = '0';
        }
        memcpy(out + o, digits, nd);
        o += nd;
    } else {
        out[o++] = digits[0];
        if (nd > 1) {
            out[o++] = '.';
            memcpy(out + o, digits + 1, nd - 1);
            o += nd - 1;
        }
        o += (size_t)snprintf(out + o, sizeof(out) - o, "e%ld", sci_exp);
    }
    eb_put(b, out, o);
}

static size_t put_digits2(char *p, unsigned v)
{
    p[0] = (char)('0' + (v / 10));
    p[1] = (char)('0' + (v % 10));
    return 2;
}

static size_t put_digits4(char *p, unsigned v)
{
    put_digits2(p, v / 100);
    put_digits2(p + 2, v % 100);
    return 4;
}

static void emit_dt(ebuf *b, const teptris_datetime *dt, teptris_kind k)
{
    char tmp[64];
    size_t n = 0;
    if (k != TEPTRIS_TIME_LOCAL) {
        n += put_digits4(tmp + n, (unsigned)dt->year);
        tmp[n++] = '-';
        n += put_digits2(tmp + n, dt->month);
        tmp[n++] = '-';
        n += put_digits2(tmp + n, dt->day);
        if (k != TEPTRIS_DATE_LOCAL) {
            tmp[n++] = 'T';
        }
    }
    if (k != TEPTRIS_DATE_LOCAL) {
        n += put_digits2(tmp + n, dt->hour);
        tmp[n++] = ':';
        n += put_digits2(tmp + n, dt->minute);
        tmp[n++] = ':';
        n += put_digits2(tmp + n, dt->second);
    }
    if (dt->nanosecond != 0 && k != TEPTRIS_DATE_LOCAL) {
        tmp[n++] = '.';
        unsigned v = dt->nanosecond;
        for (int i = 8; i >= 0; i--) {
            tmp[n + (size_t)i] = (char)('0' + v % 10);
            v /= 10;
        }
        n += 9;
        while (n > 0 && tmp[n - 1] == '0') {
            n--; /* trim trailing fraction zeros, keep at least one digit */
        }
    }
    if (k == TEPTRIS_DATETIME_OFFSET) {
        int32_t off = dt->offset_seconds;
        if (off == 0) {
            tmp[n++] = 'Z';
        } else {
            int32_t a = (off < 0) ? -off : off;
            tmp[n++] = (off < 0) ? '-' : '+';
            n += put_digits2(tmp + n, (unsigned)(a / 3600));
            tmp[n++] = ':';
            n += put_digits2(tmp + n, (unsigned)((a % 3600) / 60));
        }
    }
    eb_put(b, tmp, n);
}

static void emit_basic_bytes(ebuf *b, const char *s, size_t n)
{
    eb_c(b, '"');
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
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
            if (c < 0x20 || c == 0x7F) {
                char esc[8];
                snprintf(esc, sizeof(esc), "\\u%04X", c);
                eb_str(b, esc);
            } else {
                eb_c(b, (char)c);
            }
        }
    }
    eb_c(b, '"');
}

static void emit_string_value(ebuf *b, teptris_view s)
{
    bool literal = true;
    for (size_t i = 0; i < s.len; i++) {
        unsigned char c = (unsigned char)s.ptr[i];
        if (c == '\'' || (c < 0x20 && c != '\t') || c == 0x7F) {
            literal = false;
            break;
        }
    }
    if (literal) {
        eb_c(b, '\'');
        eb_put(b, s.ptr, s.len);
        eb_c(b, '\'');
        return;
    }
    emit_basic_bytes(b, s.ptr, s.len);
}

/* Worst-case quoted-key length: 6 bytes per input byte (\u00XX) + 2 quotes. */
static size_t key_buf_cap(teptris_view key)
{
    return key.len * 6 + 4;
}

static size_t key_to_buf(char *dst, teptris_view key)
{
    bool bare = key.len > 0;
    for (size_t i = 0; i < key.len; i++) {
        if (!tep_is_barekey((unsigned char)key.ptr[i])) {
            bare = false;
            break;
        }
    }
    if (bare) {
        memcpy(dst, key.ptr, key.len);
        return key.len;
    }
    bool literal = true;
    for (size_t i = 0; i < key.len; i++) {
        unsigned char c = (unsigned char)key.ptr[i];
        if (c == '\'' || (c < 0x20 && c != '\t') || c == 0x7F) {
            literal = false;
            break;
        }
    }
    size_t n = 0;
    if (literal) {
        dst[n++] = '\'';
        memcpy(dst + n, key.ptr, key.len);
        n += key.len;
        dst[n++] = '\'';
        return n;
    }
    dst[n++] = '"';
    for (size_t i = 0; i < key.len; i++) {
        unsigned char c = (unsigned char)key.ptr[i];
        switch (c) {
        case '"':
            dst[n++] = '\\';
            dst[n++] = '"';
            break;
        case '\\':
            dst[n++] = '\\';
            dst[n++] = '\\';
            break;
        default:
            if (c < 0x20) {
                static const char hex[] = "0123456789ABCDEF";
                dst[n++] = '\\';
                dst[n++] = 'u';
                dst[n++] = '0';
                dst[n++] = '0';
                dst[n++] = hex[c >> 4];
                dst[n++] = hex[c & 0xF];
            } else {
                dst[n++] = (char)c;
            }
        }
    }
    dst[n++] = '"';
    return n;
}

static void emit_key(ebuf *b, teptris_view key)
{
    /* keys <= ~42 bytes (every bare key, nearly all quoted) never
     * touch the allocator */
    char stack[256];
    if (key_buf_cap(key) <= sizeof(stack)) {
        size_t n = key_to_buf(stack, key);
        eb_put(b, stack, n);
        return;
    }
    char *ks = malloc(key_buf_cap(key));
    if (ks == NULL) {
        b->st = TEPTRIS_ERR_ALLOC;
        return;
    }
    size_t n = key_to_buf(ks, key);
    eb_put(b, ks, n);
    free(ks);
}

/* --------------------------------------------------------------- values -- */

static void emit_inline_table(ebuf *b, const teptris_node *t);

static void emit_value(ebuf *b, const teptris_node *n)
{
    char tmp[32];
    switch (n->kind) {
    case TEPTRIS_STRING:
        emit_string_value(b, n->as.str);
        break;
    case TEPTRIS_INTEGER: {
        /* direct writer: snprintf costs a format-parse + locale check
         * per number and dominates int-heavy emits */
        uint64_t mag;
        bool neg = n->as.i < 0;
        if (neg) {
            mag = (uint64_t)(-(n->as.i + 1)) + 1; /* INT64_MIN safe */
        } else {
            mag = (uint64_t)n->as.i;
        }
        char *q = tmp + sizeof(tmp);
        *--q = '\0';
        do {
            *--q = (char)('0' + (mag % 10));
            mag /= 10;
        } while (mag != 0);
        if (neg) {
            *--q = '-';
        }
        eb_put(b, q, (size_t)(tmp + sizeof(tmp) - 1 - q));
        break;
    }
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
    case TEPTRIS_ARRAY:
        eb_c(b, '[');
        for (size_t i = 0; i < n->as.array.len; i++) {
            if (i > 0) {
                eb_str(b, ", ");
            }
            emit_value(b, n->as.array.items[i]);
        }
        eb_c(b, ']');
        break;
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
    for (size_t i = 0; i < s.len; i++) {
        unsigned char c = (unsigned char)s.ptr[i];
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
            } else {
                eb_c(b, (char)c);
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
    char tmp[32];
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
        snprintf(tmp, sizeof(tmp), "%" PRId64, n->as.i);
        eb_str(b, "{\"type\":\"integer\",\"value\":\"");
        eb_str(b, tmp);
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
