/* Schema compilation: JSON Schema text -> a compiled sch_node graph.
 * See schema.h for the policy contract. Unsupported keywords that
 * would silently change results are rejected here, at compile. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "refs.h"
#include "metaschemas.h"
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

/* pointer-keyed open addressing (FNV-1a over the pointer bytes); the
 * slot holds the entry index + 1, 0 = empty. Compile is O(n), not
 * O(n^2): memo and node-base lookups are per-node on large schemas. */
static uint32_t ptr_hash(const void *p) {
    uint64_t x = (uint64_t)(uintptr_t)p;
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < sizeof x; i++) {
        h ^= (uint32_t)(x & 0xff);
        h *= 16777619u;
        x >>= 8;
    }
    return h;
}

static uint32_t *hash_build(sctx *c, const void **keys, size_t n,
                            size_t *cap_out) {
    size_t ncap = 64;
    while (ncap * 7 / 10 < n + 1) ncap *= 2;
    uint32_t *nt = teptris_arena_alloc(c->a, ncap * sizeof(uint32_t));
    memset(nt, 0, ncap * sizeof(uint32_t));
    uint32_t mask = (uint32_t)ncap - 1;
    for (size_t i = 0; i < n; i++) {
        size_t slot = ptr_hash(keys[i]) & mask;
        while (nt[slot] != 0) slot = (slot + 1) & mask;
        nt[slot] = (uint32_t)(i + 1);
    }
    *cap_out = ncap;
    return nt;
}

/* entries: base of an array whose stride is `stride`; the key sits in
 * the first field (offset 0) of each entry */
static size_t hash_find(const uint32_t *tbl, size_t cap, const void *key,
                        const char *entries, size_t stride) {
    if (tbl == NULL || cap == 0) return SIZE_MAX;
    uint32_t mask = (uint32_t)cap - 1;
    size_t slot = ptr_hash(key) & mask;
    while (tbl[slot] != 0) {
        size_t idx = tbl[slot] - 1;
        const void *k = *(const void *const *)(entries + idx * stride);
        if (k == key) return idx;
        slot = (slot + 1) & mask;
    }
    return SIZE_MAX;
}

static void memo_hash_put(sctx *c, const jv *v) {
    if (c->n_memo + 1 > c->memo_hash_cap * 7 / 10) {
        /* rebuild from the entry list */
        const void **keys = teptris_arena_alloc(
            c->a, (c->n_memo + 1) * sizeof(*keys));
        for (size_t i = 0; i < c->n_memo; i++) keys[i] = c->memo[i].src;
        c->memo_hash = hash_build(c, keys, c->n_memo, &c->memo_hash_cap);
    }
    uint32_t mask = (uint32_t)c->memo_hash_cap - 1;
    size_t slot = ptr_hash(v) & mask;
    while (c->memo_hash[slot] != 0) slot = (slot + 1) & mask;
    c->memo_hash[slot] = (uint32_t)c->n_memo + 1;
}

static sch_node *memo_get(sctx *c, const jv *v, bool *found, bool *busy) {
    *found = false;
    *busy = false;
    size_t idx = hash_find(c->memo_hash, c->memo_hash_cap, v,
                           (const char *)c->memo, sizeof(*c->memo));
    if (idx == SIZE_MAX) return NULL;
    *found = true;
    *busy = c->memo[idx].in_progress;
    return c->memo[idx].node;
}

static void memo_put(sctx *c, const jv *v, sch_node *n) {
    if (c->n_memo == c->cap_memo) {
        c->cap_memo = c->cap_memo == 0 ? 16 : c->cap_memo * 2;
        void *nm = teptris_arena_alloc(c->a, c->cap_memo * sizeof(*c->memo));
        if (c->n_memo > 0) memcpy(nm, c->memo, c->n_memo * sizeof(*c->memo));
        c->memo = nm;
        c->memo_hash_cap = 0; /* keys moved: rebuild on the next put */
    }
    memo_hash_put(c, v);
    c->memo[c->n_memo].src = v;
    c->memo[c->n_memo].node = n;
    c->memo[c->n_memo].in_progress = true;
    c->n_memo++;
}

static void memo_done(sctx *c, const jv *v) {
    size_t idx = hash_find(c->memo_hash, c->memo_hash_cap, v,
                           (const char *)c->memo, sizeof(*c->memo));
    if (idx != SIZE_MAX) c->memo[idx].in_progress = false;
}

/* --------------------------------------------------- JSON Pointer -- */

/* resolve "#", "#/a/b", or "#anchor" to a jv subtree of the CURRENT
 * resource (a plain "#" names the resource the walk is inside, which
 * is the document root for single-resource schemas) */
static jv *resolve_local(sctx *c, const char *ref) {
    if (ref[0] != '#') return NULL;
    jv *root = c->cur_root != NULL ? c->cur_root : c->root;
    if (ref[1] == '\0') return root;
    if (ref[1] == '/') {
        /* JSON Pointer with ~0/~1 unescaping */
        jv *cur = root;
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
                } else if (p[i] == '%' && i + 2 < tlen) {
                    /* RFC 3986: refs percent-decode before the
                     * pointer walk (~1 decodes to '%', %25 to '%') */
                    static const char hex[] = "0123456789abcdef";
                    const char *hi = strchr(hex, (char)((p[i + 1] | 0x20)));
                    const char *lo = strchr(hex, (char)((p[i + 2] | 0x20)));
                    if (hi != NULL && lo != NULL) {
                        tok[o++] = (char)((hi - hex) * 16 + (lo - hex));
                        i += 2;
                    } else {
                        tok[o++] = p[i];
                    }
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
    /* plain-name fragment: an $anchor or a $dynamicAnchor (a plain
     * $ref to either behaves the same way) */
    for (size_t i = 0; i < c->n_anchors; i++) {
        if (strcmp(c->anchors[i].name, ref + 1) == 0) {
            return c->anchors[i].target;
        }
    }
    for (size_t i = 0; i < c->n_dyn_anchors; i++) {
        if (strcmp(c->dyn_anchors[i].name, ref + 1) == 0) {
            return c->dyn_anchors[i].target;
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

/* pre-register every $id in the document against its enclosing base:
 * references may point at resources the compile has not reached yet */
static void node_base_hash_put(sctx *c, const jv *v) {
    if (c->n_node_bases + 1 > c->node_base_hash_cap * 7 / 10) {
        const void **keys = teptris_arena_alloc(
            c->a, (c->n_node_bases + 1) * sizeof(*keys));
        for (size_t i = 0; i < c->n_node_bases; i++) {
            keys[i] = c->node_bases[i].node;
        }
        c->node_base_hash = hash_build(c, keys, c->n_node_bases,
                                       &c->node_base_hash_cap);
    }
    uint32_t mask = (uint32_t)c->node_base_hash_cap - 1;
    size_t slot = ptr_hash(v) & mask;
    while (c->node_base_hash[slot] != 0) slot = (slot + 1) & mask;
    c->node_base_hash[slot] = (uint32_t)c->n_node_bases + 1;
}

static void node_base_put(sctx *c, const jv *v, const char *abs,
                          const char *dir, jv *root, bool recursive) {
    if (c->n_node_bases == c->cap_node_bases) {
        c->cap_node_bases =
            c->cap_node_bases == 0 ? 64 : c->cap_node_bases * 2;
        void *nb = teptris_arena_alloc(
            c->a, c->cap_node_bases * sizeof(*c->node_bases));
        if (c->n_node_bases > 0) {
            memcpy(nb, c->node_bases, c->n_node_bases * sizeof(*c->node_bases));
        }
        c->node_bases = nb;
        c->node_base_hash_cap = 0; /* keys moved: rebuild on the next put */
    }
    node_base_hash_put(c, v);
    struct node_base *nb = &c->node_bases[c->n_node_bases++];
    nb->node = v;
    nb->abs = abs;
    nb->dir = dir;
    nb->root = root;
    nb->recursive = recursive;
}

static void collect_ids(sctx *c, jv *v, const char *base, const char *dir,
                        jv *res_root, bool res_recursive) {
    if (v == NULL) return;
    if (jv_kind_of(v) == JV_OBJECT) {
        /* compute this node's own base first: a $id applies to the
         * resource's own subtree, including its own $refs */
        bool recursive_root = res_recursive;
        jv *idv = jv_member_get(v, "$id");
        if (idv != NULL && jv_kind_of(idv) == JV_STRING) {
            const char *idtext = jv_str(idv);
            if (idtext[0] == '#') {
                /* a fragment-only $id aliases an anchor of the current
                 * resource (2019-09+: same as $anchor) */
                if (c->n_anchors == c->cap_anchors) {
                    c->cap_anchors =
                        c->cap_anchors == 0 ? 8 : c->cap_anchors * 2;
                    void *na = teptris_arena_alloc(
                        c->a, c->cap_anchors * sizeof(*c->anchors));
                    if (c->n_anchors > 0) {
                        memcpy(na, c->anchors,
                               c->n_anchors * sizeof(*c->anchors));
                    }
                    c->anchors = na;
                }
                c->anchors[c->n_anchors].name = idtext + 1;
                c->anchors[c->n_anchors].target = v;
                c->n_anchors++;
                /* the subtree still records with the current base:
                 * nested $ids and refs beneath it keep resolving */
            }
            char abs[TEPTRIS_URI_MAX];
            if (teptris_uri_join(base, idtext, abs, sizeof abs)) {
                char *held = teptris_arena_alloc(c->a, strlen(abs) + 1);
                strcpy(held, abs);
                base = held;
                res_root = v; /* a new resource boundary */
                /* does this resource root declare $recursiveAnchor
                 * true? (2019-09: it enables the dynamic $recursiveRef
                 * within the resource) */
                jv *ra = jv_member_get(v, "$recursiveAnchor");
                recursive_root = ra != NULL && jv_kind_of(ra) == JV_BOOL &&
                                 jv_bool(ra);
                if (strncmp(abs, "file://", 7) == 0) {
                    char path[TEPTRIS_URI_MAX];
                    teptris_file_uri_to_path(abs, path, sizeof path);
                    char *slash = strrchr(path, '/');
                    char *held_dir = teptris_arena_alloc(
                        c->a, (slash != NULL ? (size_t)(slash - path) : 0) + 2);
                    if (slash != NULL) {
                        size_t n = (size_t)(slash - path) + 1;
                        memcpy(held_dir, path, n);
                        held_dir[n] = '\0';
                    } else {
                        held_dir[0] = '\0';
                    }
                    dir = held_dir;
                }
            } else if ((strncmp(idtext, "http://", 7) == 0 ||
                        strncmp(idtext, "https://", 8) == 0)) {
                /* an http-space $id names an in-memory resource (the
                 * document itself or one of its subtrees): it registers
                 * under its own text and nested relative references
                 * merge against that space; nothing ever loads from the
                 * network */
                char *held_id = teptris_arena_alloc(c->a, strlen(idtext) + 1);
                strcpy(held_id, idtext);
                base = held_id;
                res_root = v;
                jv *ra = jv_member_get(v, "$recursiveAnchor");
                recursive_root = ra != NULL && jv_kind_of(ra) == JV_BOOL &&
                                 jv_bool(ra);
            }
        }
        node_base_put(c, v, base, dir, res_root, recursive_root);
        const char *next_base = base;
        const char *next_dir = dir;
        /* the resource this $id names is registered under the
         * already-joined base */
        if (idv != NULL && base[0] != '\0') {
            bool known = false;
            for (size_t i = 0; i < c->n_resources; i++) {
                if (strcmp(c->resources[i].abs, base) == 0) {
                    known = true;
                    break;
                }
            }
            if (!known && c->n_resources < 128) {
                if (c->n_resources == c->cap_resources) {
                    c->cap_resources =
                        c->cap_resources == 0 ? 8 : c->cap_resources * 2;
                    void *nr = teptris_arena_alloc(
                        c->a, c->cap_resources * sizeof(*c->resources));
                    if (c->n_resources > 0) {
                        memcpy(nr, c->resources,
                               c->n_resources * sizeof(*c->resources));
                    }
                    c->resources = nr;
                }
                teptris_resource *res = &c->resources[c->n_resources++];
                res->abs = base;
                res->root = v;
                res->node = NULL;
                res->recursive_anchor = recursive_root;
            }
        }
        for (size_t i = 0; i < jv_member_count(v); i++) {
            collect_ids(c, jv_member_val(v, i), next_base, next_dir, res_root,
                        recursive_root);
        }
    } else if (jv_kind_of(v) == JV_ARRAY) {
        for (size_t i = 0; i < jv_item_count(v); i++) {
            collect_ids(c, jv_item(v, i), base, dir, res_root, res_recursive);
        }
    }
}

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
            if (strcmp(k, "$dynamicAnchor") == 0 && jv_kind_of(val) == JV_STRING) {
                if (c->n_dyn_anchors == c->cap_dyn_anchors) {
                    c->cap_dyn_anchors =
                        c->cap_dyn_anchors == 0 ? 8 : c->cap_dyn_anchors * 2;
                    void *na = teptris_arena_alloc(
                        c->a, c->cap_dyn_anchors * sizeof(*c->dyn_anchors));
                    if (c->n_dyn_anchors > 0) {
                        memcpy(na, c->dyn_anchors,
                               c->n_dyn_anchors * sizeof(*c->dyn_anchors));
                    }
                    c->dyn_anchors = na;
                }
                c->dyn_anchors[c->n_dyn_anchors].name = jv_str(val);
                c->dyn_anchors[c->n_dyn_anchors].target = v;
                c->n_dyn_anchors++;
            }
            if (strcmp(k, "$recursiveAnchor") == 0 && jv_kind_of(val) == JV_BOOL &&
                jv_bool(val) && c->rec_anchor_target == NULL) {
                c->rec_anchor_target = v;
            }
            collect_anchors(c, val);
        }
    } else if (jv_kind_of(v) == JV_ARRAY) {
        for (size_t i = 0; i < jv_item_count(v); i++) {
            collect_anchors(c, jv_item(v, i));
        }
    }
}

/* ------------------------------------------------ self scope frames -- */

/* the anchors (dynamic or recursive) inside a schema's own subtree,
 * self included, in document order: applying the schema puts all of
 * them in dynamic scope, whether or not the walk reaches each one.
 * $defs bodies compile lazily, so the walk compiles what it needs. */
static void build_self_scope(sctx *c, const jv *v) {
    if (v == NULL) return;
    if (jv_kind_of(v) == JV_OBJECT) {
        bool found = false, busy = false;
        sch_node *cn = memo_get(c, v, &found, &busy);
        if (!found) cn = sch_compile_node(c, v);
        if (cn != NULL &&
            (cn->dynamic_anchor != NULL || cn->recursive_anchor != NULL)) {
            if (c->n_entry_acc == c->cap_entry_acc) {
                c->cap_entry_acc =
                    c->cap_entry_acc == 0 ? 8 : c->cap_entry_acc * 2;
                void *ne = teptris_arena_alloc(
                    c->a, c->cap_entry_acc * sizeof(*c->entry_acc));
                if (c->n_entry_acc > 0) {
                    memcpy(ne, c->entry_acc,
                           c->n_entry_acc * sizeof(*c->entry_acc));
                }
                c->entry_acc = ne;
            }
            c->entry_acc[c->n_entry_acc].name =
                cn->dynamic_anchor != NULL ? cn->dynamic_anchor : "";
            c->entry_acc[c->n_entry_acc].node = cn;
            c->entry_acc[c->n_entry_acc].res = cn->res_id;
            c->n_entry_acc++;
        }
        for (size_t i = 0; i < jv_member_count(v); i++) {
            build_self_scope(c, jv_member_val(v, i));
        }
    } else if (jv_kind_of(v) == JV_ARRAY) {
        for (size_t i = 0; i < jv_item_count(v); i++) {
            build_self_scope(c, jv_item(v, i));
        }
    }
}

static void attach_self_scope(sctx *c, sch_node *owner, const jv *self) {
    /* the walk may compile a lazily-seen $defs body whose own attach
     * re-enters this function: my entries start at the base and the
     * accumulator is handed back to my caller untouched */
    size_t base = c->n_entry_acc;
    build_self_scope(c, self);
    size_t mine = c->n_entry_acc - base;
    if (mine == 0) return;
    owner->self_scope =
        teptris_arena_alloc(c->a, mine * sizeof(*owner->self_scope));
    memcpy(owner->self_scope, &c->entry_acc[base],
           mine * sizeof(*owner->self_scope));
    owner->n_self_scope = mine;
    c->n_entry_acc = base;
}

/* the first $dynamicAnchor with this name inside the CURRENT resource
 * (the bookend: a fragment-only $dynamicRef initially resolves here) */
static jv *resource_local_anchor(sctx *c, const char *name) {
    jv *root = c->cur_root != NULL ? c->cur_root : c->root;
    if (root == NULL || jv_kind_of(root) != JV_OBJECT) return NULL;
    size_t base = c->n_entry_acc;
    build_self_scope(c, root);
    size_t mine = c->n_entry_acc - base;
    for (size_t i = 0; i < mine; i++) {
        if (strcmp(c->entry_acc[base + i].name, name) == 0) {
            /* recover the anchor's jv through the compiled node: the
             * walk was document order, so the first match is the
             * resource-local initial resolution */
            for (size_t m = 0; m < c->n_memo; m++) {
                if (c->memo[m].node == c->entry_acc[base + i].node) {
                    return (jv *)c->memo[m].src;
                }
            }
            return NULL;
        }
    }
    c->n_entry_acc = base;
    return NULL;
}

/* ----------------------------------------------------------- compile -- */

static int type_bit(const char *name) {
    if (strcmp(name, "string") == 0) return ST_STRING;
    if (strcmp(name, "integer") == 0) return ST_INTEGER;
    if (strcmp(name, "number") == 0) return ST_NUMBER;
    if (strcmp(name, "boolean") == 0) return ST_BOOLEAN;
    if (strcmp(name, "object") == 0) return ST_OBJECT;
    if (strcmp(name, "array") == 0) return ST_ARRAY;
    if (strcmp(name, "null") == 0) return ST_NULL;
    return 0;
}

static int format_kind(const char *name) {
    if (strcmp(name, "date-time") == 0) return FMT_DATETIME;
    if (strcmp(name, "date") == 0) return FMT_DATE;
    if (strcmp(name, "time") == 0) return FMT_TIME;
    if (strcmp(name, "date-time-local") == 0 ||
        strcmp(name, "partial-date-time") == 0) {
        return FMT_DATETIME_LOCAL;
    }
    if (strcmp(name, "time-local") == 0 ||
        strcmp(name, "partial-time") == 0) {
        return FMT_TIME_LOCAL;
    }
    if (strcmp(name, "email") == 0) return FMT_EMAIL;
    if (strcmp(name, "idn-email") == 0) return FMT_IDN_EMAIL;
    if (strcmp(name, "hostname") == 0) return FMT_HOSTNAME;
    if (strcmp(name, "idn-hostname") == 0) return FMT_IDN_HOSTNAME;
    if (strcmp(name, "ipv4") == 0) return FMT_IPV4;
    if (strcmp(name, "ipv6") == 0) return FMT_IPV6;
    if (strcmp(name, "uri") == 0) return FMT_URI;
    if (strcmp(name, "iri") == 0) return FMT_IRI;
    if (strcmp(name, "uri-reference") == 0) return FMT_URI_REFERENCE;
    if (strcmp(name, "iri-reference") == 0) return FMT_IRI_REFERENCE;
    if (strcmp(name, "json-pointer") == 0) return FMT_JSON_POINTER;
    if (strcmp(name, "relative-json-pointer") == 0)
        return FMT_RELATIVE_JSON_POINTER;
    if (strcmp(name, "regex") == 0) return FMT_REGEX;
    if (strcmp(name, "uri-template") == 0) return FMT_URI_TEMPLATE;
    if (strcmp(name, "uuid") == 0) return FMT_UUID;
    if (strcmp(name, "duration") == 0) return FMT_DURATION;
    if (strcmp(name, "semver") == 0) return FMT_SEMVER;
    if (strcmp(name, "semver-requirement") == 0) return FMT_SEMVER_REQUIREMENT;
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

/* the suite writes count constraints as decimals ("maxLength": 3.0):
 * a JSON number is valid there, so accept float tokens whose value is
 * integral (a non-integral count cannot be satisfied; ignore it like
 * an annotation would lie - reject the schema instead) */
static bool count_value(sctx *c, jv *val, long *out) {
    if (jv_kind_of(val) == JV_INT) {
        *out = (long)jv_int(val);
        return true;
    }
    if (jv_kind_of(val) == JV_NUM) {
        double d = jv_num(val);
        if (d != d || d != (double)(long)d) {
            sfail(c, "count constraint must be an integer", NULL);
            return false;
        }
        *out = (long)d;
        return true;
    }
    sfail(c, "count constraint must be an integer", NULL);
    return false;
}

static bool grow_dep_list(sctx *c, sch_kschema **list, size_t *n, size_t *cap) {
    if (*n == *cap) {
        *cap = *cap == 0 ? 4 : *cap * 2;
        sch_kschema *nl =
            teptris_arena_alloc(c->a, *cap * sizeof(**list));
        if (*n > 0) memcpy(nl, *list, *n * sizeof(**list));
        *list = nl;
    }
    return true;
}

/* one "key": [names...] pair (dependentRequired, or dependencies'
 * array form): the fake node reuses the required storage shape */
static bool sch_compile_dep_req(sctx *c, sch_node *n, const char *key,
                                jv *list) {
    grow_dep_list(c, &n->dep_req, &n->n_dep_req, &c->cap_dep_req);
    sch_node *fake = sch_node_new(c);
    size_t dn = jv_item_count(list);
    fake->required = teptris_arena_alloc(c->a, (dn ? dn : 1) * sizeof(char *));
    for (size_t d = 0; d < dn; d++) {
        jv *e = jv_item(list, d);
        if (jv_kind_of(e) != JV_STRING) {
            sfail(c, "dependency entries must be strings", key);
            return false;
        }
        size_t slen = jv_str_len(e);
        char *rb = teptris_arena_alloc(c->a, slen + 1);
        memcpy(rb, jv_str(e), slen);
        rb[slen] = '\0';
        fake->required[d] = rb;
    }
    fake->n_req = dn;
    n->dep_req[n->n_dep_req].key = key;
    n->dep_req[n->n_dep_req].schema = fake;
    n->n_dep_req++;
    return true;
}

/* one "key": {schema} pair (dependentSchemas, or dependencies'
 * schema form; a boolean is a schema) */
static bool sch_compile_dep_schema(sctx *c, sch_node *n, const char *key,
                                   jv *dv) {
    grow_dep_list(c, &n->dep_schemas, &n->n_dep_schemas,
                  &c->cap_dep_schemas);
    sch_node *s = sch_compile_node(c, dv);
    if (s == NULL && c->err[0] != '\0') return false;
    n->dep_schemas[n->n_dep_schemas].key = key;
    n->dep_schemas[n->n_dep_schemas].schema = s;
    n->n_dep_schemas++;
    return true;
}

/* resolve a non-fragment $ref against the current resource's base:
 * the document part names a resource (file-backed), the fragment
 * names a subtree within it. NULL with *remote set when the target
 * is a network URI the library must not touch. */
/* a builtin resource (the vendored metaschemas): parse once, register
 * in the resource table, collect its anchors so its own refs resolve */
static jv *load_builtin_resource(sctx *c, const char *uri,
                                 const char *json) {
    size_t len = strlen(json);
    char jerr[160] = {0};
    jv *root = json_read(c->a, json, len, jerr, sizeof jerr);
    if (root == NULL) return NULL;

    if (c->n_resources == c->cap_resources) {
        c->cap_resources = c->cap_resources == 0 ? 8 : c->cap_resources * 2;
        void *nr = teptris_arena_alloc(
            c->a, c->cap_resources * sizeof(*c->resources));
        if (c->n_resources > 0) {
            memcpy(nr, c->resources, c->n_resources * sizeof(*c->resources));
        }
        c->resources = nr;
    }
    teptris_resource *res = &c->resources[c->n_resources++];
    res->abs = teptris_arena_alloc(c->a, strlen(uri) + 1);
    strcpy((char *)res->abs, uri);
    res->root = root;
    res->node = NULL;
    jv *ra = jv_kind_of(root) == JV_OBJECT
                 ? jv_member_get(root, "$recursiveAnchor")
                 : NULL;
    res->recursive_anchor =
        ra != NULL && jv_kind_of(ra) == JV_BOOL && jv_bool(ra);
    collect_anchors(c, root);
    bool recursive = res->recursive_anchor;
    collect_ids(c, root, uri, NULL, root, recursive);
    return root;
}

static jv *resolve_uri_ref(sctx *c, const char *r, bool *remote);

static jv *find_builtin_resource(sctx *c, const char *uri) {
    for (size_t i = 0; i < teptris_n_builtin_resources; i++) {
        if (strcmp(teptris_builtin_resources[i].uri, uri) == 0) {
            return load_builtin_resource(c, uri,
                                         teptris_builtin_resources[i].json);
        }
    }
    return NULL;
}

/* the dialect's vocabulary mask: a custom metaschema's $vocabulary
 * decides which standard vocabularies the schema uses (8.1.2); an
 * unrecognized or unresolvable $schema keeps all of them */
static void detect_vocab(sctx *c, jv *root) {
    c->vocab_mask = TEPTRIS_VOC_ALL;
    if (jv_kind_of(root) != JV_OBJECT) return;
    jv *sv = jv_member_get(root, "$schema");
    if (sv == NULL || jv_kind_of(sv) != JV_STRING) return;
    const char *s = jv_str(sv);
    if (strstr(s, "draft-04/") != NULL || strstr(s, "draft-06/") != NULL ||
        strstr(s, "draft-07/") != NULL ||
        strstr(s, "/draft/2019-09/schema") != NULL ||
        strstr(s, "/draft/2020-12/schema") != NULL) {
        return; /* a standard dialect: every vocabulary */
    }
    bool remote = false;
    jv *meta = resolve_uri_ref(c, s, &remote);
    if (meta == NULL || jv_kind_of(meta) != JV_OBJECT) return;
    jv *voc = jv_member_get(meta, "$vocabulary");
    if (voc == NULL || jv_kind_of(voc) != JV_OBJECT) return;
    /* only listed vocabularies are in the dialect; core is mandatory */
    c->vocab_mask = 0;
    for (size_t i = 0; i < jv_member_count(voc); i++) {
        const char *uri = jv_member_key(voc, i);
        jv *req = jv_member_val(voc, i);
        if (req == NULL || jv_kind_of(req) != JV_BOOL || !jv_bool(req)) {
            continue;
        }
        unsigned bit = 0;
        if (strstr(uri, "/vocab/applicator") != NULL) {
            bit = TEPTRIS_VOC_APPLICATOR;
        } else if (strstr(uri, "/vocab/unevaluated") != NULL) {
            bit = TEPTRIS_VOC_UNEVALUATED;
        } else if (strstr(uri, "/vocab/validation") != NULL) {
            bit = TEPTRIS_VOC_VALIDATION;
        } else if (strstr(uri, "/vocab/format") != NULL) {
            bit = TEPTRIS_VOC_FORMAT;
        } else if (strstr(uri, "/vocab/content") != NULL) {
            bit = TEPTRIS_VOC_CONTENT;
        }
        c->vocab_mask |= bit;
    }
}

/* the vocabulary a keyword belongs to (0 = core or unknown: ungated) */
static unsigned keyword_vocab(const char *k) {
    static const struct { const char *name; unsigned bit; } tbl[] = {
        {"allOf", TEPTRIS_VOC_APPLICATOR},
        {"anyOf", TEPTRIS_VOC_APPLICATOR},
        {"oneOf", TEPTRIS_VOC_APPLICATOR},
        {"not", TEPTRIS_VOC_APPLICATOR},
        {"if", TEPTRIS_VOC_APPLICATOR},
        {"then", TEPTRIS_VOC_APPLICATOR},
        {"else", TEPTRIS_VOC_APPLICATOR},
        {"properties", TEPTRIS_VOC_APPLICATOR},
        {"patternProperties", TEPTRIS_VOC_APPLICATOR},
        {"additionalProperties", TEPTRIS_VOC_APPLICATOR},
        {"additionalItems", TEPTRIS_VOC_APPLICATOR},
        {"propertyNames", TEPTRIS_VOC_APPLICATOR},
        {"items", TEPTRIS_VOC_APPLICATOR},
        {"prefixItems", TEPTRIS_VOC_APPLICATOR},
        {"contains", TEPTRIS_VOC_APPLICATOR},
        {"dependentSchemas", TEPTRIS_VOC_APPLICATOR},
        {"unevaluatedProperties", TEPTRIS_VOC_UNEVALUATED},
        {"unevaluatedItems", TEPTRIS_VOC_UNEVALUATED},
        {"type", TEPTRIS_VOC_VALIDATION},
        {"enum", TEPTRIS_VOC_VALIDATION},
        {"const", TEPTRIS_VOC_VALIDATION},
        {"multipleOf", TEPTRIS_VOC_VALIDATION},
        {"maximum", TEPTRIS_VOC_VALIDATION},
        {"exclusiveMaximum", TEPTRIS_VOC_VALIDATION},
        {"minimum", TEPTRIS_VOC_VALIDATION},
        {"exclusiveMinimum", TEPTRIS_VOC_VALIDATION},
        {"maxLength", TEPTRIS_VOC_VALIDATION},
        {"minLength", TEPTRIS_VOC_VALIDATION},
        {"pattern", TEPTRIS_VOC_VALIDATION},
        {"maxItems", TEPTRIS_VOC_VALIDATION},
        {"minItems", TEPTRIS_VOC_VALIDATION},
        {"uniqueItems", TEPTRIS_VOC_VALIDATION},
        {"maxContains", TEPTRIS_VOC_VALIDATION},
        {"minContains", TEPTRIS_VOC_VALIDATION},
        {"maxProperties", TEPTRIS_VOC_VALIDATION},
        {"minProperties", TEPTRIS_VOC_VALIDATION},
        {"required", TEPTRIS_VOC_VALIDATION},
        {"dependentRequired", TEPTRIS_VOC_VALIDATION},
        {"dependentExclusive", TEPTRIS_VOC_VALIDATION},
        {"format", TEPTRIS_VOC_FORMAT},
        {"contentEncoding", TEPTRIS_VOC_CONTENT},
        {"contentMediaType", TEPTRIS_VOC_CONTENT},
        {"contentSchema", TEPTRIS_VOC_CONTENT},
    };
    for (size_t i = 0; i < sizeof tbl / sizeof tbl[0]; i++) {
        if (strcmp(tbl[i].name, k) == 0) return tbl[i].bit;
    }
    return 0;
}

static jv *resolve_uri_ref(sctx *c, const char *r, bool *remote) {
    *remote = false;
    char doc[TEPTRIS_URI_MAX];
    char frag[TEPTRIS_URI_MAX];
    teptris_uri_split(r, doc, sizeof doc, frag, sizeof frag);
    if (doc[0] == '\0') {
        /* a fragment-only reference names a subtree of the current
         * resource (a $id like "#anchor" aliases an anchor) */
        char refbuf[TEPTRIS_URI_MAX];
        snprintf(refbuf, sizeof refbuf, "#%s", frag);
        return resolve_local(c, refbuf);
    }
    /* the vendored metaschemas resolve from the built-in table: a
     * parser library never touches the network, and these URIs are
     * part of the dialect itself */
    {
        jv *builtin = find_builtin_resource(c, doc);
        if (builtin != NULL) return builtin;
    }
    /* network targets are rejected outright, with or without a base */
    if (strncmp(doc, "http://", 7) == 0 || strncmp(doc, "https://", 8) == 0) {
        *remote = true;
        return NULL;
    }
    /* candidate bases: the base URI in effect, then the resource's
     * file directory (a remote $id naming a file the compile loaded);
     * each candidate tries the resource table, then the filesystem.
     * Relative references join whichever base is fs-backed when the
     * base URI itself is not. */
    char abs[TEPTRIS_URI_MAX];
    jv *root = NULL;
    const char *bases[2];
    size_t n_bases = 0;
    bases[n_bases++] = c->cur_abs;
    if (c->cur_dir != NULL && c->cur_dir[0] != '\0') {
        bases[n_bases++] = c->cur_dir;
    }
    for (size_t bi = 0; bi < n_bases && root == NULL; bi++) {
        const char *base = bases[bi];
        if (base == NULL || base[0] == '\0') continue;
        if (!teptris_uri_join(base, doc, abs, sizeof abs)) {
            if (bi == n_bases - 1) {
                *remote = true;
                return NULL;
            }
            continue;
        }
        for (size_t i = 0; i < c->n_resources; i++) {
            if (strcmp(c->resources[i].abs, abs) == 0) {
                root = c->resources[i].root;
                break;
            }
        }
        if (root == NULL) root = find_builtin_resource(c, abs);
        if (root == NULL && abs[0] != '\0' &&
            strncmp(abs, "http://", 7) != 0 &&
            strncmp(abs, "https://", 8) != 0 && strncmp(abs, "urn:", 4) != 0) {
            root = sch_load_resource(c, abs);
            if (root != NULL) {
                /* the loaded file joins the resource model: its own
                 * $ids register and its nodes record bases so its
                 * internal refs resolve within it */
                char path[TEPTRIS_URI_MAX];
                teptris_file_uri_to_path(abs, path, sizeof path);
                char *slash = strrchr(path, '/');
                char dirbuf[TEPTRIS_URI_MAX];
                if (slash != NULL) {
                    size_t n = (size_t)(slash - path) + 1;
                    if (n < sizeof dirbuf) {
                        memcpy(dirbuf, path, n);
                        dirbuf[n] = '\0';
                    } else {
                        dirbuf[0] = '\0';
                    }
                } else {
                    dirbuf[0] = '\0';
                }
                char *held = teptris_arena_alloc(c->a, strlen(dirbuf) + 1);
                strcpy(held, dirbuf);
                char abs_held[TEPTRIS_URI_MAX];
                snprintf(abs_held, sizeof abs_held, "%s", abs);
                char *held_abs = teptris_arena_alloc(c->a, strlen(abs_held) + 1);
                strcpy(held_abs, abs_held);
                bool loaded_recursive = false;
                if (jv_kind_of(root) == JV_OBJECT) {
                    jv *ra = jv_member_get(root, "$recursiveAnchor");
                    loaded_recursive = ra != NULL &&
                                       jv_kind_of(ra) == JV_BOOL && jv_bool(ra);
                }
                collect_ids(c, root, held_abs, held, root, loaded_recursive);
            }
        }
    }
    if (root == NULL) return NULL;
    if (frag[0] == '\0') return root;
    if (frag[0] == '/') {
        /* JSON Pointer into the loaded resource */
        const char *saved = c->cur_abs;
        jv *saved_root = c->cur_root;
        char refbuf[TEPTRIS_URI_MAX];
        snprintf(refbuf, sizeof refbuf, "#%s", frag);
        c->cur_root = root;
        jv *hit = resolve_local(c, refbuf);
        c->cur_root = saved_root;
        c->cur_abs = saved;
        return hit;
    }
    /* plain-name fragment inside the loaded resource: an $anchor, or
     * for a $dynamicRef the resource's own $dynamicAnchor */
    for (size_t i = 0; i < c->n_anchors; i++) {
        if (strcmp(c->anchors[i].name, frag) == 0) {
            return c->anchors[i].target;
        }
    }
    for (size_t i = 0; i < c->n_dyn_anchors; i++) {
        if (strcmp(c->dyn_anchors[i].name, frag) == 0) {
            return c->dyn_anchors[i].target;
        }
    }
    return NULL;
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

    /* the pre-pass recorded this node's base and resource root: a
     * pointer can reach a subtree whose $id chain the document walk
     * never passes through at compile time */
    const char *saved_abs = c->cur_abs;
    jv *saved_root = c->cur_root;
    const char *saved_dir = c->cur_dir;
    {
        size_t idx = hash_find(c->node_base_hash, c->node_base_hash_cap, v,
                               (const char *)c->node_bases,
                               sizeof(*c->node_bases));
        if (idx != SIZE_MAX) {
            if (c->node_bases[idx].root != saved_root) {
                c->cur_root = c->node_bases[idx].root;
            }
            c->cur_abs = c->node_bases[idx].abs;
            c->cur_dir = c->node_bases[idx].dir;
        }
    }
    n->res_id = c->cur_root != NULL ? c->cur_root : c->root;

    for (size_t i = 0; i < jv_member_count(v); i++) {
        const char *k = jv_member_key(v, i);
        jv *val = jv_member_val(v, i);

        unsigned kgate = keyword_vocab(k);
        if (kgate != 0 && (c->vocab_mask & kgate) == 0) {
            /* the dialect dropped this vocabulary: the keyword is an
             * annotation (8.1.2) */
            continue;
        }

        if (strcmp(k, "type") == 0) {
            if (!compile_type(c, n, val, k)) return NULL;
        } else if (strcmp(k, "$ref") == 0) {
            if (jv_kind_of(val) != JV_STRING) {
                sfail(c, "$ref must be a string", k);
                return NULL;
            }
            const char *r = jv_str(val);
            if (r[0] == '#') {
                n->ref = sch_resolve_pointer(c, r);
                if (n->ref == NULL) {
                    sfail(c, "unresolvable $ref", r);
                    return NULL;
                }
            } else {
                bool remote = false;
                jv *target = resolve_uri_ref(c, r, &remote);
                if (remote) {
                    sfail(c, "remote $ref is not supported (a parser library never touches the network)", r);
                    return NULL;
                }
                if (target == NULL) {
                    sfail(c, "unresolvable $ref", r);
                    return NULL;
                }
                n->ref = sch_compile_node(c, target);
            }
            if (n->ref == NULL && c->err[0] != '\0') return NULL;
            /* draft 07 and older: a $ref ignores every sibling */
            n->ref_only = c->ref_exclusive;
        } else if (strcmp(k, "enum") == 0) {
            if (jv_kind_of(val) != JV_ARRAY) {
                sfail(c, "enum must be an array", k);
                return NULL;
            }
            n->enum_vals = teptris_arena_alloc(
                c->a, (jv_item_count(val) ? jv_item_count(val) : 1) * sizeof(jv *));
            n->n_enum = jv_item_count(val);
            n->enum_empty = jv_item_count(val) == 0;
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
        } else if (strcmp(k, "unevaluatedProperties") == 0) {
            n->uneval_props = sch_compile_node(c, val);
            if (n->uneval_props == NULL && c->err[0] != '\0') return NULL;
        } else if (strcmp(k, "unevaluatedItems") == 0) {
            n->uneval_items = sch_compile_node(c, val);
            if (n->uneval_items == NULL && c->err[0] != '\0') return NULL;
        } else if (strcmp(k, "$dynamicAnchor") == 0 &&
                   jv_kind_of(val) == JV_STRING) {
            n->dynamic_anchor = jv_str(val);
        } else if (strcmp(k, "$recursiveAnchor") == 0 &&
                   jv_kind_of(val) == JV_BOOL && jv_bool(val)) {
            n->recursive_anchor = "";
        } else if (strcmp(k, "$dynamicRef") == 0 ||
                   strcmp(k, "$recursiveRef") == 0) {
            if (jv_kind_of(val) != JV_STRING) {
                sfail(c, "$dynamicRef/$recursiveRef must be a string", k);
                return NULL;
            }
            const char *r = jv_str(val);
            bool recursive = strcmp(k, "$recursiveRef") == 0;
            /* $recursiveRef is always the bare "#"; a $dynamicRef may
             * also name a URI or pointer ("#/pointer", "other.json",
             * "main.json#/$defs/inner"): none of those forms
             * participate in dynamic scope resolution - the spec and
             * the suite both read them exactly like a plain $ref - so
             * only the plain-name fragment form reaches the scope
             * machinery */
            if (r[0] != '#' || (recursive && r[1] != '\0') ||
                (!recursive && r[1] == '\0')) {
                bool remote = false;
                jv *target = NULL;
                if (r[0] != '#') {
                    target = resolve_uri_ref(c, r, &remote);
                } else {
                    sfail(c,
                          "dynamic $refs are plain-name fragments "
                          "(\"#anchor\") or pointers (\"#/a/b\")",
                          r);
                    return NULL;
                }
                if (remote) {
                    sfail(c, "remote dynamic $ref is not supported", r);
                    return NULL;
                }
                if (target == NULL) {
                    sfail(c, "unresolvable dynamic $ref", r);
                    return NULL;
                }
                n->ref = sch_compile_node(c, target);
                if (n->ref == NULL && c->err[0] != '\0') return NULL;
                continue;
            }
            if (!recursive && strchr(r + 1, '/') != NULL) {
                n->ref = sch_resolve_pointer(c, r);
                if (n->ref == NULL) {
                    sfail(c, "unresolvable $dynamicRef", r);
                    return NULL;
                }
                continue;
            }
            /* the static fallback: the doc-wide anchor collection;
             * the innermost scope wins at validation time */
            jv *target = NULL;
            if (recursive) {
                /* 2019-09: when the ref's own resource carries a true
                 * $recursiveAnchor the resolution is dynamic (the first
                 * anchor entered wins at walk time); otherwise the ref
                 * does not join the dynamic machinery at all and reads
                 * as a plain reference to the resource's root */
                bool dyn = false;
                for (size_t r2 = 0; r2 < c->n_resources; r2++) {
                    if (c->resources[r2].root == c->cur_root &&
                        c->resources[r2].recursive_anchor) {
                        dyn = true;
                    }
                }
                if (dyn) {
                    target = c->rec_anchor_target != NULL
                                 ? c->rec_anchor_target
                                 : (c->cur_root != NULL ? c->cur_root
                                                        : c->root);
                } else {
                    n->ref = sch_compile_node(
                        c, c->cur_root != NULL ? c->cur_root : c->root);
                    if (n->ref == NULL && c->err[0] != '\0') return NULL;
                    continue;
                }
            } else {
                /* a fragment-only $dynamicRef initially resolves to
                 * the matching $dynamicAnchor in the CURRENT resource
                 * (the bookend); the doc-wide table is the fallback */
                target = resource_local_anchor(c, r + 1);
                for (size_t d = 0; d < c->n_dyn_anchors && target == NULL;
                     d++) {
                    if (strcmp(c->dyn_anchors[d].name, r + 1) == 0) {
                        target = c->dyn_anchors[d].target;
                    }
                }
                if (target == NULL) {
                    sfail(c, "unresolvable $dynamicRef (no matching "
                             "$dynamicAnchor)",
                          r);
                    return NULL;
                }
            }
            n->dynamic_ref = jv_str(val);
            n->dynamic_ref_target = sch_compile_node(c, target);
            if (n->dynamic_ref_target == NULL && c->err[0] != '\0') {
                return NULL;
            }
            n->dynamic_recursive = recursive;
        } else if (strcmp(k, "contentSchema") == 0) {
            if (!c->dialect_declared) {
                n->content_schema = sch_compile_node(c, val);
                if (n->content_schema == NULL && c->err[0] != '\0') return NULL;
            }
            /* annotation under an explicit dialect (2020-12 content
             * vocabulary: assertions SHOULD NOT be made) */
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
        } else if (strcmp(k, "minProperties") == 0) {
            if (!count_value(c, val, &n->min_props)) return NULL;
            n->has_min_props = true;
        } else if (strcmp(k, "maxProperties") == 0) {
            if (!count_value(c, val, &n->max_props)) return NULL;
            n->has_max_props = true;
        } else if (strcmp(k, "dependencies") == 0 &&
                   jv_kind_of(val) == JV_OBJECT) {
            /* draft-07 dependencies: an array value is a
             * dependentRequired pair, a schema value a
             * dependentSchemas pair */
            size_t cnt = jv_member_count(val);
            for (size_t p = 0; p < cnt; p++) {
                jv *dv = jv_member_val(val, p);
                if (jv_kind_of(dv) == JV_ARRAY) {
                    if (!sch_compile_dep_req(c, n, jv_member_key(val, p), dv))
                        return NULL;
                } else if (jv_kind_of(dv) == JV_OBJECT ||
                           jv_kind_of(dv) == JV_BOOL) {
                    if (!sch_compile_dep_schema(c, n, jv_member_key(val, p),
                                                dv))
                        return NULL;
                } else {
                    sfail(c, "dependencies values must be arrays or schemas",
                          k);
                    return NULL;
                }
            }
        } else if (strcmp(k, "dependentRequired") == 0 &&
                   jv_kind_of(val) == JV_OBJECT) {
            size_t cnt = jv_member_count(val);
            for (size_t p = 0; p < cnt; p++) {
                jv *list = jv_member_val(val, p);
                if (jv_kind_of(list) != JV_ARRAY) {
                    sfail(c, "dependentRequired values must be arrays", k);
                    return NULL;
                }
                if (!sch_compile_dep_req(c, n, jv_member_key(val, p), list))
                    return NULL;
            }
        } else if (strcmp(k, "dependentSchemas") == 0 &&
                   jv_kind_of(val) == JV_OBJECT) {
            size_t cnt = jv_member_count(val);
            for (size_t p = 0; p < cnt; p++) {
                if (!sch_compile_dep_schema(c, n, jv_member_key(val, p),
                                            jv_member_val(val, p)))
                    return NULL;
            }
        } else if (strcmp(k, "contentEncoding") == 0 &&
                   jv_kind_of(val) == JV_STRING) {
            if (!c->dialect_declared) n->content_encoding = jv_str(val);
        } else if (strcmp(k, "contentMediaType") == 0 &&
                   jv_kind_of(val) == JV_STRING) {
            if (!c->dialect_declared) n->content_media_type = jv_str(val);
        } else if (strcmp(k, "items") == 0 && jv_kind_of(val) == JV_ARRAY) {
            /* draft-07 tuple form: positional prefixes; the union
             * reads them exactly like prefixItems, with
             * additionalItems covering anything beyond */
            size_t cnt = jv_item_count(val);
            size_t base = n->n_prefix;
            size_t total = base + cnt;
            sch_node **np = teptris_arena_alloc(
                c->a, (total ? total : 1) * sizeof(sch_node *));
            if (base > 0) memcpy(np, n->prefix, base * sizeof(sch_node *));
            for (size_t p = 0; p < cnt; p++) {
                np[base + p] = sch_compile_node(c, jv_item(val, p));
                if (np[base + p] == NULL && c->err[0] != '\0') return NULL;
            }
            n->prefix = np;
            n->n_prefix = total;
        } else if (strcmp(k, "items") == 0) {
            n->items = sch_compile_node(c, val);
            if (n->items == NULL && c->err[0] != '\0') return NULL;
            n->items_set = true;
        } else if (strcmp(k, "additionalItems") == 0) {
            n->add_items = sch_compile_node(c, val);
            if (n->add_items == NULL && c->err[0] != '\0') return NULL;
            n->add_items_set = true;
        } else if (strcmp(k, "prefixItems") == 0 &&
                   jv_kind_of(val) == JV_ARRAY) {
            size_t cnt = jv_item_count(val);
            size_t base = n->n_prefix;
            size_t total = base + cnt;
            sch_node **np = teptris_arena_alloc(
                c->a, (total ? total : 1) * sizeof(sch_node *));
            if (base > 0) memcpy(np, n->prefix, base * sizeof(sch_node *));
            for (size_t p = 0; p < cnt; p++) {
                np[base + p] = sch_compile_node(c, jv_item(val, p));
                if (np[base + p] == NULL && c->err[0] != '\0') return NULL;
            }
            n->prefix = np;
            n->n_prefix = total;
        } else if (strcmp(k, "contains") == 0) {
            n->contains = sch_compile_node(c, val);
            if (n->contains == NULL && c->err[0] != '\0') return NULL;
            n->has_contains = true;
        } else if (strcmp(k, "minContains") == 0) {
            if (!count_value(c, val, &n->min_contains)) return NULL;
            n->has_min_contains = true;
        } else if (strcmp(k, "maxContains") == 0) {
            if (!count_value(c, val, &n->max_contains)) return NULL;
            n->has_max_contains = true;
        } else if (strcmp(k, "uniqueItems") == 0 && jv_kind_of(val) == JV_BOOL) {
            n->unique_items = jv_bool(val);
            n->has_unique = true;
        } else if (strcmp(k, "minItems") == 0) {
            if (!count_value(c, val, &n->min_items)) return NULL;
            n->has_min_items = true;
        } else if (strcmp(k, "maxItems") == 0) {
            if (!count_value(c, val, &n->max_items)) return NULL;
            n->has_max_items = true;
        } else if (strcmp(k, "pattern") == 0 && jv_kind_of(val) == JV_STRING) {
            char rerr[128] = {0};
            n->pattern = sch_regex_compile(c->a, jv_str(val), jv_str_len(val),
                                           rerr, sizeof rerr);
            if (n->pattern == NULL) {
                sfail(c, rerr[0] != '\0' ? rerr : "invalid pattern", k);
                return NULL;
            }
        } else if (strcmp(k, "minLength") == 0) {
            if (!count_value(c, val, &n->min_length)) return NULL;
            n->has_min_length = true;
        } else if (strcmp(k, "maxLength") == 0) {
            if (!count_value(c, val, &n->max_length)) return NULL;
            n->has_max_length = true;
        } else if (strcmp(k, "format") == 0 && jv_kind_of(val) == JV_STRING) {
            if (!c->dialect_declared) {
                n->format = format_kind(jv_str(val));
            }
            /* an explicit dialect keeps the spec's annotation-only
             * default: the keyword rides the ignored tail */
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
            } else if (sch_decimal_of_double(m, &n->mult_mant,
                                             &n->mult_exp)) {
                n->mult_exact = true;
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
    c->cur_abs = saved_abs;
    c->cur_root = saved_root;
    c->cur_dir = saved_dir;
    attach_self_scope(c, n, v);
    memo_done(c, v);
    return n;
}

/* ------------------------------------------------------------ public -- */

/* the shortest decimal text that round-trips through d, read back as
 * mant * 10^exp (the same decimal view serde_json/fraction-style
 * exact arithmetic effectively uses) */
bool sch_decimal_of_double(double d, int64_t *mant, int *exp) {
    if (!(d == d) || d == INFINITY || d == -INFINITY) return false;
    char buf[64];
    for (int prec = 1; prec <= 17; prec++) {
        snprintf(buf, sizeof buf, "%.*g", prec, d);
        if (strtod(buf, NULL) == d) break;
    }
    /* [+-] D[.DDD] [e[+-]NN] */
    const char *p = buf;
    if (*p == '+' || *p == '-') p++;
    int64_t m = 0;
    int e = 0;
    for (; *p >= '0' && *p <= '9'; p++) m = m * 10 + (*p - '0');
    if (*p == '.') {
        for (p++; *p >= '0' && *p <= '9'; p++) {
            m = m * 10 + (*p - '0');
            e--;
        }
    }
    if (*p == 'e' || *p == 'E') {
        e += (int)strtol(p + 1, NULL, 10);
    }
    *mant = m;
    *exp = e;
    return true;
}

static teptris_status schema_compile_core(const char *json, size_t len,
                                          const char *base_dir, size_t base_len,
                                          struct teptris_schema **out,
                                          char **diag_buf, size_t *diag_len);

TEPTRIS_API teptris_status teptris_schema_compile_ex(
    const char *json, size_t len, const char *base_dir, size_t base_len,
    struct teptris_schema **out, char **diag_buf, size_t *diag_len) {
    return schema_compile_core(json, len, base_dir, base_len, out, diag_buf,
                               diag_len);
}

TEPTRIS_API teptris_status teptris_schema_compile(const char *json, size_t len,
                                                  struct teptris_schema **out,
                                                  char **diag_buf,
                                                  size_t *diag_len) {
    return schema_compile_core(json, len, NULL, 0, out, diag_buf, diag_len);
}

static teptris_status schema_compile_core(const char *json, size_t len,
                                          const char *base_dir, size_t base_len,
                                          struct teptris_schema **out,
                                          char **diag_buf, size_t *diag_len) {
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
    c.cur_root = root;
    if (base_dir != NULL && base_len > 0 && base_len < sizeof c.base_dir) {
        memcpy(c.base_dir, base_dir, base_len);
        c.base_dir[base_len] = '\0';
        char abs[TEPTRIS_URI_MAX];
        if (c.base_dir[strlen(c.base_dir) - 1] == '/') {
            snprintf(abs, sizeof abs, "file://%sschema.json", c.base_dir);
        } else {
            snprintf(abs, sizeof abs, "file://%s/schema.json", c.base_dir);
        }
        char *held = teptris_arena_alloc(&sch->arena, strlen(abs) + 1);
        strcpy(held, abs);
        c.cur_abs = held;
    }
    if (c.base_dir[0] != '\0') {
        char *held = teptris_arena_alloc(&sch->arena, strlen(c.base_dir) + 2);
        sprintf(held, "%s/", c.base_dir);
        c.cur_dir = held;
    }
    /* draft 07 and older make a $ref ignore its siblings; 2019-09+
     * evaluate $ref alongside them. The root's $schema decides. A
     * standard draft dialect switches format/content* to the spec's
     * annotation-only default; the format-assertion dialect URI opts
     * back in, and the no-dialect default asserts (the practical
     * mode every real-world manifest relies on). */
    if (jv_kind_of(root) == JV_OBJECT) {
        jv *sv = jv_member_get(root, "$schema");
        if (sv != NULL && jv_kind_of(sv) == JV_STRING) {
            const char *s = jv_str(sv);
            if (strstr(s, "draft-04") != NULL || strstr(s, "draft-06") != NULL ||
                strstr(s, "draft-07") != NULL) {
                c.ref_exclusive = true;
            }
            if (strstr(s, "format-assertion") == NULL) {
                c.dialect_declared = true;
            }
        }
    }

    bool root_recursive = false;
    if (jv_kind_of(root) == JV_OBJECT) {
        jv *ra = jv_member_get(root, "$recursiveAnchor");
        root_recursive = ra != NULL && jv_kind_of(ra) == JV_BOOL &&
                         jv_bool(ra);
    }
    c.cap_resources = 8;
    c.resources = teptris_arena_alloc(&sch->arena,
                                      c.cap_resources * sizeof(*c.resources));
    teptris_resource *root_res = &c.resources[c.n_resources++];
    root_res->abs = c.cur_abs != NULL ? c.cur_abs : "";
    root_res->root = root;
    root_res->node = NULL;
    root_res->recursive_anchor = root_recursive;
    detect_vocab(&c, root);
    collect_anchors(&c, root);
    collect_ids(&c, root, c.cur_abs, c.cur_dir != NULL ? c.cur_dir : c.base_dir,
                root, root_recursive);
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
