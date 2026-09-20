/* libFuzzer harness: parse -> emit -> re-parse. The invariants under
 * fuzz are the memory-safety contracts (no crash under ASan/UBSan,
 * no leak - every parse path frees or transfers its document), not
 * semantic correctness - that is toml-test's job. The input buffer
 * must outlive the document (table-key views point into it), which
 * libFuzzer guarantees for the duration of the callback. */
#include <stdint.h>
#include <stdlib.h>

#include "teptris/teptris.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size == 0 || size > (1u << 20)) return 0;

    teptris_document *doc = NULL;
    teptris_status st = teptris_parse((const char *)data, size, NULL, &doc);
    if (st == TEPTRIS_OK && doc != NULL) {
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
