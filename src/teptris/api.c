#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "teptris/dom/dom.h"
#include "teptris/emit/emitter.h"
#include "teptris/parse/parse.h"
#include "teptris/teptris.h"

teptris_status teptris_parse(const char *data, size_t len,
                             const teptris_options *opts,
                             teptris_document **out)
{
    *out = NULL;
    teptris_document *doc = calloc(1, sizeof(teptris_document));
    if (doc == NULL) {
        return TEPTRIS_ERR_ALLOC;
    }
    teptris_arena_init(&doc->arena);
    doc->max_depth = (opts != NULL && opts->max_depth != 0) ? opts->max_depth
                                                           : 512;
    doc->err.status = TEPTRIS_OK;
    doc->err.message = "";

    teptris_status st = teptris_parser_run(doc, data, len);
    if (st != TEPTRIS_OK && doc->err.status == TEPTRIS_OK) {
        doc->err.status = st;
        if (st == TEPTRIS_ERR_ALLOC) {
            snprintf(doc->err_msg, sizeof(doc->err_msg), "out of memory");
        }
        doc->err.message = doc->err_msg;
    }
    *out = doc;
    return st;
}

void teptris_document_free(teptris_document *doc)
{
    if (doc == NULL) {
        return;
    }
    teptris_arena_destroy(&doc->arena);
    free(doc);
}

const teptris_error *teptris_document_error(const teptris_document *doc)
{
    return &doc->err;
}

const teptris_node *teptris_document_root(const teptris_document *doc)
{
    if (doc == NULL || doc->err.status != TEPTRIS_OK) {
        return NULL;
    }
    return doc->root;
}

teptris_kind teptris_node_kind(const teptris_node *node)
{
    return (node != NULL) ? (teptris_kind)node->kind : TEPTRIS_STRING;
}

teptris_status teptris_node_string(const teptris_node *node, teptris_view *out)
{
    if (node == NULL || node->kind != TEPTRIS_STRING) {
        return TEPTRIS_ERR_ARG;
    }
    *out = node->as.str;
    return TEPTRIS_OK;
}

teptris_status teptris_node_integer(const teptris_node *node, int64_t *out)
{
    if (node == NULL || node->kind != TEPTRIS_INTEGER) {
        return TEPTRIS_ERR_ARG;
    }
    *out = node->as.i;
    return TEPTRIS_OK;
}

teptris_status teptris_node_float(const teptris_node *node, double *out)
{
    if (node == NULL || node->kind != TEPTRIS_FLOAT) {
        return TEPTRIS_ERR_ARG;
    }
    *out = node->as.f;
    return TEPTRIS_OK;
}

teptris_status teptris_node_boolean(const teptris_node *node, bool *out)
{
    if (node == NULL || node->kind != TEPTRIS_BOOLEAN) {
        return TEPTRIS_ERR_ARG;
    }
    *out = node->as.b;
    return TEPTRIS_OK;
}

teptris_status teptris_node_datetime(const teptris_node *node,
                                     teptris_datetime *out)
{
    if (node == NULL || node->kind < TEPTRIS_DATETIME_OFFSET ||
        node->kind > TEPTRIS_TIME_LOCAL) {
        return TEPTRIS_ERR_ARG;
    }
    *out = node->as.dt;
    return TEPTRIS_OK;
}

size_t teptris_node_array_length(const teptris_node *node)
{
    return (node != NULL && node->kind == TEPTRIS_ARRAY) ? node->as.array.len
                                                         : 0;
}

const teptris_node *teptris_node_array_at(const teptris_node *node,
                                          size_t index)
{
    if (node == NULL || node->kind != TEPTRIS_ARRAY ||
        index >= node->as.array.len) {
        return NULL;
    }
    return node->as.array.items[index];
}

size_t teptris_node_table_length(const teptris_node *node)
{
    return (node != NULL && node->kind == TEPTRIS_TABLE) ? node->as.table.len
                                                         : 0;
}

const teptris_node *teptris_node_table_at(const teptris_node *node,
                                          size_t index, teptris_view *key_out)
{
    if (node == NULL || node->kind != TEPTRIS_TABLE ||
        index >= node->as.table.len) {
        return NULL;
    }
    if (key_out != NULL) {
        *key_out = node->as.table.entries[index].key;
    }
    return node->as.table.entries[index].value;
}

const teptris_node *teptris_node_table_get(const teptris_node *node,
                                           const char *key, size_t key_len)
{
    if (node == NULL || node->kind != TEPTRIS_TABLE || key == NULL) {
        return NULL;
    }
    teptris_entry *e = teptris_dom_table_find(node, key, key_len);
    return (e != NULL) ? e->value : NULL;
}

teptris_status teptris_document_emit(const teptris_document *doc, char **buf,
                                     size_t *len)
{
    return teptris_emit_document(doc, buf, len);
}

teptris_status teptris_document_emit_json(const teptris_document *doc,
                                          char **buf, size_t *len)
{
    return teptris_emit_document_json(doc, buf, len);
}

/* --------------------------------------------------- bulk flatten ----- */

typedef struct {
    uint8_t *p;
    size_t len, cap;
    teptris_status st;
} flatbuf;

static void fb_grow(flatbuf *b, size_t extra)
{
    if (b->st != TEPTRIS_OK) {
        return;
    }
    if (b->len + extra <= b->cap) {
        return;
    }
    size_t ncap = b->cap ? b->cap : 512;
    while (b->len + extra > ncap) {
        ncap *= 2;
    }
    uint8_t *np = realloc(b->p, ncap);
    if (np == NULL) {
        free(b->p);
        b->p = NULL;
        b->st = TEPTRIS_ERR_ALLOC;
        return;
    }
    b->p = np;
    b->cap = ncap;
}

static void fb_byte(flatbuf *b, uint8_t v)
{
    fb_grow(b, 1);
    if (b->st == TEPTRIS_OK) {
        b->p[b->len++] = v;
    }
}

static void fb_u32(flatbuf *b, uint32_t v)
{
    fb_grow(b, 4);
    if (b->st == TEPTRIS_OK) {
        b->p[b->len++] = (uint8_t)v;
        b->p[b->len++] = (uint8_t)(v >> 8);
        b->p[b->len++] = (uint8_t)(v >> 16);
        b->p[b->len++] = (uint8_t)(v >> 24);
    }
}

static void fb_u64(flatbuf *b, uint64_t v)
{
    for (int i = 0; i < 8; i++) {
        fb_byte(b, (uint8_t)(v >> (8 * i)));
    }
}

static void fb_i64(flatbuf *b, int64_t v)
{
    uint64_t u;
    memcpy(&u, &v, 8);
    fb_u64(b, u);
}

static void fb_f64(flatbuf *b, double v)
{
    uint64_t u;
    memcpy(&u, &v, 8);
    fb_u64(b, u);
}

static void fb_dt(flatbuf *b, const teptris_datetime *d)
{
    fb_i64(b, d->year);
    fb_byte(b, d->month);
    fb_byte(b, d->day);
    fb_byte(b, d->hour);
    fb_byte(b, d->minute);
    fb_byte(b, d->second);
    fb_u32(b, d->nanosecond);
    fb_i64(b, d->offset_seconds);
}

static teptris_status flatten_node(flatbuf *b, const teptris_node *n)
{
    switch (n->kind) {
    case TEPTRIS_TABLE:
        fb_byte(b, 0x01);
        fb_u32(b, (uint32_t)n->as.table.len);
        for (size_t i = 0; i < n->as.table.len; i++) {
            fb_u32(b, (uint32_t)n->as.table.entries[i].key.len);
            fb_grow(b, n->as.table.entries[i].key.len);
            if (b->st != TEPTRIS_OK) {
                return b->st;
            }
            memcpy(b->p + b->len, n->as.table.entries[i].key.ptr,
                   n->as.table.entries[i].key.len);
            b->len += n->as.table.entries[i].key.len;
            teptris_status st = flatten_node(b, n->as.table.entries[i].value);
            if (st != TEPTRIS_OK) {
                return st;
            }
        }
        return TEPTRIS_OK;
    case TEPTRIS_ARRAY:
        fb_byte(b, 0x02);
        fb_u32(b, (uint32_t)n->as.array.len);
        for (size_t i = 0; i < n->as.array.len; i++) {
            teptris_status st = flatten_node(b, n->as.array.items[i]);
            if (st != TEPTRIS_OK) {
                return st;
            }
        }
        return TEPTRIS_OK;
    case TEPTRIS_STRING:
        fb_byte(b, 0x03);
        fb_u32(b, (uint32_t)n->as.str.len);
        fb_grow(b, n->as.str.len);
        if (b->st != TEPTRIS_OK) {
            return b->st;
        }
        memcpy(b->p + b->len, n->as.str.ptr, n->as.str.len);
        b->len += n->as.str.len;
        return TEPTRIS_OK;
    case TEPTRIS_INTEGER:
        fb_byte(b, 0x04);
        fb_i64(b, n->as.i);
        return TEPTRIS_OK;
    case TEPTRIS_FLOAT:
        fb_byte(b, 0x05);
        fb_f64(b, n->as.f);
        return TEPTRIS_OK;
    case TEPTRIS_BOOLEAN:
        fb_byte(b, n->as.b ? 0x07 : 0x06);
        return TEPTRIS_OK;
    case TEPTRIS_DATETIME_OFFSET:
    case TEPTRIS_DATETIME_LOCAL:
    case TEPTRIS_DATE_LOCAL:
    case TEPTRIS_TIME_LOCAL:
        fb_byte(b, (uint8_t)(0x08 + (n->kind - TEPTRIS_DATETIME_OFFSET)));
        fb_dt(b, &n->as.dt);
        return TEPTRIS_OK;
    default:
        return TEPTRIS_ERR_STATE;
    }
}

teptris_status teptris_document_flatten(const teptris_document *doc,
                                        uint8_t **buf, size_t *len)
{
    if (doc == NULL || doc->root == NULL || buf == NULL || len == NULL) {
        return TEPTRIS_ERR_ARG;
    }
    *buf = NULL;
    *len = 0;
    flatbuf b = {NULL, 0, 0, TEPTRIS_OK};
    fb_grow(&b, 512);
    teptris_status st = flatten_node(&b, doc->root);
    if (st != TEPTRIS_OK) {
        free(b.p);
        return st;
    }
    *buf = b.p;
    *len = b.len;
    return TEPTRIS_OK;
}

void teptris_flatten_free(void *buf)
{
    free(buf);
}

const char *teptris_status_string(teptris_status status)
{
    switch (status) {
    case TEPTRIS_OK:
        return "ok";
    case TEPTRIS_ERR_ALLOC:
        return "out of memory";
    case TEPTRIS_ERR_SYNTAX:
        return "syntax error";
    case TEPTRIS_ERR_SEMANTIC:
        return "semantic error";
    case TEPTRIS_ERR_ENCODING:
        return "invalid encoding";
    case TEPTRIS_ERR_DEPTH:
        return "nesting too deep";
    case TEPTRIS_ERR_ARG:
        return "invalid argument";
    case TEPTRIS_ERR_STATE:
        return "invalid state";
    default:
        return "unknown status";
    }
}
