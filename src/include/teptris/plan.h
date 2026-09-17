#ifndef TEPTRIS_PLAN_H
#define TEPTRIS_PLAN_H

#include <stdint.h>

#include "teptris/teptris.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TEPTRIS_PLAN_ABI_VERSION 1

typedef struct teptris_plan teptris_plan;
typedef struct teptris_plan_result teptris_plan_result;

typedef enum {
    TEPTRIS_PLAN_SCALAR = 1,
    TEPTRIS_PLAN_COLLECTION = 2,
    TEPTRIS_PLAN_NESTED = 3,
    TEPTRIS_PLAN_RAW = 4,
} teptris_plan_row_kind;

/* Result node kinds (walk output). */
enum {
    TEPTRIS_PLAN_MISSING = 0,
    TEPTRIS_PLAN_SCALAR_RESULT = 1,
    TEPTRIS_PLAN_ARRAY = 2,
    TEPTRIS_PLAN_TABLE = 3,
    TEPTRIS_PLAN_RAW_RESULT = 4,
};

typedef struct teptris_plan_row {
    const char *name; /* table key */
    uint8_t kind;     /* teptris_plan_row_kind */
    uint32_t sub;     /* sub-plan index (NESTED only) */
} teptris_plan_row;

/* Flattened plan tree: plan p owns rows [first_row[p], first_row[p+1]).
 * The builder deep-copies; caller memory is transient. */
typedef struct teptris_plan_spec {
    uint32_t abi_version;
    uint32_t plan_count;
    const teptris_plan_row *plans;
    const uint32_t *plan_first_row; /* plan_count + 1 entries */
} teptris_plan_spec;

TEPTRIS_API teptris_plan *teptris_plan_build(const teptris_plan_spec *spec,
                                             teptris_status *status);
TEPTRIS_API void teptris_plan_free(teptris_plan *plan);

/* Walk a TABLE node against the plan's root. Results are addressed by
 * ROW INDEX (the plan's row order); keys absent from the document (or
 * type-mismatched rows) read as MISSING. */
TEPTRIS_API teptris_plan_result *teptris_plan_walk(const teptris_plan *plan,
                                                   const teptris_node *node,
                                                   teptris_status *status);
TEPTRIS_API void teptris_plan_result_free(teptris_plan_result *result);

TEPTRIS_API uint8_t teptris_plan_result_kind_at(const teptris_plan_result *r,
                                                uint32_t row);
TEPTRIS_API uint8_t teptris_plan_result_value_kind_at(
    const teptris_plan_result *r, uint32_t row);
TEPTRIS_API teptris_status teptris_plan_result_string_at(
    const teptris_plan_result *r, uint32_t row, teptris_view *out);
TEPTRIS_API teptris_status teptris_plan_result_integer_at(
    const teptris_plan_result *r, uint32_t row, int64_t *out);
TEPTRIS_API teptris_status teptris_plan_result_float_at(
    const teptris_plan_result *r, uint32_t row, double *out);
TEPTRIS_API teptris_status teptris_plan_result_boolean_at(
    const teptris_plan_result *r, uint32_t row, bool *out);
TEPTRIS_API teptris_status teptris_plan_result_datetime_at(
    const teptris_plan_result *r, uint32_t row, teptris_datetime *out);
TEPTRIS_API uint32_t teptris_plan_result_array_len_at(
    const teptris_plan_result *r, uint32_t row);
TEPTRIS_API uint8_t teptris_plan_result_array_kind_at(
    const teptris_plan_result *r, uint32_t row, uint32_t i);
TEPTRIS_API teptris_status teptris_plan_result_array_string_at(
    const teptris_plan_result *r, uint32_t row, uint32_t i, teptris_view *out);
TEPTRIS_API teptris_status teptris_plan_result_array_integer_at(
    const teptris_plan_result *r, uint32_t row, uint32_t i, int64_t *out);
/* RAW rows: the untouched document subtree (read with the normal
 * teptris_node_* API). */
/* Borrowed sub-result over a TABLE-kind row (NESTED): its plan rows
 * stay addressable with the same accessors. */
TEPTRIS_API teptris_plan_result *teptris_plan_result_row_view(
    const teptris_plan_result *r, uint32_t row);
TEPTRIS_API teptris_status teptris_plan_result_array_float_at(
    const teptris_plan_result *r, uint32_t row, uint32_t i, double *out);
TEPTRIS_API teptris_status teptris_plan_result_array_boolean_at(
    const teptris_plan_result *r, uint32_t row, uint32_t i, bool *out);
TEPTRIS_API teptris_status teptris_plan_result_array_datetime_at(
    const teptris_plan_result *r, uint32_t row, uint32_t i,
    teptris_datetime *out);
/* Plan row metadata (assembly drivers): kind/sub/name by plan+row. */
TEPTRIS_API uint8_t teptris_plan_row_kind_at(const teptris_plan *p,
                                          uint32_t plan_idx, uint32_t row);
TEPTRIS_API uint32_t teptris_plan_row_sub_at(const teptris_plan *p,
                                          uint32_t plan_idx, uint32_t row);
TEPTRIS_API const char *teptris_plan_row_name_at(const teptris_plan *p,
                                              uint32_t plan_idx, uint32_t row);
/* Borrowed sub-result over an ARRAY element (e.g. a table from an
 * array-of-tables): its plan rows are addressable with the same
 * accessors. Shares the parent result's lifetime; release the wrapper
 * with teptris_plan_result_view_free (the tree itself is not freed). */
TEPTRIS_API teptris_plan_result *teptris_plan_result_array_entry_at(
    const teptris_plan_result *r, uint32_t row, uint32_t i);
TEPTRIS_API void teptris_plan_result_view_free(teptris_plan_result *view);

TEPTRIS_API const teptris_node *teptris_plan_result_raw_at(
    const teptris_plan_result *r, uint32_t row);

TEPTRIS_API uint32_t teptris_plan_row_count(const teptris_plan *plan,
                                            uint32_t plan_idx);
TEPTRIS_API uint32_t teptris_plan_abi_version(void);

#ifdef __cplusplus
}
#endif

#endif /* TEPTRIS_PLAN_H */
