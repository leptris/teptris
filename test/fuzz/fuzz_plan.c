/* libFuzzer harness for the plan API (teptris#46 surface, the engine
 * half of both bindings' Descriptor). The input is parsed as TOML;
 * a plan is then derived DETERMINISTICALLY from the same bytes
 * (row names from a fixed alphabet that overlaps the bench-corpus
 * keys so walks actually match, kinds/subs from input bytes), built,
 * walked, and every accessor drained - including mismatched-kind
 * calls, which the plan.h contract answers with a non-OK status.
 * Under fuzz are the memory-safety contracts: build/walk/view
 * lifetimes under ASan/UBSan, no leaks. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "teptris/teptris.h"
#include "teptris/plan.h"

/* Fixed alphabet overlapping the bench-corpus keys: k/m/t root keys,
 * arr/n array keys, id/name descriptor rows. */
static const char *const kRowNames[] = {
    "a", "k", "m", "t", "arr", "n", "id", "name"};
#define PLAN_ALPHA 8

static uint8_t byte_at(const uint8_t *d, size_t sz, size_t i) {
    return sz ? d[i % sz] : 0;
}

static void drain_row(const teptris_plan_result *r, uint32_t row,
                      uint32_t depth) {
    /* every typed accessor regardless of kind - the mismatched ones
     * must return non-OK, never crash */
    teptris_view v;
    (void)teptris_plan_result_string_at(r, row, &v);
    int64_t iv = 0;
    (void)teptris_plan_result_integer_at(r, row, &iv);
    double fv = 0;
    (void)teptris_plan_result_float_at(r, row, &fv);
    bool bv = false;
    (void)teptris_plan_result_boolean_at(r, row, &bv);
    teptris_datetime dt;
    (void)teptris_plan_result_datetime_at(r, row, &dt);

    uint32_t alen = teptris_plan_result_array_len_at(r, row);
    if (alen > 1024) alen = 1024;
    for (uint32_t j = 0; j < alen; j++) {
        (void)teptris_plan_result_array_value_kind_at(r, row, j);
        (void)teptris_plan_result_array_kind_at(r, row, j);
        (void)teptris_plan_result_array_string_at(r, row, j, &v);
        (void)teptris_plan_result_array_integer_at(r, row, j, &iv);
        (void)teptris_plan_result_array_float_at(r, row, j, &fv);
        (void)teptris_plan_result_array_boolean_at(r, row, j, &bv);
        (void)teptris_plan_result_array_datetime_at(r, row, j, &dt);
        if (depth > 0) {
            teptris_plan_result *sub =
                teptris_plan_result_array_entry_at(r, row, j);
            if (sub != NULL) {
                drain_row(sub, 0, depth - 1);
                teptris_plan_result_view_free(sub);
            }
        }
    }
    if (depth > 0 &&
        teptris_plan_result_kind_at(r, row) == TEPTRIS_PLAN_TABLE) {
        teptris_plan_result *sub = teptris_plan_result_row_view(r, row);
        if (sub != NULL) {
            drain_row(sub, 0, depth - 1);
            teptris_plan_result_view_free(sub);
        }
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size == 0 || size > (1u << 20)) return 0;

    teptris_document *doc = NULL;
    teptris_status st = teptris_parse((const char *)data, size, NULL, &doc);
    if (st != TEPTRIS_OK) {
        if (doc != NULL) teptris_document_free(doc);
        return 0; /* parse fuzzing is fuzz_parse.c's job */
    }

    /* Root plan: 1..8 rows from byte 0. Sub-plan: 1..8 rows from
     * byte 1. Disjoint first_row ranges (the descriptor layout). */
    uint32_t root_rows = (uint32_t)(byte_at(data, size, 0) % PLAN_ALPHA) + 1;
    uint32_t sub_rows = (size > 1)
        ? (uint32_t)(byte_at(data, size, 1) % PLAN_ALPHA) + 1 : 1;

    teptris_plan_row rows[PLAN_ALPHA * 2];
    uint32_t first_row[3] = {0, root_rows, root_rows + sub_rows};
    for (uint32_t i = 0; i < root_rows; i++) {
        uint8_t b = byte_at(data, size, 2 + i * 2);
        rows[i].name = kRowNames[b % PLAN_ALPHA];
        rows[i].kind = (uint8_t)((b >> 4) % 4) + 1;
        rows[i].sub = (rows[i].kind == TEPTRIS_PLAN_NESTED) ? 1 : 0;
    }
    for (uint32_t i = 0; i < sub_rows; i++) {
        uint8_t b = byte_at(data, size, 3 + i * 2);
        rows[root_rows + i].name = kRowNames[b % PLAN_ALPHA];
        rows[root_rows + i].kind = (uint8_t)((b >> 4) % 4) + 1;
        rows[root_rows + i].sub = 0;
    }

    teptris_plan_spec spec = {TEPTRIS_PLAN_ABI_VERSION, 2, rows, first_row};
    teptris_plan *plan = teptris_plan_build(&spec, &st);
    if (plan != NULL) {
        teptris_plan_result *res =
            teptris_plan_walk(plan, teptris_document_root(doc), &st);
        if (res != NULL) {
            for (uint32_t i = 0; i < root_rows; i++) drain_row(res, i, 3);
            teptris_plan_result_free(res);
        }
        teptris_plan_free(plan);
    }
    teptris_document_free(doc);
    return 0;
}
