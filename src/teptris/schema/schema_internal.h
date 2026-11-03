/* The compiled-schema internals: shared between schema.c (compile)
 * and validate.c (the walk). */
#ifndef TEPTRIS_SCHEMA_INTERNAL_H
#define TEPTRIS_SCHEMA_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "json_read.h"
#include "regex.h"
#include "teptris/memory/arena.h"

/* the JSON-view types a schema can match; datetimes map to string
 * (policy 3) */
enum {
    ST_STRING = 1 << 0,
    ST_INTEGER = 1 << 1,
    ST_NUMBER = 1 << 2, /* float or integer */
    ST_BOOLEAN = 1 << 3,
    ST_OBJECT = 1 << 4,
    ST_ARRAY = 1 << 5,
    ST_NULL = 1 << 6 /* the JSON null type (policy 1: no TOML value) */
};
#define ST_ANY 0x7Fu

enum {
    FMT_NONE = 0,
    FMT_DATETIME,
    FMT_DATE,
    FMT_TIME,
    FMT_DATETIME_LOCAL,
    FMT_TIME_LOCAL,
    /* the taplo parity set: every format the jsonschema crate (the
     * engine behind taplo) asserts on; names outside this set are
     * annotations on both sides */
    FMT_EMAIL,
    FMT_IDN_EMAIL,
    FMT_HOSTNAME,
    FMT_IDN_HOSTNAME,
    FMT_IPV4,
    FMT_IPV6,
    FMT_URI,
    FMT_IRI,
    FMT_URI_REFERENCE,
    FMT_IRI_REFERENCE,
    FMT_JSON_POINTER,
    FMT_RELATIVE_JSON_POINTER,
    FMT_REGEX,
    FMT_URI_TEMPLATE,
    FMT_UUID,
    FMT_DURATION,
    /* taplo's own custom formats (registered by its schema cache) */
    FMT_SEMVER,
    FMT_SEMVER_REQUIREMENT
};

typedef struct sch_node sch_node;

typedef struct {
    const char *name;
    sch_node *schema;
} sch_prop;

typedef struct {
    struct sch_regex *re;
    sch_node *schema;
} sch_pprop;

typedef struct {
    const char *key;
    sch_node *schema;
} sch_kschema;

struct sch_node {
    bool is_false; /* the false schema: matches nothing */

    unsigned types; /* ST_* mask; ST_ANY when "type" is absent */

    /* applicators */
    sch_node **all_of, **any_of, **one_of;
    size_t n_all, n_any, n_one;
    sch_node *_not, *if_s, *then_s, *else_s;
    sch_node *ref; /* resolved $ref target (transparent) */

    /* object keywords */
    sch_prop *props;
    size_t n_props;
    sch_pprop *pprops;
    size_t n_pprops;
    sch_node *prop_names;
    sch_node *additional; /* NULL = unconstrained */
    bool additional_set;
    char **required;
    size_t n_req;
    long min_props, max_props;
    bool has_min_props, has_max_props;
    sch_kschema *dep_req; /* key -> required keys */
    size_t n_dep_req;
    sch_kschema *dep_schemas; /* key -> schema */
    size_t n_dep_schemas;

    /* array keywords */
    sch_node **prefix;
    size_t n_prefix;
    sch_node *items;
    bool items_set;
    sch_node *add_items; /* additionalItems: applies beyond prefix */
    bool add_items_set;
    sch_node *uneval_props; /* unevaluatedProperties: frame-local */
    sch_node *uneval_items; /* unevaluatedItems: frame-local */
    sch_node *content_schema; /* contentSchema: asserts decoded content */
    /* the dynamic anchors inside this node's own subtree (self
     * included, document order): pushed as one scope frame whenever
     * this node is applied — the dynamic scope grows per entered
     * subschema, not per traversed anchor */
    struct sch_scope_entry {
        const char *name;
        struct sch_node *node;
        const jv *res; /* the anchor's own resource (nearest $id) */
    } *self_scope;
    size_t n_self_scope;
    const jv *res_id;             /* the resource root this node sits in */
    bool dynamic_recursive;       /* the ref site was a $recursiveRef */
    const char *dynamic_anchor;   /* $dynamicAnchor name carried here */
    const char *recursive_anchor; /* "" when $recursiveAnchor is true */
    const char *dynamic_ref;      /* "#name" fragment for
                                   * $dynamicRef / $recursiveRef */
    sch_node *dynamic_ref_target; /* compile-resolved static fallback */
    bool ref_only;  /* draft-07-and-older: this $ref ignores siblings */
    bool enum_empty; /* "enum": [] matches nothing */
    sch_node *contains;
    long min_contains, max_contains;
    bool has_contains, has_min_contains, has_max_contains;
    bool unique_items;
    bool has_unique;
    long min_items, max_items;
    bool has_min_items, has_max_items;

    /* string keywords */
    struct sch_regex *pattern;
    long min_length, max_length;
    bool has_min_length, has_max_length;
    int format;
    const char *content_encoding;    /* enforced: "base64" */
    const char *content_media_type;  /* enforced: "application/json" */

    /* numeric keywords: the int form is exact when the constraint was
     * an exact integer token */
    bool has_min, has_max, has_ex_min, has_ex_max;
    double fmin, fmax, fex_min, fex_max;
    int64_t imin, imax, iex_min, iex_max;
    bool has_mult;
    double fmult;
    int64_t imult;
    bool mult_is_int;
    /* exact-decimal view of a non-integer multipleOf: the
     * shortest-roundtrip mantissa and decimal exponent */
    bool mult_exact;
    int64_t mult_mant;
    int mult_exp;

    /* value constraints */
    jv **enum_vals;
    size_t n_enum;
    jv *const_val;
    bool has_const;
};

struct teptris_schema {
    teptris_arena arena;
    sch_node *root;
    char err[256];
};

typedef struct teptris_resource teptris_resource;

typedef struct {
    teptris_arena *a;
    jv *root;
    /* memo: compile-once per raw subtree (handles $ref cycles) */
    struct memo_entry {
        const jv *src;
        sch_node *node;
        bool in_progress;
    } *memo;
    size_t n_memo, cap_memo;
    /* dependency-list capacities (grow-in-place across keywords) */
    size_t cap_dep_req, cap_dep_schemas;
    /* anchors: "$anchor": name -> subtree (doc-wide; the $anchor
     * space of every loaded resource joins it) */
    struct {
        const char *name;
        jv *target;
    } *anchors;
    size_t n_anchors, cap_anchors;
    /* $dynamicAnchor targets (the static fallback for $dynamicRef) */
    struct {
        const char *name;
        jv *target;
    } *dyn_anchors;
    size_t n_dyn_anchors, cap_dyn_anchors;
    jv *rec_anchor_target;
    /* the resource model: every base URI this compile has materialized */
    teptris_resource *resources;
    size_t n_resources, cap_resources;
    /* the resource the walk is currently inside */
    jv *cur_root;        /* its root jv ("#" resolves here) */
    const char *cur_abs; /* its base URI (NULL in local-only mode) */
    const char *cur_dir; /* its fs dir (relative refs join against it) */
    bool ref_exclusive;  /* draft-07-and-older: $ref ignores siblings */
    char base_dir[1024]; /* fs dir of the main schema ("" = none) */
    /* the base every node was pre-passed with: compile looks each
     * node's base up here (pointers reach subtrees whose $id chain
     * the document walk would otherwise miss) */
    /* scratch accumulator for attach_self_scope */
    struct sch_scope_entry *entry_acc;
    size_t n_entry_acc, cap_entry_acc;
    /* which standard vocabularies the dialect keeps (8.1.2: a custom
     * metaschema's $vocabulary decides; absent = all of them) */
    unsigned vocab_mask;
    /* standard vocabulary gates (the core vocabulary is mandatory and
 * never gated) */
#define TEPTRIS_VOC_APPLICATOR 1u
#define TEPTRIS_VOC_UNEVALUATED 2u
#define TEPTRIS_VOC_VALIDATION 4u
#define TEPTRIS_VOC_FORMAT 8u
#define TEPTRIS_VOC_CONTENT 16u
#define TEPTRIS_VOC_ALL 31u

struct node_base {
        const jv *node;
        const char *abs;
        const char *dir;
        jv *root;
        bool recursive;
    } *node_bases;
    size_t n_node_bases, cap_node_bases;
    char *err;
    size_t err_cap;
} sctx;

/* a materialized resource: a base URI naming a jv subtree */
struct teptris_resource {
    const char *abs; /* base URI (file://... or empty for local-only) */
    jv *root;        /* the resource's root jv ("#" resolves here) */
    sch_node *node;  /* compiled root (memo-resolved) */
    bool recursive_anchor; /* $recursiveAnchor: true on the root */
};

/* the tagged value the validator walks: converted from a document's
 * DOM once per validate call, or synthesized (schema-side enum/const
 * values, propertyNames strings) */
typedef struct sch_svalue sch_svalue;
typedef struct {
    const char *key;
    sch_svalue *val;
} sv_member;

enum {
    SV_STRING,
    SV_INT,
    SV_FLOAT,
    SV_BOOL,
    SV_NULL,
    SV_ARRAY,
    SV_TABLE,
    SV_DATETIME, /* offset or local */
    SV_DATE,
    SV_TIME
};

struct sch_svalue {
    int kind;
    const char *str;
    size_t str_len;
    int64_t i;
    double f;
    bool b;
    sv_member *members;
    size_t n_members;
    sch_svalue **items;
    size_t n_items;
    bool has_offset; /* offset datetimes only (kind rules for formats) */
};

/* schema.c */
sch_node *sch_compile_node(sctx *c, const jv *v);
sch_node *sch_resolve_pointer(sctx *c, const char *pointer);
/* the shortest-roundtrip decimal view of a double: d = mant * 10^exp
 * with |mant| < 10^18; false for non-finite d */
bool sch_decimal_of_double(double d, int64_t *mant, int *exp);

/* refs.c: the resource loader used by $ref resolution */
jv *sch_load_resource(sctx *c, const char *abs_uri);

/* formats.c */
/* the string-side format battery (the taplo parity set); datetimes
 * reach it through their canonical text like any other string */
bool sch_format_string_check(int format, const char *s, size_t len,
                             teptris_arena *scratch);

#endif
