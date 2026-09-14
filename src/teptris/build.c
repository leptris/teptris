#include <stdlib.h>
#include <string.h>

#include "teptris/dom/dom.h"
#include "teptris/teptris.h"

/* Dump-side document construction. The parser assembles nodes from
 * text; the builder assembles them from materialized values so dump
 * consumers share the emitter (single formatting source). */

#define BUILDER_MAX_DEPTH 512

enum arr_nature {
    ARR_UNDECIDED = 0, /* no elements yet */
    ARR_INLINE,        /* scalar/inline elements: renders [..] */
    ARR_AOT_SECTIONS   /* table elements: renders [[..]] */
};

typedef struct bframe {
    teptris_node *node;
    bool is_array;
    uint8_t nature; /* arr_nature when is_array */
} bframe;

struct teptris_builder {
    teptris_document *doc; /* NULL once finish() transferred it */
    bframe stack[BUILDER_MAX_DEPTH];
    int depth; /* index of the current container */
};

static teptris_status doc_strdup(teptris_document *doc, const char *src,
                                 size_t len, teptris_view *out)
{
    char *copy = teptris_arena_alloc(&doc->arena, len + 1);
    if (copy == NULL) {
        return TEPTRIS_ERR_ALLOC;
    }
    memcpy(copy, src, len);
    copy[len] = '\0';
    out->ptr = copy;
    out->len = len;
    return TEPTRIS_OK;
}

static bool datetime_ok(teptris_kind kind, const teptris_datetime *dt)
{
    if (dt->hour > 23 || dt->minute > 59 || dt->second > 60 ||
        dt->nanosecond > 999999999 ||
        dt->offset_seconds < -86399 || dt->offset_seconds > 86399) {
        return false;
    }
    if (kind == TEPTRIS_TIME_LOCAL) {
        /* time of day: no date component at all */
        return dt->year == 0 && dt->month == 0 && dt->day == 0 &&
               dt->offset_seconds == 0;
    }
    if (dt->year > 9999 || dt->month < 1 || dt->month > 12 ||
        dt->day < 1 || dt->day > 31) {
        return false;
    }
    if (kind == TEPTRIS_DATE_LOCAL &&
        (dt->hour != 0 || dt->minute != 0 || dt->second != 0 ||
         dt->nanosecond != 0)) {
        return false;
    }
    if (kind != TEPTRIS_DATETIME_OFFSET && dt->offset_seconds != 0) {
        return false;
    }
    return true;
}

static const bframe *current(const teptris_builder *b)
{
    return &b->stack[b->depth];
}

/* Attach node to the current container: keyed insert into a table, or
 * element push into an array (deciding the array's nature). Returns
 * the attached node for scalar field fills. */
static teptris_status attach(teptris_builder *b, const char *key,
                             size_t key_len, bool element_is_table,
                             teptris_node *n, teptris_node **attached)
{
    const bframe *f = current(b);
    if (f->is_array) {
        if (element_is_table) {
            if (f->nature == ARR_UNDECIDED) {
                /* first element is a table: array of tables */
                b->stack[b->depth].nature = ARR_AOT_SECTIONS;
                f->node->flags |= ARR_AOT;
            }
            /* tables of an inline array render inline (open_table
             * picked the flag from the array's nature) */
        } else if (f->nature == ARR_AOT_SECTIONS) {
            return TEPTRIS_ERR_ARG; /* [[..]] sections cannot mix values */
        } else if (f->nature == ARR_UNDECIDED) {
            b->stack[b->depth].nature = ARR_INLINE;
        }
        teptris_status st = teptris_dom_array_push(b->doc, f->node, n);
        if (st != TEPTRIS_OK) {
            return st;
        }
        *attached = n;
        return TEPTRIS_OK;
    }
    if (key == NULL) {
        return TEPTRIS_ERR_ARG; /* element push requires an array current */
    }
    teptris_view kview;
    teptris_status st = doc_strdup(b->doc, key, key_len, &kview);
    if (st != TEPTRIS_OK) {
        return st;
    }
    uint64_t hash;
    if (teptris_dom_table_find_h(f->node, kview.ptr, kview.len, &hash) != NULL) {
        return TEPTRIS_ERR_ARG; /* duplicate key */
    }
    st = teptris_dom_table_insert_h(b->doc, f->node, kview, hash, n);
    if (st != TEPTRIS_OK) {
        return st;
    }
    *attached = n;
    return TEPTRIS_OK;
}

static teptris_status put_scalar(teptris_builder *b, const char *key,
                                 size_t key_len, teptris_kind kind,
                                 teptris_node **out)
{
    if (b->doc == NULL) {
        return TEPTRIS_ERR_STATE;
    }
    teptris_node *n = teptris_dom_new_node(b->doc, kind);
    if (n == NULL) {
        return TEPTRIS_ERR_ALLOC;
    }
    return attach(b, key, key_len, false, n, out);
}

static teptris_status push_container(teptris_builder *b, const char *key,
                                     size_t key_len, teptris_node *n,
                                     bool n_is_array)
{
    teptris_node *attached;
    teptris_status st = attach(b, key, key_len, !n_is_array, n, &attached);
    if (st != TEPTRIS_OK) {
        return st;
    }
    if (b->depth + 1 >= BUILDER_MAX_DEPTH) {
        return TEPTRIS_ERR_DEPTH;
    }
    b->depth++;
    b->stack[b->depth].node = n;
    b->stack[b->depth].is_array = n_is_array;
    b->stack[b->depth].nature = ARR_UNDECIDED;
    return TEPTRIS_OK;
}

teptris_builder *teptris_builder_new(void)
{
    teptris_builder *b = calloc(1, sizeof(teptris_builder));
    if (b == NULL) {
        return NULL;
    }
    teptris_document *doc = calloc(1, sizeof(teptris_document));
    if (doc == NULL) {
        free(b);
        return NULL;
    }
    teptris_arena_init(&doc->arena);
    doc->max_depth = BUILDER_MAX_DEPTH;
    doc->err.status = TEPTRIS_OK;
    doc->err.message = "";
    doc->root = teptris_dom_new_table(doc, TBL_EXPLICIT);
    if (doc->root == NULL) {
        teptris_arena_destroy(&doc->arena);
        free(doc);
        free(b);
        return NULL;
    }
    b->doc = doc;
    b->stack[0].node = doc->root;
    return b;
}

void teptris_builder_free(teptris_builder *b)
{
    if (b == NULL) {
        return;
    }
    teptris_document_free(b->doc); /* NULL after finish() */
    free(b);
}

teptris_status teptris_builder_put_string(teptris_builder *b, const char *key,
                                          size_t key_len, const char *val,
                                          size_t val_len)
{
    teptris_node *n;
    teptris_status st = put_scalar(b, key, key_len, TEPTRIS_STRING, &n);
    if (st != TEPTRIS_OK) {
        return st;
    }
    return doc_strdup(b->doc, val, val_len, &n->as.str);
}

teptris_status teptris_builder_put_integer(teptris_builder *b, const char *key,
                                           size_t key_len, int64_t v)
{
    teptris_node *n;
    teptris_status st = put_scalar(b, key, key_len, TEPTRIS_INTEGER, &n);
    if (st != TEPTRIS_OK) {
        return st;
    }
    n->as.i = v;
    return TEPTRIS_OK;
}

teptris_status teptris_builder_put_float(teptris_builder *b, const char *key,
                                         size_t key_len, double v)
{
    teptris_node *n;
    teptris_status st = put_scalar(b, key, key_len, TEPTRIS_FLOAT, &n);
    if (st != TEPTRIS_OK) {
        return st;
    }
    n->as.f = v;
    return TEPTRIS_OK;
}

teptris_status teptris_builder_put_boolean(teptris_builder *b, const char *key,
                                           size_t key_len, bool v)
{
    teptris_node *n;
    teptris_status st = put_scalar(b, key, key_len, TEPTRIS_BOOLEAN, &n);
    if (st != TEPTRIS_OK) {
        return st;
    }
    n->as.b = v;
    return TEPTRIS_OK;
}

teptris_status teptris_builder_put_datetime(teptris_builder *b, const char *key,
                                            size_t key_len, teptris_kind kind,
                                            const teptris_datetime *dt)
{
    if (kind != TEPTRIS_DATETIME_OFFSET && kind != TEPTRIS_DATETIME_LOCAL &&
        kind != TEPTRIS_DATE_LOCAL && kind != TEPTRIS_TIME_LOCAL) {
        return TEPTRIS_ERR_ARG;
    }
    if (!datetime_ok(kind, dt)) {
        return TEPTRIS_ERR_ARG;
    }
    teptris_node *n;
    teptris_status st = put_scalar(b, key, key_len, kind, &n);
    if (st != TEPTRIS_OK) {
        return st;
    }
    n->as.dt = *dt;
    return TEPTRIS_OK;
}

teptris_status teptris_builder_open_table(teptris_builder *b, const char *key,
                                          size_t key_len)
{
    if (b->doc == NULL) {
        return TEPTRIS_ERR_STATE;
    }
    const bframe *f = current(b);
    uint8_t flags;
    if (f->is_array) {
        /* UNDECIDED means this is the first element: attach() turns the
         * array into an AOT and the table renders as a section. */
        flags = (f->nature == ARR_INLINE) ? TBL_INLINE : TBL_EXPLICIT;
    } else if ((f->node->flags & TBL_INLINE) != 0) {
        flags = TBL_INLINE; /* tables inside inline tables stay inline */
    } else {
        flags = TBL_EXPLICIT;
    }
    teptris_node *t = teptris_dom_new_table(b->doc, flags);
    if (t == NULL) {
        return TEPTRIS_ERR_ALLOC;
    }
    return push_container(b, key, key_len, t, false);
}

teptris_status teptris_builder_open_array(teptris_builder *b, const char *key,
                                          size_t key_len)
{
    if (b->doc == NULL) {
        return TEPTRIS_ERR_STATE;
    }
    teptris_node *a = teptris_dom_new_array(b->doc, 0);
    if (a == NULL) {
        return TEPTRIS_ERR_ALLOC;
    }
    teptris_status st = push_container(b, key, key_len, a, true);
    if (st != TEPTRIS_OK) {
        return st;
    }
    /* arrays in inline territory (inside an inline array or an inline
     * table) can never become [[..]] sections: pin them inline */
    const bframe *parent = &b->stack[b->depth - 1];
    bool inline_territory =
        (parent->is_array && parent->nature == ARR_INLINE) ||
        (!parent->is_array && (parent->node->flags & TBL_INLINE) != 0);
    if (inline_territory) {
        b->stack[b->depth].nature = ARR_INLINE;
    }
    return TEPTRIS_OK;
}

teptris_status teptris_builder_open_inline_array(teptris_builder *b,
                                                 const char *key,
                                                 size_t key_len)
{
    if (b->doc == NULL) {
        return TEPTRIS_ERR_STATE;
    }
    teptris_node *a = teptris_dom_new_array(b->doc, 0);
    if (a == NULL) {
        return TEPTRIS_ERR_ALLOC;
    }
    teptris_status st = push_container(b, key, key_len, a, true);
    if (st != TEPTRIS_OK) {
        return st;
    }
    /* explicit inline mode: caller-side lookahead knows the array mixes
     * tables with values (or wants [..] rendering) */
    b->stack[b->depth].nature = ARR_INLINE;
    return TEPTRIS_OK;
}

teptris_status teptris_builder_close(teptris_builder *b)
{
    if (b->doc == NULL || b->depth == 0) {
        return TEPTRIS_ERR_STATE;
    }
    b->depth--;
    return TEPTRIS_OK;
}

teptris_status teptris_builder_finish(teptris_builder *b, teptris_document **out)
{
    *out = NULL;
    if (b->doc == NULL) {
        return TEPTRIS_ERR_STATE;
    }
    if (b->depth != 0) {
        return TEPTRIS_ERR_STATE; /* unclosed containers */
    }
    *out = b->doc;
    b->doc = NULL;
    return TEPTRIS_OK;
}
