/* The schema-side JSON DOM (arena-owned). Internal to the schema
 * subsystem; see json_read.c for the reader contract. */
#ifndef TEPTRIS_SCHEMA_JSON_READ_H
#define TEPTRIS_SCHEMA_JSON_READ_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "teptris/memory/arena.h"

typedef enum { JV_NULL, JV_BOOL, JV_INT, JV_NUM, JV_STRING, JV_ARRAY, JV_OBJECT } jv_kind;

typedef struct jv jv;

jv *json_read(teptris_arena *a, const char *src, size_t len, char *err,
              size_t err_cap);

jv_kind jv_kind_of(const jv *v);
bool jv_bool(const jv *v);
int64_t jv_int(const jv *v);
double jv_num(const jv *v); /* the double view of INT or NUM */
const char *jv_str(const jv *v);
size_t jv_str_len(const jv *v);

size_t jv_member_count(const jv *v);
const char *jv_member_key(const jv *v, size_t i);
jv *jv_member_val(const jv *v, size_t i);
jv *jv_member_get(const jv *v, const char *key); /* last duplicate wins */

size_t jv_item_count(const jv *v);
jv *jv_item(const jv *v, size_t i);

#endif
