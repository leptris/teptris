/* libFuzzer harness: parse (with input-driven max_depth) -> full DOM
 * walk through every typed accessor -> emit -> re-parse. The invariants
 * under fuzz are the memory-safety contracts (no crash under ASan/UBSan,
 * no leak - every parse path frees or transfers its document), not
 * semantic correctness - that is toml-test's job. The input buffer
 * must outlive the document (table-key views point into it), which
 * libFuzzer guarantees for the duration of the callback. */
#include <stdint.h>
#include <stdlib.h>

#include "teptris/teptris.h"

static void walk(const teptris_node *n, int depth) {
    if (n == NULL || depth > 64) return;
    switch (teptris_node_kind(n)) {
    case TEPTRIS_STRING: {
        teptris_view v;
        (void)teptris_node_string(n, &v);
        break;
    }
    case TEPTRIS_INTEGER: {
        int64_t i;
        (void)teptris_node_integer(n, &i);
        break;
    }
    case TEPTRIS_FLOAT: {
        double d;
        (void)teptris_node_float(n, &d);
        break;
    }
    case TEPTRIS_BOOLEAN: {
        bool b;
        (void)teptris_node_boolean(n, &b);
        break;
    }
    case TEPTRIS_DATETIME_OFFSET:
    case TEPTRIS_DATETIME_LOCAL:
    case TEPTRIS_DATE_LOCAL:
    case TEPTRIS_TIME_LOCAL: {
        teptris_datetime dt;
        (void)teptris_node_datetime(n, &dt);
        break;
    }
    case TEPTRIS_ARRAY: {
        size_t nitems = teptris_node_array_length(n);
        for (size_t i = 0; i < nitems; i++)
            walk(teptris_node_array_at(n, i), depth + 1);
        break;
    }
    case TEPTRIS_TABLE: {
        size_t nent = teptris_node_table_length(n);
        for (size_t i = 0; i < nent; i++) {
            teptris_view key;
            walk(teptris_node_table_at(n, i, &key), depth + 1);
        }
        /* probes: present, absent, and embedded-NUL keys */
        walk(teptris_node_table_get(n, "a", 1), depth + 1);
        walk(teptris_node_table_get(n, "zz", 2), depth + 1);
        walk(teptris_node_table_get(n, "a\0b", 3), depth + 1);
        break;
    }
    default:
        break;
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size == 0 || size > (1u << 20)) return 0;

    /* depth limit driven by the input: default, 1, 4, 64 - deep docs
     * must fail with ERR_DEPTH, shallow ones parse; both paths free */
    teptris_options opts;
    opts.max_depth = 0;
    if (size >= 2) {
        static const uint32_t depths[4] = {0, 1, 4, 64};
        opts.max_depth = depths[data[0] % 4];
    }

    teptris_document *doc = NULL;
    teptris_status st = teptris_parse((const char *)data, size, &opts, &doc);
    if (st == TEPTRIS_OK && doc != NULL) {
        walk(teptris_document_root(doc), 0);
        char *buf = NULL;
        size_t len = 0;
        if (teptris_document_emit(doc, &buf, &len) == TEPTRIS_OK && buf != NULL) {
            teptris_document *d2 = NULL;
            if (teptris_parse(buf, len, NULL, &d2) == TEPTRIS_OK && d2 != NULL)
                teptris_document_free(d2);
            free(buf);
        }
        teptris_document_free(doc);
    } else if (doc != NULL) {
        /* the contract sets *out on failure too - free it either way */
        teptris_document_free(doc);
    }
    return 0;
}
