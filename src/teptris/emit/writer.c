/* Streaming TOML writer: an op-stream in, TOML text out.
 *
 * Fuses the builder's DOM construction and the emitter's tree walk
 * into one pass: bindings that walk host objects (Python dicts, JS
 * objects) encode an op-stream instead of calling builder_put_* per
 * value and re-emitting from the DOM. Sections emit as
 * [dotted.headers] / [[aot.headers]] with full paths; arrays and
 * inline tables render inline.
 *
 * Op grammar (little-endian, mirroring the flatten payload styles):
 *   0x00 END-OF-CHUNK
 *   0x01 KEY       u8 klen <bytes>        (klen 0 clears the pending key)
 *   0x02 STRING    u32 len <bytes>
 *   0x03 INT       i64
 *   0x04 FLOAT     f64
 *   0x05 FALSE
 *   0x06 TRUE
 *   0x07 DATETIME  u8 kind(4..7) <25 B, same layout as flatten>
 *   0x08 BEGIN-TABLE        section header ([k] / [a.b])
 *   0x09 BEGIN-ARRAY        inline array as a value
 *   0x0A BEGIN-AOT          array-of-tables ([[k]], elements via BEGIN-TABLE)
 *   0x0B BEGIN-INLINE-TABLE {inline table: array element or keyed value}
 *   0x0C CLOSE
 *   0x0D EMPTY-TABLE        (value {})
 *   0x0E EMPTY-ARRAY        (value [])
 *
 * Caller contract: within each table, feed every scalar BEFORE any
 * nested table/AOT - TOML forbids keys after a sub-table header, and
 * the writer enforces that with ERR_STATE instead of emitting invalid
 * TOML. */
#include <stdlib.h>
#include <string.h>

#include "emit_shared.h"
#include "teptris/teptris.h"

enum {
    W_END = 0x00,
    W_KEY = 0x01,
    W_STRING = 0x02,
    W_INT = 0x03,
    W_FLOAT = 0x04,
    W_FALSE = 0x05,
    W_TRUE = 0x06,
    W_DATETIME = 0x07,
    W_BEGIN_TABLE = 0x08,
    W_BEGIN_ARRAY = 0x09,
    W_BEGIN_AOT = 0x0A,
    W_BEGIN_INLINE_TABLE = 0x0B,
    W_CLOSE = 0x0C,
    W_EMPTY_TABLE = 0x0D,
    W_EMPTY_ARRAY = 0x0E
};

enum {
    F_TABLE,  /* section table (root or [dotted]) or an AOT element */
    F_ARRAY,  /* inline array */
    F_AOT,    /* the AOT itself: contributes its key to element paths */
    F_ITAB    /* inline table */
};

typedef struct wframe {
    struct wframe *down;
    int kind;
    char *key;         /* TBL section component; AOT key; else NULL */
    size_t key_len;
    int first;         /* inline separator state */
    int saw_subtable;  /* TBL: a nested section was opened under us */
} wframe;

struct teptris_writer {
    ebuf out;
    char *key; /* pending key */
    size_t key_len;
    wframe *top;
    int finished;
    teptris_status st;
};

static wframe *w_push(teptris_writer *w, int kind, const char *k,
                      size_t klen);

teptris_writer *teptris_writer_new(void)
{
    teptris_writer *w = calloc(1, sizeof(*w));
    if (w != NULL) {
        /* implicit root table: tracks root-level value/sub-table
         * ordering; never closed by CLOSE */
        w_push(w, F_TABLE, NULL, 0);
        w->st = TEPTRIS_OK;
    }
    return w;
}

void teptris_writer_free(teptris_writer *w)
{
    if (w == NULL) {
        return;
    }
    while (w->top != NULL) {
        wframe *f = w->top;
        w->top = f->down;
        free(f->key);
        free(f);
    }
    free(w->key);
    free(w->out.p);
    free(w);
}

static void w_take_key(teptris_writer *w, const char *k, size_t klen)
{
    free(w->key);
    w->key = NULL;
    w->key_len = 0;
    if (klen > 0) {
        w->key = malloc(klen);
        if (w->key == NULL) {
            w->st = TEPTRIS_ERR_ALLOC;
            return;
        }
        memcpy(w->key, k, klen);
        w->key_len = klen;
    }
}

static void w_clear_key(teptris_writer *w)
{
    free(w->key);
    w->key = NULL;
    w->key_len = 0;
}

/* push with an OWNED key (transfers the malloc from the caller) */
static wframe *w_push_steal(teptris_writer *w, int kind, char *k,
                            size_t klen)
{
    wframe *f = calloc(1, sizeof(*f));
    if (f == NULL) {
        free(k);
        w->st = TEPTRIS_ERR_ALLOC;
        return NULL;
    }
    f->down = w->top;
    f->kind = kind;
    f->key = k;
    f->key_len = klen;
    w->top = f;
    return f;
}

static wframe *w_push(teptris_writer *w, int kind, const char *k,
                      size_t klen)
{
    wframe *f = calloc(1, sizeof(*f));
    if (f == NULL) {
        w->st = TEPTRIS_ERR_ALLOC;
        return NULL;
    }
    f->down = w->top;
    f->kind = kind;
    if (klen > 0) {
        f->key = malloc(klen);
        if (f->key == NULL) {
            free(f);
            w->st = TEPTRIS_ERR_ALLOC;
            return NULL;
        }
        memcpy(f->key, k, klen);
        f->key_len = klen;
    }
    w->top = f;
    return f;
}

/* dotted path of all keyed frames down the stack, outermost first;
 * with_tail appends the pending key as the last component */
static void w_path(teptris_writer *w, int with_tail)
{
    wframe *stack[600];
    int n = 0;
    for (wframe *f = w->top; f != NULL && n < 600; f = f->down) {
        stack[n++] = f;
    }
    int first = 1;
    for (int i = n - 1; i >= 0; i--) {
        wframe *f = stack[i];
        if ((f->kind == F_TABLE || f->kind == F_AOT) && f->key != NULL) {
            if (!first) {
                eb_c(&w->out, '.');
            }
            emit_key(&w->out, (teptris_view){f->key, f->key_len});
            first = 0;
        }
    }
    if (with_tail && w->key != NULL) {
        if (!first) {
            eb_c(&w->out, '.');
        }
        emit_key(&w->out, (teptris_view){w->key, w->key_len});
    }
}

/* section headers follow the previous line directly (emitter style) */
static void w_sep(teptris_writer *w)
{
    (void)w;
}

static void w_sep_val(teptris_writer *w, wframe *f)
{
    if (f->first) {
        eb_put(&w->out, ", ", 2);
    }
    f->first = 1;
}

/* emit "key = " in a section/inline-table context; arrays take bare
 * values with separators */
static void w_keyed_prefix(teptris_writer *w)
{
    wframe *f = w->top;
    if (f != NULL && f->kind == F_ARRAY) {
        w_sep_val(w, f);
        return;
    }
    if (w->key == NULL) {
        w->st = TEPTRIS_ERR_ARG;
        return;
    }
    if (f != NULL && f->saw_subtable) {
        w->st = TEPTRIS_ERR_STATE; /* value after a nested table header */
        return;
    }
    emit_key(&w->out, (teptris_view){w->key, w->key_len});
    eb_put(&w->out, " = ", 3);
    w_clear_key(w);
}

static void w_end_line(teptris_writer *w)
{
    wframe *f = w->top;
    if (f == NULL || f->kind == F_TABLE) {
        eb_c(&w->out, '\n');
    }
    /* inside inline arrays/tables the separator or close comes next */
}

teptris_status teptris_writer_feed(teptris_writer *w, const uint8_t *ops,
                                   size_t len)
{
    if (w == NULL || (ops == NULL && len > 0)) {
        return TEPTRIS_ERR_ARG;
    }
    if (w->finished) {
        return TEPTRIS_ERR_STATE;
    }
    size_t i = 0;
    while (i < len && w->st == TEPTRIS_OK) {
        uint8_t op = ops[i++];
        switch (op) {
        case W_END:
            return w->st;
        case W_KEY: {
            if (i >= len) {
                w->st = TEPTRIS_ERR_ARG;
                break;
            }
            uint8_t klen = ops[i++];
            if (i + klen > len) {
                w->st = TEPTRIS_ERR_ARG;
                break;
            }
            w_take_key(w, (const char *)ops + i, klen);
            i += klen;
            break;
        }
        case W_STRING: {
            if (i + 4 > len) {
                w->st = TEPTRIS_ERR_ARG;
                break;
            }
            uint32_t vlen = (uint32_t)ops[i] |
                            ((uint32_t)ops[i + 1] << 8) |
                            ((uint32_t)ops[i + 2] << 16) |
                            ((uint32_t)ops[i + 3] << 24);
            i += 4;
            if (i + vlen > len) {
                w->st = TEPTRIS_ERR_ARG;
                break;
            }
            w_keyed_prefix(w);
            if (w->st != TEPTRIS_OK) {
                break;
            }
            emit_string_value(&w->out,
                              (teptris_view){(const char *)ops + i, vlen});
            i += vlen;
            w_end_line(w);
            break;
        }
        case W_INT: {
            if (i + 8 > len) {
                w->st = TEPTRIS_ERR_ARG;
                break;
            }
            uint64_t u = 0;
            for (int b = 0; b < 8; b++) {
                u |= (uint64_t)ops[i + b] << (8 * b); /* little-endian */
            }
            int64_t v;
            memcpy(&v, &u, 8);
            i += 8;
            w_keyed_prefix(w);
            if (w->st != TEPTRIS_OK) {
                break;
            }
            emit_i64(&w->out, v);
            w_end_line(w);
            break;
        }
        case W_FLOAT: {
            if (i + 8 > len) {
                w->st = TEPTRIS_ERR_ARG;
                break;
            }
            uint64_t u = 0;
            for (int b = 0; b < 8; b++) {
                u |= (uint64_t)ops[i + b] << (8 * b); /* little-endian */
            }
            double d;
            memcpy(&d, &u, 8);
            i += 8;
            w_keyed_prefix(w);
            if (w->st != TEPTRIS_OK) {
                break;
            }
            emit_float(&w->out, d);
            w_end_line(w);
            break;
        }
        case W_FALSE:
        case W_TRUE: {
            w_keyed_prefix(w);
            if (w->st != TEPTRIS_OK) {
                break;
            }
            if (op == W_TRUE) {
                eb_put(&w->out, "true", 4);
            } else {
                eb_put(&w->out, "false", 5);
            }
            w_end_line(w);
            break;
        }
        case W_DATETIME: {
            if (i + 26 > len) { /* kind + i64 year + 5 + u32 ns + i64 off */
                w->st = TEPTRIS_ERR_ARG;
                break;
            }
            uint8_t kb = ops[i];
            if (kb < 4 || kb > 7) {
                w->st = TEPTRIS_ERR_ARG;
                break;
            }
            teptris_datetime dt;
            uint64_t yr = 0, off = 0, ns = 0;
            for (int b = 0; b < 8; b++) {
                yr |= (uint64_t)ops[i + 1 + b] << (8 * b); /* LE */
                off |= (uint64_t)ops[i + 18 + b] << (8 * b);
            }
            for (int b = 0; b < 4; b++) {
                ns |= (uint64_t)ops[i + 14 + b] << (8 * b);
            }
            dt.year = (int32_t)yr;
            dt.month = ops[i + 9];
            dt.day = ops[i + 10];
            dt.hour = ops[i + 11];
            dt.minute = ops[i + 12];
            dt.second = ops[i + 13];
            memcpy(&dt.nanosecond, &ns, 4);
            dt.offset_seconds = (int32_t)off;
            i += 26;
            w_keyed_prefix(w);
            if (w->st != TEPTRIS_OK) {
                break;
            }
            emit_dt(&w->out, &dt, (teptris_kind)kb);
            w_end_line(w);
            break;
            if (w->st != TEPTRIS_OK) {
                break;
            }
            emit_dt(&w->out, &dt, (teptris_kind)kb);
            w_end_line(w);
            break;
        }
        case W_BEGIN_TABLE: {
            wframe *t = w->top;
            if (t != NULL && (t->kind == F_ARRAY || t->kind == F_ITAB)) {
                /* sections cannot live inside inline containers; use
                 * BEGIN_INLINE_TABLE there */
                w->st = TEPTRIS_ERR_ARG;
                break;
            }
            if (t != NULL && t->kind == F_AOT) {
                /* element: [[path]] header, keyless frame */
                w_sep(w);
                eb_put(&w->out, "[[", 2);
                w_path(w, 0);
                eb_put(&w->out, "]]\n", 3);
                w_clear_key(w);
                w_push(w, F_TABLE, NULL, 0);
                break;
            }
            if (w->key == NULL) {
                w->st = TEPTRIS_ERR_ARG;
                break;
            }
            if (t != NULL) {
                t->saw_subtable = 1;
            }
            w_sep(w);
            eb_c(&w->out, '[');
            w_path(w, 1);
            eb_put(&w->out, "]\n", 2);
            char *k = w->key; /* frame takes ownership */
            size_t klen = w->key_len;
            w->key = NULL;
            w->key_len = 0;
            w_push_steal(w, F_TABLE, k, klen);
            break;
        }
        case W_BEGIN_AOT: {
            if (w->key == NULL) {
                w->st = TEPTRIS_ERR_ARG;
                break;
            }
            wframe *t = w->top;
            if (t != NULL && t->kind != F_TABLE) {
                w->st = TEPTRIS_ERR_ARG;
                break;
            }
            if (t != NULL) {
                t->saw_subtable = 1;
            }
            char *k = w->key; /* frame takes ownership */
            size_t klen = w->key_len;
            w->key = NULL;
            w->key_len = 0;
            w_push_steal(w, F_AOT, k, klen);
            break;
        }
        case W_BEGIN_ARRAY: {
            wframe *t = w->top;
            if (t != NULL && t->kind == F_AOT) {
                w->st = TEPTRIS_ERR_ARG;
                break;
            }
            if (t == NULL || t->kind == F_TABLE || t->kind == F_ITAB) {
                if (w->key == NULL) {
                    w->st = TEPTRIS_ERR_ARG;
                    break;
                }
                if (t != NULL && t->saw_subtable) {
                    w->st = TEPTRIS_ERR_STATE;
                    break;
                }
                emit_key(&w->out, (teptris_view){w->key, w->key_len});
                eb_put(&w->out, " = [", 4);
                w_clear_key(w);
            } else { /* F_ARRAY: nested inline array */
                w_sep_val(w, t);
                eb_c(&w->out, '[');
            }
            w_push(w, F_ARRAY, NULL, 0);
            break;
        }
        case W_BEGIN_INLINE_TABLE: {
            wframe *t = w->top;
            if (t != NULL && t->kind == F_ARRAY) {
                if (w->key != NULL) {
                    w->st = TEPTRIS_ERR_ARG;
                    break;
                }
                w_sep_val(w, t);
            } else {
                if (w->key == NULL) {
                    w->st = TEPTRIS_ERR_ARG;
                    break;
                }
                if (t != NULL && t->saw_subtable) {
                    w->st = TEPTRIS_ERR_STATE;
                    break;
                }
                emit_key(&w->out, (teptris_view){w->key, w->key_len});
                eb_put(&w->out, " = ", 3);
                w_clear_key(w);
            }
            eb_c(&w->out, '{');
            w_push(w, F_ITAB, NULL, 0);
            break;
        }
        case W_CLOSE: {
            wframe *f = w->top;
            if (f == NULL || f->down == NULL) {
                w->st = TEPTRIS_ERR_ARG; /* nothing (or only the root) open */
                break;
            }
            int kind = f->kind;
            w->top = f->down;
            free(f->key);
            free(f);
            if (kind == F_ARRAY) {
                eb_c(&w->out, ']');
                w_end_line(w);
            } else if (kind == F_ITAB) {
                eb_c(&w->out, '}');
                w_end_line(w);
            }
            break;
        }
        case W_EMPTY_TABLE:
        case W_EMPTY_ARRAY: {
            w_keyed_prefix(w);
            if (w->st != TEPTRIS_OK) {
                break;
            }
            if (op == W_EMPTY_TABLE) {
                eb_put(&w->out, "{}", 2);
            } else {
                eb_put(&w->out, "[]", 2);
            }
            w_end_line(w);
            break;
        }
        default:
            w->st = TEPTRIS_ERR_ARG;
            break;
        }
    }
    return w->st;
}

teptris_status teptris_writer_finish(teptris_writer *w, char **buf,
                                     size_t *len)
{
    if (w == NULL || buf == NULL || len == NULL) {
        return TEPTRIS_ERR_ARG;
    }
    *buf = NULL;
    *len = 0;
    if (w->top == NULL || w->top->down != NULL) {
        return TEPTRIS_ERR_STATE; /* unclosed containers */
    }
    if (w->st != TEPTRIS_OK) {
        return w->st;
    }
    w->finished = 1;
    if (w->out.p == NULL) {
        char *empty = malloc(1);
        if (empty == NULL) {
            return TEPTRIS_ERR_ALLOC;
        }
        empty[0] = '\0';
        *buf = empty;
        *len = 0;
        return TEPTRIS_OK;
    }
    w->out.p[w->out.len] = '\0';
    *buf = w->out.p;
    *len = w->out.len;
    w->out.p = NULL; /* ownership transferred */
    return TEPTRIS_OK;
}
