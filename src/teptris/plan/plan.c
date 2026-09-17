/* Plan-walk materialization (teptris#46, mirroring leptris's v1
 * descriptor ABI): compile a schema plan once, then materialize a
 * whole TOML subtree against it in one native pass. Keys the plan
 * does not describe are skipped — the read-once-and-drop fields the
 * lutaml-model profile showed as GC pressure never materialize. */
#include <stdlib.h>
#include <string.h>

#include "teptris/dom/dom.h"
#include "teptris/plan.h"

typedef struct plan_row {
    char *name; /* owned, NUL-terminated */
    uint8_t kind;
    uint32_t sub; /* sub-plan index for NESTED */
} plan_row;

struct teptris_plan {
    uint32_t plan_count;
    uint32_t *first_row; /* plan_count + 1 entries into rows */
    plan_row *rows;
    uint32_t row_total;
};

typedef struct rnode rnode;
struct rnode {
    uint8_t kind;   /* teptris_plan_result_kind */
    const teptris_node *src; /* document node for value access */
    rnode **kids;   /* ARRAY children / NESTED-of-array items */
    rnode **entries; /* RAW table entries (value side) */
    uint32_t n;
};

struct teptris_plan_result {
    const rnode *root;
};

/* ------------------------------------------------------------- build -- */

teptris_plan *teptris_plan_build(const teptris_plan_spec *spec,
                                 teptris_status *status)
{
    *status = TEPTRIS_ERR_ARG;
    if (spec == NULL || spec->abi_version != TEPTRIS_PLAN_ABI_VERSION ||
        spec->plan_count == 0 || spec->plans == NULL ||
        spec->plan_first_row == NULL) {
        return NULL;
    }
    teptris_plan *p = calloc(1, sizeof(*p));
    if (p == NULL) {
        *status = TEPTRIS_ERR_ALLOC;
        return NULL;
    }
    p->plan_count = spec->plan_count;
    p->row_total = spec->plan_first_row[spec->plan_count];
    p->first_row =
        malloc((spec->plan_count + 1) * sizeof(*p->first_row));
    /* calloc, not malloc: a mid-loop rejection frees every row name,
     * and uninitialized pointers must read as NULL (ASan caught this) */
    p->rows = p->row_total
                  ? calloc(p->row_total, sizeof(*p->rows))
                  : NULL;
    if (p->first_row == NULL || (p->row_total > 0 && p->rows == NULL)) {
        teptris_plan_free(p);
        *status = TEPTRIS_ERR_ALLOC;
        return NULL;
    }
    memcpy(p->first_row, spec->plan_first_row,
           (spec->plan_count + 1) * sizeof(*p->first_row));
    for (uint32_t i = 0; i < p->row_total; i++) {
        const teptris_plan_row *r = &spec->plans[i];
        if (r->name == NULL || r->name[0] == '\0' ||
            r->kind < TEPTRIS_PLAN_SCALAR || r->kind > TEPTRIS_PLAN_RAW ||
            (r->kind == TEPTRIS_PLAN_NESTED && r->sub >= spec->plan_count)) {
            teptris_plan_free(p);
            return NULL;
        }
        size_t nlen = strlen(r->name) + 1;
        p->rows[i].name = malloc(nlen);
        if (p->rows[i].name == NULL) {
            teptris_plan_free(p);
            *status = TEPTRIS_ERR_ALLOC;
            return NULL;
        }
        memcpy(p->rows[i].name, r->name, nlen);
        p->rows[i].kind = r->kind;
        p->rows[i].sub = r->sub;
    }
    *status = TEPTRIS_OK;
    return p;
}

void teptris_plan_free(teptris_plan *plan)
{
    if (plan == NULL) {
        return;
    }
    for (uint32_t i = 0; i < plan->row_total; i++) {
        free(plan->rows[i].name);
    }
    free(plan->rows);
    free(plan->first_row);
    free(plan);
}

/* -------------------------------------------------------------- walk -- */

static rnode *rnode_new(uint8_t kind, const teptris_node *src)
{
    rnode *r = calloc(1, sizeof(*r));
    if (r != NULL) {
        r->kind = kind;
        r->src = src;
    }
    return r;
}

static void rnode_free(rnode *r)
{
    if (r == NULL) {
        return;
    }
    for (uint32_t i = 0; i < r->n; i++) {
        rnode_free(r->kids ? r->kids[i] : r->entries[i]);
    }
    free(r->kids);
    free(r->entries);
    free(r);
}

static rnode *walk_table(const teptris_plan *p, uint32_t plan_idx,
                         const teptris_node *t);

static rnode *walk_value(const teptris_plan *p, const plan_row *row,
                         const teptris_node *v)
{
    switch (row->kind) {
    case TEPTRIS_PLAN_SCALAR:
        if (v->kind == TEPTRIS_ARRAY || v->kind == TEPTRIS_TABLE) {
            return rnode_new(TEPTRIS_PLAN_MISSING, NULL);
        }
        return rnode_new(TEPTRIS_PLAN_SCALAR, v);
    case TEPTRIS_PLAN_COLLECTION: {
        if (v->kind != TEPTRIS_ARRAY) {
            return rnode_new(TEPTRIS_PLAN_MISSING, NULL);
        }
        rnode *arr = rnode_new(TEPTRIS_PLAN_ARRAY, v);
        arr->n = (uint32_t)teptris_node_array_length(v);
        if (arr->n > 0) {
            arr->kids = calloc(arr->n, sizeof(rnode *));
        }
        for (uint32_t i = 0; i < arr->n; i++) {
            const teptris_node *item = teptris_node_array_at(v, i);
            if (item->kind == TEPTRIS_ARRAY || item->kind == TEPTRIS_TABLE) {
                arr->kids[i] = rnode_new(TEPTRIS_PLAN_MISSING, NULL);
            } else {
                arr->kids[i] = rnode_new(TEPTRIS_PLAN_SCALAR, item);
            }
        }
        return arr;
    }
    case TEPTRIS_PLAN_NESTED:
        if (v->kind == TEPTRIS_TABLE) {
            return walk_table(p, row->sub, v);
        }
        if (v->kind == TEPTRIS_ARRAY) { /* array of tables */
            rnode *arr = rnode_new(TEPTRIS_PLAN_ARRAY, v);
            arr->n = (uint32_t)teptris_node_array_length(v);
            if (arr->n > 0) {
                arr->kids = calloc(arr->n, sizeof(rnode *));
            }
            for (uint32_t i = 0; i < arr->n; i++) {
                const teptris_node *item = teptris_node_array_at(v, i);
                arr->kids[i] = item->kind == TEPTRIS_TABLE
                                   ? walk_table(p, row->sub, item)
                                   : rnode_new(TEPTRIS_PLAN_MISSING, NULL);
            }
            return arr;
        }
        return rnode_new(TEPTRIS_PLAN_MISSING, NULL);
    default: /* RAW: the full native subtree */
        return rnode_new(TEPTRIS_PLAN_RAW, v);
    }
}

static rnode *walk_table(const teptris_plan *p, uint32_t plan_idx,
                         const teptris_node *t)
{
    if (t == NULL || t->kind != TEPTRIS_TABLE) {
        return rnode_new(TEPTRIS_PLAN_MISSING, NULL);
    }
    rnode *out = rnode_new(TEPTRIS_PLAN_TABLE, t);
    uint32_t lo = p->first_row[plan_idx];
    uint32_t hi = p->first_row[plan_idx + 1];
    out->kids = hi > lo ? calloc(hi - lo, sizeof(rnode *)) : NULL;
    for (uint32_t i = lo; i < hi; i++) {
        const teptris_node *v =
            teptris_node_table_get(t, p->rows[i].name, strlen(p->rows[i].name));
        out->kids[out->n++] =
            v != NULL ? walk_value(p, &p->rows[i], v)
                      : rnode_new(TEPTRIS_PLAN_MISSING, NULL);
    }
    return out;
}

teptris_plan_result *teptris_plan_walk(const teptris_plan *plan,
                                       const teptris_node *node,
                                       teptris_status *status)
{
    *status = TEPTRIS_ERR_ARG;
    if (plan == NULL || node == NULL) {
        return NULL;
    }
    teptris_plan_result *res = calloc(1, sizeof(*res));
    if (res == NULL) {
        *status = TEPTRIS_ERR_ALLOC;
        return NULL;
    }
    res->root = walk_table(plan, 0, node);
    if (res->root == NULL) {
        free(res);
        *status = TEPTRIS_ERR_ALLOC;
        return NULL;
    }
    *status = TEPTRIS_OK;
    return res;
}

void teptris_plan_result_free(teptris_plan_result *result)
{
    if (result != NULL) {
        /* sole owner: reclaim the const view to destroy the tree */
        rnode_free((rnode *)result->root);
        free(result);
    }
}

/* ---------------------------------------------------------- accessors -- */

static const rnode *child_of(const teptris_plan_result *r, uint32_t i)
{
    const rnode *root = r->root;
    if (root->kind != TEPTRIS_PLAN_TABLE || i >= root->n) {
        return NULL;
    }
    return root->kids[i];
}

uint8_t teptris_plan_result_kind_at(const teptris_plan_result *r, uint32_t row)
{
    const rnode *c = child_of(r, row);
    return c != NULL ? c->kind : TEPTRIS_PLAN_MISSING;
}

uint8_t teptris_plan_result_value_kind_at(const teptris_plan_result *r,
                                          uint32_t row)
{
    const rnode *c = child_of(r, row);
    return (c != NULL && c->src != NULL) ? teptris_node_kind(c->src)
                                         : (uint8_t)TEPTRIS_STRING;
}

#define SCALAR_AT(fn, field, out_type)                                        \
    teptris_status fn(const teptris_plan_result *r, uint32_t row, out_type o) \
    {                                                                         \
        const rnode *c = child_of(r, row);                                    \
        if (c == NULL || c->kind != TEPTRIS_PLAN_SCALAR) {                    \
            return TEPTRIS_ERR_ARG;                                           \
        }                                                                     \
        return teptris_node_##field(c->src, o);                               \
    }

SCALAR_AT(teptris_plan_result_string_at, string, teptris_view *)
SCALAR_AT(teptris_plan_result_integer_at, integer, int64_t *)
SCALAR_AT(teptris_plan_result_float_at, float, double *)
SCALAR_AT(teptris_plan_result_boolean_at, boolean, bool *)
SCALAR_AT(teptris_plan_result_datetime_at, datetime, teptris_datetime *)

uint32_t teptris_plan_result_array_len_at(const teptris_plan_result *r,
                                          uint32_t row)
{
    const rnode *c = child_of(r, row);
    return (c != NULL && c->kind == TEPTRIS_PLAN_ARRAY) ? c->n : 0;
}

uint8_t teptris_plan_result_array_kind_at(const teptris_plan_result *r,
                                          uint32_t row, uint32_t i)
{
    const rnode *c = child_of(r, row);
    if (c == NULL || c->kind != TEPTRIS_PLAN_ARRAY || i >= c->n) {
        return TEPTRIS_PLAN_MISSING;
    }
    return c->kids[i]->kind;
}

/* The TOML value kind of an ARRAY element (drives scalar dispatch). */
uint8_t teptris_plan_result_array_value_kind_at(const teptris_plan_result *r,
                                                uint32_t row, uint32_t i)
{
    const rnode *c = child_of(r, row);
    if (c == NULL || c->kind != TEPTRIS_PLAN_ARRAY || i >= c->n) {
        return (uint8_t)TEPTRIS_STRING;
    }
    const rnode *k = c->kids[i];
    return (k->kind == TEPTRIS_PLAN_SCALAR && k->src != NULL)
               ? (uint8_t)teptris_node_kind(k->src)
               : (uint8_t)TEPTRIS_STRING;
}

teptris_status teptris_plan_result_array_string_at(const teptris_plan_result *r,
                                                   uint32_t row, uint32_t i,
                                                   teptris_view *out)
{
    const rnode *c = child_of(r, row);
    if (c == NULL || c->kind != TEPTRIS_PLAN_ARRAY || i >= c->n) {
        return TEPTRIS_ERR_ARG;
    }
    const rnode *k = c->kids[i];
    if (k->kind != TEPTRIS_PLAN_SCALAR) {
        return TEPTRIS_ERR_ARG;
    }
    return teptris_node_string(k->src, out);
}

teptris_status teptris_plan_result_array_integer_at(const teptris_plan_result *r,
                                                    uint32_t row, uint32_t i,
                                                    int64_t *out)
{
    const rnode *c = child_of(r, row);
    if (c == NULL || c->kind != TEPTRIS_PLAN_ARRAY || i >= c->n) {
        return TEPTRIS_ERR_ARG;
    }
    const rnode *k = c->kids[i];
    if (k->kind != TEPTRIS_PLAN_SCALAR) {
        return TEPTRIS_ERR_ARG;
    }
    return teptris_node_integer(k->src, out);
}

/* Borrowed sub-result: navigates an ARRAY element (e.g. a table from
 * an array-of-tables) so its plan rows stay addressable. Shares the
 * parent's lifetime — do not free. */
/* Borrowed sub-result for a TABLE-kind row (a NESTED row's walked
 * subtree): its plan rows stay addressable. Same lifetime rules as
 * array_entry_at. */
teptris_plan_result *teptris_plan_result_row_view(const teptris_plan_result *r,
                                                  uint32_t row)
{
    const rnode *c = child_of(r, row);
    if (c == NULL || c->kind != TEPTRIS_PLAN_TABLE) {
        return NULL;
    }
    teptris_plan_result *view = malloc(sizeof(*view));
    if (view != NULL) {
        view->root = c;
    }
    return view;
}

teptris_status teptris_plan_result_array_float_at(const teptris_plan_result *r,
                                                  uint32_t row, uint32_t i,
                                                  double *out)
{
    const rnode *c = child_of(r, row);
    if (c == NULL || c->kind != TEPTRIS_PLAN_ARRAY || i >= c->n) {
        return TEPTRIS_ERR_ARG;
    }
    const rnode *k = c->kids[i];
    if (k->kind != TEPTRIS_PLAN_SCALAR) {
        return TEPTRIS_ERR_ARG;
    }
    return teptris_node_float(k->src, out);
}

teptris_status teptris_plan_result_array_boolean_at(
    const teptris_plan_result *r, uint32_t row, uint32_t i, bool *out)
{
    const rnode *c = child_of(r, row);
    if (c == NULL || c->kind != TEPTRIS_PLAN_ARRAY || i >= c->n) {
        return TEPTRIS_ERR_ARG;
    }
    const rnode *k = c->kids[i];
    if (k->kind != TEPTRIS_PLAN_SCALAR) {
        return TEPTRIS_ERR_ARG;
    }
    return teptris_node_boolean(k->src, out);
}

teptris_status teptris_plan_result_array_datetime_at(
    const teptris_plan_result *r, uint32_t row, uint32_t i,
    teptris_datetime *out)
{
    const rnode *c = child_of(r, row);
    if (c == NULL || c->kind != TEPTRIS_PLAN_ARRAY || i >= c->n) {
        return TEPTRIS_ERR_ARG;
    }
    const rnode *k = c->kids[i];
    if (k->kind != TEPTRIS_PLAN_SCALAR) {
        return TEPTRIS_ERR_ARG;
    }
    return teptris_node_datetime(k->src, out);
}

uint8_t teptris_plan_row_kind_at(const teptris_plan *p, uint32_t plan_idx,
                              uint32_t row)
{
    uint32_t lo = p->first_row[plan_idx];
    if (plan_idx >= p->plan_count || row >= p->first_row[plan_idx + 1] - lo) {
        return 0;
    }
    return p->rows[lo + row].kind;
}

uint32_t teptris_plan_row_sub_at(const teptris_plan *p, uint32_t plan_idx,
                              uint32_t row)
{
    uint32_t lo = p->first_row[plan_idx];
    if (plan_idx >= p->plan_count || row >= p->first_row[plan_idx + 1] - lo) {
        return 0;
    }
    return p->rows[lo + row].sub;
}

const char *teptris_plan_row_name_at(const teptris_plan *p, uint32_t plan_idx,
                                  uint32_t row)
{
    uint32_t lo = p->first_row[plan_idx];
    if (plan_idx >= p->plan_count || row >= p->first_row[plan_idx + 1] - lo) {
        return NULL;
    }
    return p->rows[lo + row].name;
}

teptris_plan_result *teptris_plan_result_array_entry_at(
    const teptris_plan_result *r, uint32_t row, uint32_t i)
{
    const rnode *c = child_of(r, row);
    if (c == NULL || c->kind != TEPTRIS_PLAN_ARRAY || i >= c->n) {
        return NULL;
    }
    teptris_plan_result *view = malloc(sizeof(*view));
    if (view != NULL) {
        view->root = c->kids[i];
    }
    return view;
}

void teptris_plan_result_view_free(teptris_plan_result *view)
{
    /* the view's tree is owned by the parent result */
    free(view);
}

const teptris_node *teptris_plan_result_raw_at(const teptris_plan_result *r,
                                               uint32_t row)
{
    const rnode *c = child_of(r, row);
    return (c != NULL && c->kind == TEPTRIS_PLAN_RAW) ? c->src : NULL;
}

uint32_t teptris_plan_row_count(const teptris_plan *plan, uint32_t plan_idx)
{
    if (plan == NULL || plan_idx >= plan->plan_count) {
        return 0;
    }
    return plan->first_row[plan_idx + 1] - plan->first_row[plan_idx];
}

uint32_t teptris_plan_abi_version(void)
{
    return TEPTRIS_PLAN_ABI_VERSION;
}
