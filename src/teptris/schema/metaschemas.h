/* The official JSON Schema metaschemas as built-in resources. */
#ifndef TEPTRIS_SCHEMA_METASCHEMAS_H
#define TEPTRIS_SCHEMA_METASCHEMAS_H

#include <stddef.h>

struct teptris_builtin_resource {
    const char *uri;
    const char *json;
};

extern const struct teptris_builtin_resource teptris_builtin_resources[];
extern const size_t teptris_n_builtin_resources;

#endif
