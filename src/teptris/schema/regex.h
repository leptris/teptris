/* The safe regex subset: internal to the schema subsystem; see
 * regex.c for the engine contract. */
#ifndef TEPTRIS_SCHEMA_REGEX_H
#define TEPTRIS_SCHEMA_REGEX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "teptris/memory/arena.h"

typedef struct sch_regex sch_regex;

/* Compile a pattern into the arena; NULL with a diagnostic on error. */
struct sch_regex *sch_regex_compile(teptris_arena *a, const char *pattern,
                                    size_t len, char *err, size_t err_cap);

/* ECMA-style unanchored search over UTF-8 bytes. */
bool sch_regex_is_match(const struct sch_regex *re, const char *utf8,
                        size_t len);

/* Search over pre-decoded code points. */
bool sch_regex_search(const struct sch_regex *re, const uint32_t *s, int len);

#endif
