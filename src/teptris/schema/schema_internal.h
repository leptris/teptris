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
    ST_ARRAY = 1 << 5
};
#define ST_ANY 0x3Fu

enum {
    FMT_NONE = 0,
    FMT_DATETIME,
    FMT_DATE,
    FMT_TIME,
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
    /* anchors: "$anchor": name -> subtree */
    struct {
        const char *name;
        jv *target;
    } *anchors;
    size_t n_anchors, cap_anchors;
    char *err;
    size_t err_cap;
} sctx;

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
};

/* schema.c */
sch_node *sch_compile_node(sctx *c, const jv *v);
sch_node *sch_resolve_pointer(sctx *c, const char *pointer);
/* the shortest-roundtrip decimal view of a double: d = mant * 10^exp
 * with |mant| < 10^18; false for non-finite d */
bool sch_decimal_of_double(double d, int64_t *mant, int *exp);

/* formats.c */
/* the string-side format battery (the taplo parity set); datetimes
 * reach it through their canonical text like any other string */
bool sch_format_string_check(int format, const char *s, size_t len,
                             teptris_arena *scratch);

#endif
