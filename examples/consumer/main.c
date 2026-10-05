/* examples/consumer/main.c — the smallest real teptris consumer.
 *
 * Parse a TOML document, walk it, emit it back, free it. Build it:
 *
 *   cc main.c $(pkg-config --cflags --libs teptris) -o demo && ./demo
 *
 * The one rule that matters: the document's strings are ZERO-COPY
 * VIEWS into the input buffer — the buffer must outlive the document
 * (here it is a static array; heap inputs keep the same contract).
 * teptris_parse writes NUL terminators into the buffer, so it must
 * be writable: copy your bytes into a malloc'd buffer first when the
 * source is read-only.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "teptris/teptris.h"

static void print_scalar(const teptris_node *node) {
    switch (teptris_node_kind(node)) {
    case TEPTRIS_STRING: {
        teptris_view s;
        if (teptris_node_string(node, &s) == TEPTRIS_OK)
            printf("  string: %.*s\n", (int)s.len, s.ptr);
        break;
    }
    case TEPTRIS_INTEGER: {
        int64_t v = 0;
        if (teptris_node_integer(node, &v) == TEPTRIS_OK)
            printf("  integer: %lld\n", (long long)v);
        break;
    }
    default:
        printf("  (kind %d)\n", (int)teptris_node_kind(node));
        break;
    }
}

int main(void) {
    /* writable, and outlives the document (see the header comment) */
    char source[] = "name = \"consumer-demo\"\n"
                    "port = 8080\n"
                    "[server]\n"
                    "host = \"0.0.0.0\"\n";

    teptris_document *doc = NULL;
    teptris_status st = teptris_parse(source, strlen(source), NULL, &doc);
    if (st != TEPTRIS_OK || doc == NULL) {
        fprintf(stderr, "parse failed (status %d)\n", (int)st);
        return 1;
    }

    const teptris_node *root = teptris_document_root(doc);
    size_t n = teptris_node_table_length(root);
    for (size_t i = 0; i < n; i++) {
        teptris_view key;
        const teptris_node *value = teptris_node_table_at(root, i, &key);
        printf("%.*s:\n", (int)key.len, key.ptr);
        if (teptris_node_kind(value) == TEPTRIS_TABLE) {
            teptris_view k2;
            const teptris_node *inner = teptris_node_table_at(value, 0, &k2);
            printf("  %.*s:\n", (int)k2.len, k2.ptr);
            print_scalar(inner);
        } else {
            print_scalar(value);
        }
    }

    char *buf = NULL;
    size_t len = 0;
    if (teptris_document_emit(doc, &buf, &len) == TEPTRIS_OK && buf) {
        printf("--- emitted ---\n%.*s", (int)len, buf);
        free(buf);
    }

    teptris_document_free(doc);
    return 0;
}
