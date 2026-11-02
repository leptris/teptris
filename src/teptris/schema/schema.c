/* Schema compilation: JSON Schema text -> a compiled sch_node graph.
 * See schema.h for the policy contract. Unsupported keywords that
 * would silently change results are rejected here, at compile. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "schema_internal.h"
#include "teptris/teptris.h"

static void sfail(sctx *c, const char *msg, const char *where) {
    if (c->err != NULL && c->err[0] == '\0') {
        snprintf(c->err, c->err_cap, "%s%s%s", msg,
                 where != NULL && where[0] != '\0' ? " at " : "",
                 where != NULL ? where : "");
    }
}

static sch_node *sch_node_new(sctx *c) {
    sch_node *n = teptris_arena_alloc(c->a, sizeof(sch_node));
    memset(n, 0, sizeof(*n));
    n->types = ST_ANY;
    return n;
}

/* ------------------------------------------------------------- memo -- */

static sch_node *memo_get(sctx *c, const jv *v, bool *found, bool *busy) {
    *found = false;
    *busy = false;
    for (size_t i = 0; i < c->n_memo; i++) {
        if (c->memo[i].src == v) {
            *found = true;
            *busy = c->memo[i].in_progress;
            return c->memo[i].node;
        }
    }
    return NULL;
}

static void memo_put(sctx *c, const jv *v, sch_node *n) {
    if (c->n_memo == c->cap_memo) {
        c->cap_memo = c->cap_memo == 0 ? 16 : c->cap_memo * 2;
        void *nm = teptris_arena_alloc(c->a, c->cap_memo * sizeof(*c->memo));
        if (c->n_memo > 0) memcpy(nm, c->memo, c->n_memo * sizeof(*c->memo));
        c->memo = nm;
    }
    c->memo[c->n_memo].src = v;
    c->memo[c->n_memo].node = n;
    c->memo[c->n_memo].in_progress = true;
    c->n_memo++;
}

static void memo_done(sctx *c, const jv *v) {
    for (size_t i = 0; i < c->n_memo; i++) {
        if (c->memo[i].src == v) {
            c->memo[i].in_progress = false;
            return;
        }
    }
}

/* --------------------------------------------------- JSON Pointer -- */

/* resolve "#", "#/a/b", or "#anchor" to a jv subtree */
static jv *resolve_local(sctx *c, const char *ref) {
    if (ref[0] != '#') return NULL;
    if (ref[1] == '\0') return c->root;
    if (ref[1] == '/') {
        /* JSON Pointer with ~0/~1 unescaping */
        jv *cur = c->root;
        const char *p = ref + 2;
        while (*p != '\0') {
            if (cur == NULL || jv_kind_of(cur) != JV_OBJECT) return NULL;
            size_t tlen = 0;
            const char *tok_end = strchr(p, '/');
            tlen = tok_end != NULL ? (size_t)(tok_end - p) : strlen(p);
            char tok[256];
            if (tlen >= sizeof tok) return NULL;
            size_t o = 0;
            for (size_t i = 0; i < tlen; i++) {
                if (p[i] == '~' && i + 1 < tlen) {
                    tok[o++] = p[i + 1] == '1' ? '/' : '~';
                    i++;
                } else {
                    tok[o++] = p[i];
                }
            }
            tok[o] = '\0';
            cur = jv_member_get(cur, tok);
            if (cur == NULL) return NULL;
            p += tlen;
            if (*p == '/') p++;
        }
        return cur;
    }
    /* plain-name fragment: an $anchor */
    for (size_t i = 0; i < c->n_anchors; i++) {
        if (strcmp(c->anchors[i].name, ref + 1) == 0) {
            return c->anchors[i].target;
        }
    }
    return NULL;
}

sch_node *sch_resolve_pointer(sctx *c, const char *pointer) {
    jv *target = resolve_local(c, pointer);
    if (target == NULL) return NULL;
    return sch_compile_node(c, target);
}

/* -------------------------------------------------- anchor collection */

static void collect_anchors(sctx *c, jv *v) {
    if (v == NULL) return;
    if (jv_kind_of(v) == JV_OBJECT) {
        for (size_t i = 0; i < jv_member_count(v); i++) {
            const char *k = jv_member_key(v, i);
            jv *val = jv_member_val(v, i);
            if (strcmp(k, "$anchor") == 0 && jv_kind_of(val) == JV_STRING) {
                if (c->n_anchors == c->cap_anchors) {
                    c->cap_anchors = c->cap_anchors == 0 ? 8 : c->cap_anchors * 2;
                    void *na =
                        teptris_arena_alloc(c->a, c->cap_anchors * sizeof(*c->anchors));
                    if (c->n_anchors > 0) {
                        memcpy(na, c->anchors, c->n_anchors * sizeof(*c->anchors));
                    }
                    c->anchors = na;
                }
                c->anchors[c->n_anchors].name = jv_str(val);
                c->anchors[c->n_anchors].target = v;
                c->n_anchors++;
            }
            collect_anchors(c, val);
        }
    } else if (jv_kind_of(v) == JV_ARRAY) {
        for (size_t i = 0; i < jv_item_count(v); i++) {
            collect_anchors(c, jv_item(v, i));
        }
    }
}

/* ----------------------------------------------------------- compile -- */

static int type_bit(const char *name) {
    if (strcmp(name, "string") == 0) return ST_STRING;
    if (strcmp(name, "integer") == 0) return ST_INTEGER;
    if (strcmp(name, "number") == 0) return ST_NUMBER;
    if (strcmp(name, "boolean") == 0) return ST_BOOLEAN;
    if (strcmp(name, "object") == 0) return ST_OBJECT;
    if (strcmp(name, "array") == 0) return ST_ARRAY;
    return 0;
}

static int format_kind(const char *name) {
    if (strcmp(name, "date-time") == 0) return FMT_DATETIME;
    if (strcmp(name, "date") == 0) return FMT_DATE;
    if (strcmp(name, "time") == 0) return FMT_TIME;
    return FMT_NONE; /* other formats: annotations (host's choice) */
}

static bool compile_type(sctx *c, sch_node *n, jv *tv, const char *where) {
    if (jv_kind_of(tv) == JV_STRING) {
        int bit = type_bit(jv_str(tv));
        if (bit == 0) {
            sfail(c, "unknown type name", where);
            return false;
        }
        n->types = (unsigned)bit;
        return true;
    }
    if (jv_kind_of(tv) == JV_ARRAY) {
        unsigned mask = 0;
        bool had_null = false;
        for (size_t i = 0; i < jv_item_count(tv); i++) {
            jv *e = jv_item(tv, i);
            if (jv_kind_of(e) == JV_NULL) {
                had_null = true; /* policy 1: null matches no TOML value */
                continue;
            }
            if (jv_kind_of(e) != JV_STRING) {
                sfail(c, "type entries must be strings", where);
                return false;
            }
            int bit = type_bit(jv_str(e));
            if (bit == 0) {
                sfail(c, "unknown type name", where);
                return false;
            }
            mask |= (unsigned)bit;
        }
        n->types = mask != 0 ? mask : 0;
        if (had_null && mask == 0) {
            n->is_false = true; /* "type": "null" alone: unsatisfiable */
        }
        return true;
    }
    sfail(c, "type must be a string or array of strings", where);
    return false;
}

sch_node *sch_compile_node(sctx *c, const jv *v) {
    bool found, busy;
    sch_node *memo_hit = memo_get(c, v, &found, &busy);
    if (found) {
        return memo_hit; /* cycles resolve: the node fills in place */
    }

    if (jv_kind_of(v) == JV_BOOL) {
        sch_node *n = sch_node_new(c);
        memo_put(c, v, n);
        n->is_false = !jv_bool(v);
        memo_done(c, v);
        return n;
    }
    if (jv_kind_of(v) != JV_OBJECT) {
        sfail(c, "a schema must be an object or boolean", NULL);
        return NULL;
    }

    sch_node *n = sch_node_new(c);
    memo_put(c, v, n);

    const char *unsupported[] = {
        "unevaluatedProperties", "unevaluatedItems", "$dynamicRef",
        "$dynamicAnchor", "$recursiveRef", "$recursiveAnchor", NULL
    };

    for (size_t i = 0; i < jv_member_count(v); i++) {
        const char *k = jv_member_key(v, i);
        jv *val = jv_member_val(v, i);

        /* unsupported KNOWN keywords: reject (ignoring them would
         * silently accept documents that should fail) */
        for (int u = 0; unsupported[u] != NULL; u++) {
            if (strcmp(k, unsupported[u]) == 0) {
                sfail(c, "unsupported keyword", k);
                return NULL;
            }
        }

        if (strcmp(k, "type") == 0) {
            if (!compile_type(c, n, val, k)) return NULL;
        } else if (strcmp(k, "$ref") == 0) {
            if (jv_kind_of(val) != JV_STRING) {
                sfail(c, "$ref must be a string", k);
                return NULL;
            }
            const char *r = jv_str(val);
            if (r[0] != '#') {
                sfail(c, "remote $ref is not supported (a parser library never touches the network)", r);
                return NULL;
            }
            n->ref = sch_resolve_pointer(c, r);
            if (n->ref == NULL) {
                sfail(c, "unresolvable $ref", r);
                return NULL;
            }
        } else if (strcmp(k, "enum") == 0) {
            if (jv_kind_of(val) != JV_ARRAY) {
                sfail(c, "enum must be an array", k);
                return NULL;
            }
            n->enum_vals = teptris_arena_alloc(
                c->a, (jv_item_count(val) ? jv_item_count(val) : 1) * sizeof(jv *));
            n->n_enum = jv_item_count(val);
            for (size_t e = 0; e < jv_item_count(val); e++) {
                n->enum_vals[e] = jv_item(val, e);
            }
        } else if (strcmp(k, "const") == 0) {
            n->const_val = val;
            n->has_const = true;
        } else if (strcmp(k, "properties") == 0 &&
                   jv_kind_of(val) == JV_OBJECT) {
            size_t cnt = jv_member_count(val);
            n->props = teptris_arena_alloc(c->a, (cnt ? cnt : 1) * sizeof(sch_prop));
            for (size_t p = 0; p < cnt; p++) {
                n->props[p].name = jv_member_key(val, p);
                n->props[p].schema = sch_compile_node(c, jv_member_val(val, p));
                if (n->props[p].schema == NULL && c->err[0] != '\0') return NULL;
            }
            n->n_props = cnt;
        } else if (strcmp(k, "patternProperties") == 0 &&
                   jv_kind_of(val) == JV_OBJECT) {
            size_t cnt = jv_member_count(val);
            n->pprops = teptris_arena_alloc(c->a, (cnt ? cnt : 1) * sizeof(sch_pprop));
            for (size_t p = 0; p < cnt; p++) {
                char rerr[128] = {0};
                struct sch_regex *re = sch_regex_compile(
                    c->a, jv_member_key(val, p), strlen(jv_member_key(val, p)),
                    rerr, sizeof rerr);
                if (re == NULL) {
                    sfail(c, rerr[0] != '\0' ? rerr : "invalid pattern", k);
                    return NULL;
                }
                n->pprops[p].re = re;
                n->pprops[p].schema = sch_compile_node(c, jv_member_val(val, p));
                if (n->pprops[p].schema == NULL && c->err[0] != '\0') return NULL;
            }
            n->n_pprops = cnt;
        } else if (strcmp(k, "additionalProperties") == 0) {
            n->additional = sch_compile_node(c, val);
            if (n->additional == NULL && c->err[0] != '\0') return NULL;
            n->additional_set = true;
        } else if (strcmp(k, "required") == 0 &&
                   jv_kind_of(val) == JV_ARRAY) {
            size_t cnt = jv_item_count(val);
            n->required = teptris_arena_alloc(c->a, (cnt ? cnt : 1) * sizeof(char *));
            for (size_t r = 0; r < cnt; r++) {
                jv *e = jv_item(val, r);
                if (jv_kind_of(e) != JV_STRING) {
                    sfail(c, "required entries must be strings", k);
                    return NULL;
                }
                /* NUL-terminated key for strcmp lookups */
                size_t slen = jv_str_len(e);
                char *rb = teptris_arena_alloc(c->a, slen + 1);
                memcpy(rb, jv_str(e), slen);
                rb[slen] = '\0';
                n->required[r] = rb;
            }
            n->n_req = cnt;
        } else if (strcmp(k, "propertyNames") == 0) {
            n->prop_names = sch_compile_node(c, val);
            if (n->prop_names == NULL && c->err[0] != '\0') return NULL;
        } else if (strcmp(k, "minProperties") == 0 &&
                   jv_kind_of(val) == JV_INT) {
            n->min_props = (long)jv_int(val);
            n->has_min_props = true;
        } else if (strcmp(k, "maxProperties") == 0 &&
                   jv_kind_of(val) == JV_INT) {
            n->max_props = (long)jv_int(val);
            n->has_max_props = true;
        } else if (strcmp(k, "dependentRequired") == 0 &&
                   jv_kind_of(val) == JV_OBJECT) {
            size_t cnt = jv_member_count(val);
            n->dep_req =
                teptris_arena_alloc(c->a, (cnt ? cnt : 1) * sizeof(sch_kschema));
            for (size_t p = 0; p < cnt; p++) {
                jv *list = jv_member_val(val, p);
                if (jv_kind_of(list) != JV_ARRAY) {
                    sfail(c, "dependentRequired values must be arrays", k);
                    return NULL;
                }
                n->dep_req[p].key = jv_member_key(val, p);
                /* reuse the required storage shape: an sch_node whose
                 * required list carries the dependency keys */
                sch_node *fake = sch_node_new(c);
                size_t dn = jv_item_count(list);
                fake->required =
                    teptris_arena_alloc(c->a, (dn ? dn : 1) * sizeof(char *));
                for (size_t d = 0; d < dn; d++) {
                    jv *e = jv_item(list, d);
                    if (jv_kind_of(e) != JV_STRING) {
                        sfail(c, "dependentRequired entries must be strings", k);
                        return NULL;
                    }
                    size_t slen = jv_str_len(e);
                    char *rb = teptris_arena_alloc(c->a, slen + 1);
                    memcpy(rb, jv_str(e), slen);
                    rb[slen] = '\0';
                    fake->required[d] = rb;
                }
                fake->n_req = dn;
                n->dep_req[p].schema = fake;
            }
            n->n_dep_req = cnt;
        } else if (strcmp(k, "dependentSchemas") == 0 &&
                   jv_kind_of(val) == JV_OBJECT) {
            size_t cnt = jv_member_count(val);
            n->dep_schemas = teptris_arena_alloc(
                c->a, (cnt ? cnt : 1) * sizeof(sch_kschema));
            for (size_t p = 0; p < cnt; p++) {
                n->dep_schemas[p].key = jv_member_key(val, p);
                n->dep_schemas[p].schema = sch_compile_node(c, jv_member_val(val, p));
                if (n->dep_schemas[p].schema == NULL && c->err[0] != '\0') return NULL;
            }
            n->n_dep_schemas = cnt;
        } else if (strcmp(k, "items") == 0) {
            n->items = sch_compile_node(c, val);
            if (n->items == NULL && c->err[0] != '\0') return NULL;
            n->items_set = true;
        } else if (strcmp(k, "prefixItems") == 0 &&
                   jv_kind_of(val) == JV_ARRAY) {
            size_t cnt = jv_item_count(val);
            n->prefix = teptris_arena_alloc(c->a, (cnt ? cnt : 1) * sizeof(sch_node *));
            for (size_t p = 0; p < cnt; p++) {
                n->prefix[p] = sch_compile_node(c, jv_item(val, p));
                if (n->prefix[p] == NULL && c->err[0] != '\0') return NULL;
            }
            n->n_prefix = cnt;
        } else if (strcmp(k, "contains") == 0) {
            n->contains = sch_compile_node(c, val);
            if (n->contains == NULL && c->err[0] != '\0') return NULL;
            n->has_contains = true;
        } else if (strcmp(k, "minContains") == 0 && jv_kind_of(val) == JV_INT) {
            n->min_contains = (long)jv_int(val);
            n->has_min_contains = true;
        } else if (strcmp(k, "maxContains") == 0 && jv_kind_of(val) == JV_INT) {
            n->max_contains = (long)jv_int(val);
            n->has_max_contains = true;
        } else if (strcmp(k, "uniqueItems") == 0 && jv_kind_of(val) == JV_BOOL) {
            n->unique_items = jv_bool(val);
            n->has_unique = true;
        } else if (strcmp(k, "minItems") == 0 && jv_kind_of(val) == JV_INT) {
            n->min_items = (long)jv_int(val);
            n->has_min_items = true;
        } else if (strcmp(k, "maxItems") == 0 && jv_kind_of(val) == JV_INT) {
            n->max_items = (long)jv_int(val);
            n->has_max_items = true;
        } else if (strcmp(k, "pattern") == 0 && jv_kind_of(val) == JV_STRING) {
            char rerr[128] = {0};
            n->pattern = sch_regex_compile(c->a, jv_str(val), jv_str_len(val),
                                           rerr, sizeof rerr);
            if (n->pattern == NULL) {
                sfail(c, rerr[0] != '\0' ? rerr : "invalid pattern", k);
                return NULL;
            }
        } else if (strcmp(k, "minLength") == 0 && jv_kind_of(val) == JV_INT) {
            n->min_length = (long)jv_int(val);
            n->has_min_length = true;
        } else if (strcmp(k, "maxLength") == 0 && jv_kind_of(val) == JV_INT) {
            n->max_length = (long)jv_int(val);
            n->has_max_length = true;
        } else if (strcmp(k, "format") == 0 && jv_kind_of(val) == JV_STRING) {
            n->format = format_kind(jv_str(val));
        } else if ((strcmp(k, "minimum") == 0 || strcmp(k, "maximum") == 0 ||
                    strcmp(k, "exclusiveMinimum") == 0 ||
                    strcmp(k, "exclusiveMaximum") == 0) &&
                   jv_kind_of(val) == JV_INT) {
            if (strcmp(k, "minimum") == 0) {
                n->fmin = (double)jv_int(val); n->imin = jv_int(val); n->has_min = true;
            } else if (strcmp(k, "maximum") == 0) {
                n->fmax = (double)jv_int(val); n->imax = jv_int(val); n->has_max = true;
            } else if (strcmp(k, "exclusiveMinimum") == 0) {
                n->fex_min = (double)jv_int(val); n->iex_min = jv_int(val); n->has_ex_min = true;
            } else {
                n->fex_max = (double)jv_int(val); n->iex_max = jv_int(val); n->has_ex_max = true;
            }
        } else if ((strcmp(k, "minimum") == 0 || strcmp(k, "maximum") == 0 ||
                    strcmp(k, "exclusiveMinimum") == 0 ||
                    strcmp(k, "exclusiveMaximum") == 0) &&
                   jv_kind_of(val) == JV_NUM) {
            if (strcmp(k, "minimum") == 0) { n->fmin = jv_num(val); n->has_min = true; }
            else if (strcmp(k, "maximum") == 0) { n->fmax = jv_num(val); n->has_max = true; }
            else if (strcmp(k, "exclusiveMinimum") == 0) { n->fex_min = jv_num(val); n->has_ex_min = true; }
            else { n->fex_max = jv_num(val); n->has_ex_max = true; }
        } else if (strcmp(k, "multipleOf") == 0) {
            double m = jv_num(val);
            if (!(m > 0.0)) {
                sfail(c, "multipleOf must be > 0", k);
                return NULL;
            }
            n->has_mult = true;
            n->fmult = m;
            if (jv_kind_of(val) == JV_INT) {
                n->mult_is_int = true;
                n->imult = jv_int(val);
            }
        } else if (strcmp(k, "allOf") == 0 && jv_kind_of(val) == JV_ARRAY) {
            size_t cnt = jv_item_count(val);
            n->all_of = teptris_arena_alloc(c->a, (cnt ? cnt : 1) * sizeof(sch_node *));
            for (size_t p = 0; p < cnt; p++) {
                n->all_of[p] = sch_compile_node(c, jv_item(val, p));
                if (n->all_of[p] == NULL && c->err[0] != '\0') return NULL;
            }
            n->n_all = cnt;
        } else if (strcmp(k, "anyOf") == 0 && jv_kind_of(val) == JV_ARRAY) {
            size_t cnt = jv_item_count(val);
            n->any_of = teptris_arena_alloc(c->a, (cnt ? cnt : 1) * sizeof(sch_node *));
            for (size_t p = 0; p < cnt; p++) {
                n->any_of[p] = sch_compile_node(c, jv_item(val, p));
                if (n->any_of[p] == NULL && c->err[0] != '\0') return NULL;
            }
            n->n_any = cnt;
        } else if (strcmp(k, "oneOf") == 0 && jv_kind_of(val) == JV_ARRAY) {
            size_t cnt = jv_item_count(val);
            n->one_of = teptris_arena_alloc(c->a, (cnt ? cnt : 1) * sizeof(sch_node *));
            for (size_t p = 0; p < cnt; p++) {
                n->one_of[p] = sch_compile_node(c, jv_item(val, p));
                if (n->one_of[p] == NULL && c->err[0] != '\0') return NULL;
            }
            n->n_one = cnt;
        } else if (strcmp(k, "not") == 0) {
            n->_not = sch_compile_node(c, val);
            if (n->_not == NULL && c->err[0] != '\0') return NULL;
        } else if (strcmp(k, "if") == 0) {
            n->if_s = sch_compile_node(c, val);
            if (n->if_s == NULL && c->err[0] != '\0') return NULL;
        } else if (strcmp(k, "then") == 0) {
            n->then_s = sch_compile_node(c, val);
            if (n->then_s == NULL && c->err[0] != '\0') return NULL;
        } else if (strcmp(k, "else") == 0) {
            n->else_s = sch_compile_node(c, val);
            if (n->else_s == NULL && c->err[0] != '\0') return NULL;
        }
        /* everything else: annotations, $schema/$id/$comment/$defs/
         * definitions/$anchor (containers resolved via $ref), unknown
         * keywords (extensions per the spec): all ignored */
    }
    memo_done(c, v);
    return n;
}

/* ------------------------------------------------------------ public -- */

TEPTRIS_API teptris_status teptris_schema_compile(const char *json, size_t len,
                                                  struct teptris_schema **out,
                                                  char **diag_buf,
                                                  size_t *diag_len) {
    if (json == NULL || out == NULL) {
        return TEPTRIS_ERR_ARG;
    }
    if (diag_buf != NULL) *diag_buf = NULL;
    if (diag_len != NULL) *diag_len = 0;

    struct teptris_schema *sch = malloc(sizeof(*sch));
    if (sch == NULL) return TEPTRIS_ERR_ALLOC;
    memset(sch, 0, sizeof(*sch));
    teptris_arena_init(&sch->arena);

    char jerr[160] = {0};
    jv *root = json_read(&sch->arena, json, len, jerr, sizeof jerr);
    if (root == NULL) {
        if (diag_buf != NULL) {
            char diag[256];
            snprintf(diag, sizeof diag, "{\"error\":%s}",
                     jerr[0] != '\0' ? jerr : "invalid schema JSON");
            /* wrap the message as a JSON string */
            char quoted[288];
            size_t o = 0;
            quoted[o++] = '"';
            for (const char *p = jerr[0] != '\0' ? jerr : "invalid schema JSON";
                 *p != '\0' && o < sizeof quoted - 2; p++) {
                if (*p == '"') quoted[o++] = '\\';
                quoted[o++] = *p;
            }
            quoted[o++] = '"';
            quoted[o] = '\0';
            snprintf(diag, sizeof diag, "{\"error\":%s}", quoted);
            size_t dlen = strlen(diag);
            char *buf = malloc(dlen + 1);
            if (buf != NULL) {
                memcpy(buf, diag, dlen + 1);
                *diag_buf = buf;
                if (diag_len != NULL) *diag_len = dlen;
            }
        }
        teptris_arena_destroy(&sch->arena);
        free(sch);
        return TEPTRIS_ERR_ARG;
    }

    sctx c;
    memset(&c, 0, sizeof(c));
    c.a = &sch->arena;
    c.root = root;
    c.err = sch->err;
    c.err_cap = sizeof sch->err;
    sch->err[0] = '\0';

    collect_anchors(&c, root);
    sch_node *compiled = sch_compile_node(&c, root);
    if (compiled == NULL) {
        if (diag_buf != NULL) {
            char quoted[288];
            size_t o = 0;
            quoted[o++] = '"';
            const char *msg = sch->err[0] != '\0' ? sch->err : "invalid schema";
            for (const char *p = msg; *p != '\0' && o < sizeof quoted - 2; p++) {
                if (*p == '"') quoted[o++] = '\\';
                quoted[o++] = *p;
            }
            quoted[o++] = '"';
            quoted[o] = '\0';
            char diag[320];
            snprintf(diag, sizeof diag, "{\"error\":%s}", quoted);
            size_t dlen = strlen(diag);
            char *buf = malloc(dlen + 1);
            if (buf != NULL) {
                memcpy(buf, diag, dlen + 1);
                *diag_buf = buf;
                if (diag_len != NULL) *diag_len = dlen;
            }
        }
        teptris_arena_destroy(&sch->arena);
        free(sch);
        return TEPTRIS_ERR_ARG;
    }
    sch->root = compiled;
    *out = sch;
    return TEPTRIS_OK;
}

TEPTRIS_API void teptris_diag_free(char *buf) { free(buf); }

TEPTRIS_API void teptris_schema_free(struct teptris_schema *sch) {
    if (sch == NULL) return;
    teptris_arena_destroy(&sch->arena);
    free(sch);
}
