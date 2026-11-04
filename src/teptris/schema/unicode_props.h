/* Unicode property ranges for the schema regex \p{...}. Generated. */
#ifndef TEPTRIS_SCHEMA_UNICODE_PROPS_H
#define TEPTRIS_SCHEMA_UNICODE_PROPS_H

#include <stdint.h>

typedef struct {
    uint32_t lo, hi;
} teptris_uc_range;

typedef struct {
    const char *name;
    const teptris_uc_range *ranges;
    int n;
} teptris_uc_prop;

extern const teptris_uc_prop teptris_unicode_props[];
extern const int teptris_n_unicode_props;

#endif
