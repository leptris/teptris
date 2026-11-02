/* The schema-side JSON reader: a small, complete RFC 8259 parser
 * producing an arena-owned DOM for schema compilation. Numbers keep
 * their integer identity when the token is exact and fits int64 (enum
 * and const need exactness against TOML integers); depth is capped so
 * hostile nesting cannot blow the C stack. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json_read.h"
#include "teptris/memory/arena.h"

#define JSON_MAX_DEPTH 128

struct jv {
    jv_kind kind;
    bool b;
    int64_t i;
    double num;
    /* string */
    const char *str;
    size_t str_len;
    /* object (order preserved) */
    struct jv_member {
        const char *key;
        jv *val;
    } *members;
    size_t n_members;
    /* array */
    jv **items;
    size_t n_items;
};

jv_kind jv_kind_of(const jv *v) { return v->kind; }
bool jv_bool(const jv *v) { return v->b; }
int64_t jv_int(const jv *v) { return v->i; }
double jv_num(const jv *v) { return v->kind == JV_INT ? (double)v->i : v->num; }
const char *jv_str(const jv *v) { return v->str; }
size_t jv_str_len(const jv *v) { return v->str_len; }

size_t jv_member_count(const jv *v) { return v->n_members; }
const char *jv_member_key(const jv *v, size_t i) { return v->members[i].key; }
jv *jv_member_val(const jv *v, size_t i) { return v->members[i].val; }

/* last duplicate key wins (the common practice) */
jv *jv_member_get(const jv *v, const char *key) {
    jv *found = NULL;
    for (size_t i = 0; i < v->n_members; i++) {
        if (strcmp(v->members[i].key, key) == 0) {
            found = v->members[i].val;
        }
    }
    return found;
}

size_t jv_item_count(const jv *v) { return v->n_items; }
jv *jv_item(const jv *v, size_t i) { return v->items[i]; }

typedef struct {
    teptris_arena *a;
    const char *src;
    size_t len;
    size_t pos;
    char *err;
    size_t err_cap;
    int depth;
} jctx;

static void jfail(jctx *c, const char *msg) {
    if (c->err != NULL && c->err[0] == '\0') {
        snprintf(c->err, c->err_cap, "%s at byte %zu", msg, c->pos);
    }
}

static void jskip_ws(jctx *c) {
    while (c->pos < c->len) {
        char ch = c->src[c->pos];
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
            c->pos++;
        } else {
            break;
        }
    }
}

static int jhex(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

/* decode one \uXXXX (or a surrogate pair) to a code point; -1 invalid */
static long jescape_u(jctx *c) {
    /* c->pos sits after the backslash-u's first digit? Contract: after 'u' */
    if (c->pos + 4 > c->len) {
        jfail(c, "incomplete \\u escape");
        return -1;
    }
    long cp = 0;
    for (int k = 0; k < 4; k++) {
        int d = jhex(c->src[c->pos + (size_t)k]);
        if (d < 0) {
            jfail(c, "invalid \\u escape");
            return -1;
        }
        cp = cp * 16 + d;
    }
    c->pos += 4;
    if (cp >= 0xD800 && cp <= 0xDBFF && c->pos + 6 <= c->len &&
        c->src[c->pos] == '\\' && c->src[c->pos + 1] == 'u') {
        size_t save = c->pos;
        c->pos += 2;
        long lo = 0;
        for (int k = 0; k < 4; k++) {
            int d = jhex(c->src[c->pos + (size_t)k]);
            if (d < 0) {
                c->pos = save;
                return cp; /* lone high surrogate: let the caller reject */
            }
            lo = lo * 16 + d;
        }
        if (lo >= 0xDC00 && lo <= 0xDFFF) {
            c->pos += 4;
            return 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
        }
        c->pos = save;
    }
    return cp;
}

static size_t jput_utf8(char *out, long cp) {
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

/* parse a string; c->pos sits on the opening quote. The result is
 * always arena-owned and NUL-terminated (members are looked up with
 * strcmp); str_len carries the exact length. */
static const char *jstring(jctx *c, size_t *out_len) {
    c->pos++; /* opening quote */
    /* fast path: no escapes, copy the raw span */
    size_t start = c->pos;
    bool plain = true;
    while (c->pos < c->len) {
        unsigned char ch = (unsigned char)c->src[c->pos];
        if (ch == '"') {
            break;
        }
        if (ch == '\\' || ch < 0x20) {
            plain = false;
            break;
        }
        c->pos++;
    }
    if (c->pos >= c->len) {
        jfail(c, "unterminated string");
        return NULL;
    }
    if (plain) {
        size_t slen = c->pos - start;
        char *s = teptris_arena_alloc(c->a, slen + 1);
        memcpy(s, c->src + start, slen);
        s[slen] = '\0';
        *out_len = slen;
        c->pos++; /* closing quote */
        return s;
    }
    /* escaped string: rebuild into arena memory */
    size_t cap = (c->pos - start) * 4 + 16;
    char *out = teptris_arena_alloc(c->a, cap);
    size_t n = 0;
    for (;;) {
        if (c->pos >= c->len) {
            jfail(c, "unterminated string");
            return NULL;
        }
        unsigned char ch = (unsigned char)c->src[c->pos];
        if (ch == '"') {
            c->pos++;
            break;
        }
        if (ch < 0x20) {
            jfail(c, "control character in string");
            return NULL;
        }
        if (ch != '\\') {
            out[n++] = (char)ch;
            c->pos++;
            continue;
        }
        c->pos++;
        if (c->pos >= c->len) {
            jfail(c, "unterminated escape");
            return NULL;
        }
        char e = c->src[c->pos];
        switch (e) {
        case '"': out[n++] = '"'; c->pos++; break;
        case '\\': out[n++] = '\\'; c->pos++; break;
        case '/': out[n++] = '/'; c->pos++; break;
        case 'b': out[n++] = '\b'; c->pos++; break;
        case 'f': out[n++] = '\f'; c->pos++; break;
        case 'n': out[n++] = '\n'; c->pos++; break;
        case 'r': out[n++] = '\r'; c->pos++; break;
        case 't': out[n++] = '\t'; c->pos++; break;
        case 'u': {
            c->pos++;
            long cp = jescape_u(c);
            if (cp < 0 || (cp >= 0xD800 && cp <= 0xDFFF)) {
                if (cp >= 0) jfail(c, "lone surrogate in \\u escape");
                return NULL;
            }
            n += jput_utf8(out + n, cp);
            break;
        }
        default:
            jfail(c, "invalid escape");
            return NULL;
        }
    }
    *out_len = n;
    out[n] = '\0';
    return out;
}

static jv *jvalue(jctx *c);

static jv *jobject(jctx *c) {
    c->pos++; /* '{' */
    jv *v = teptris_arena_alloc(c->a, sizeof(jv));
    memset(v, 0, sizeof(jv));
    v->kind = JV_OBJECT;
    size_t cap = 8;
    v->members = teptris_arena_alloc(c->a, cap * sizeof(*v->members));
    jskip_ws(c);
    if (c->pos < c->len && c->src[c->pos] == '}') {
        c->pos++;
        return v;
    }
    for (;;) {
        jskip_ws(c);
        if (c->pos >= c->len || c->src[c->pos] != '"') {
            jfail(c, "expected string key");
            return NULL;
        }
        size_t klen = 0;
        const char *key = jstring(c, &klen);
        if (key == NULL) return NULL;
        jskip_ws(c);
        if (c->pos >= c->len || c->src[c->pos] != ':') {
            jfail(c, "expected ':'");
            return NULL;
        }
        c->pos++;
        jv *val = jvalue(c);
        if (val == NULL) return NULL;
        if (v->n_members == cap) {
            cap *= 2;
            void *nm = teptris_arena_alloc(c->a, cap * sizeof(*v->members));
            memcpy(nm, v->members, v->n_members * sizeof(*v->members));
            v->members = nm;
        }
        v->members[v->n_members].key = key;
        v->members[v->n_members].val = val;
        v->n_members++;
        jskip_ws(c);
        if (c->pos < c->len && c->src[c->pos] == ',') {
            c->pos++;
            continue;
        }
        if (c->pos < c->len && c->src[c->pos] == '}') {
            c->pos++;
            return v;
        }
        jfail(c, "expected ',' or '}'");
        return NULL;
    }
}

static jv *jarray(jctx *c) {
    c->pos++; /* '[' */
    jv *v = teptris_arena_alloc(c->a, sizeof(jv));
    memset(v, 0, sizeof(jv));
    v->kind = JV_ARRAY;
    size_t cap = 8;
    v->items = teptris_arena_alloc(c->a, cap * sizeof(*v->items));
    jskip_ws(c);
    if (c->pos < c->len && c->src[c->pos] == ']') {
        c->pos++;
        return v;
    }
    for (;;) {
        jv *val = jvalue(c);
        if (val == NULL) return NULL;
        if (v->n_items == cap) {
            cap *= 2;
            void *ni = teptris_arena_alloc(c->a, cap * sizeof(*v->items));
            memcpy(ni, v->items, v->n_items * sizeof(*v->items));
            v->items = ni;
        }
        v->items[v->n_items++] = val;
        jskip_ws(c);
        if (c->pos < c->len && c->src[c->pos] == ',') {
            c->pos++;
            continue;
        }
        if (c->pos < c->len && c->src[c->pos] == ']') {
            c->pos++;
            return v;
        }
        jfail(c, "expected ',' or ']'");
        return NULL;
    }
}

static jv *jvalue(jctx *c) {
    if (++c->depth > JSON_MAX_DEPTH) {
        jfail(c, "schema nesting too deep");
        c->depth--;
        return NULL;
    }
    jskip_ws(c);
    if (c->pos >= c->len) {
        jfail(c, "unexpected end of schema");
        c->depth--;
        return NULL;
    }
    char ch = c->src[c->pos];
    jv *v = NULL;
    if (ch == '{') {
        v = jobject(c);
    } else if (ch == '[') {
        v = jarray(c);
    } else if (ch == '"') {
        size_t slen = 0;
        const char *s = jstring(c, &slen);
        if (s != NULL) {
            v = teptris_arena_alloc(c->a, sizeof(jv));
            memset(v, 0, sizeof(jv));
            v->kind = JV_STRING;
            v->str = s;
            v->str_len = slen;
        }
    } else if (c->len - c->pos >= 4 &&
               strncmp(c->src + c->pos, "true", 4) == 0) {
        v = teptris_arena_alloc(c->a, sizeof(jv));
        memset(v, 0, sizeof(jv));
        v->kind = JV_BOOL;
        v->b = true;
        c->pos += 4;
    } else if (c->len - c->pos >= 5 &&
               strncmp(c->src + c->pos, "false", 5) == 0) {
        v = teptris_arena_alloc(c->a, sizeof(jv));
        memset(v, 0, sizeof(jv));
        v->kind = JV_BOOL;
        c->pos += 5;
    } else if (c->len - c->pos >= 4 &&
               strncmp(c->src + c->pos, "null", 4) == 0) {
        v = teptris_arena_alloc(c->a, sizeof(jv));
        memset(v, 0, sizeof(jv));
        v->kind = JV_NULL;
        c->pos += 4;
    } else if (ch == '-' || (ch >= '0' && ch <= '9')) {
        size_t start = c->pos;
        if (ch == '-') c->pos++;
        bool integral = true;
        while (c->pos < c->len) {
            char d = c->src[c->pos];
            if (d >= '0' && d <= '9') {
                c->pos++;
            } else if (d == '.' || d == 'e' || d == 'E' || d == '+' || d == '-') {
                integral = false;
                c->pos++;
            } else {
                break;
            }
        }
        char tmp[64];
        size_t tlen = c->pos - start;
        if (tlen == 0 || tlen >= sizeof tmp) {
            jfail(c, "invalid number");
            c->depth--;
            return NULL;
        }
        memcpy(tmp, c->src + start, tlen);
        tmp[tlen] = '\0';
        v = teptris_arena_alloc(c->a, sizeof(jv));
        memset(v, 0, sizeof(jv));
        v->num = strtod(tmp, NULL);
        if (integral) {
            errno = 0;
            char *end = NULL;
            long long i = strtoll(tmp, &end, 10);
            if (errno == 0 && end != NULL && *end == '\0') {
                v->kind = JV_INT;
                v->i = (int64_t)i;
            } else {
                v->kind = JV_NUM;
            }
        } else {
            v->kind = JV_NUM;
        }
    } else {
        jfail(c, "unexpected character");
    }
    c->depth--;
    return v;
}

jv *json_read(teptris_arena *a, const char *src, size_t len, char *err,
              size_t err_cap) {
    jctx c;
    memset(&c, 0, sizeof(c));
    c.a = a;
    c.src = src;
    c.len = len;
    c.err = err;
    c.err_cap = err_cap;
    if (err != NULL && err_cap > 0) err[0] = '\0';
    jv *v = jvalue(&c);
    if (v == NULL) return NULL;
    jskip_ws(&c);
    if (c.pos != c.len) {
        if (err != NULL && err_cap > 0) {
            snprintf(err, err_cap, "trailing content at byte %zu", c.pos);
        }
        return NULL;
    }
    return v;
}
