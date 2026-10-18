/* libFuzzer harness for the batch parse API: the input is sliced into
 * N length-equal documents and handed to teptris_parse_batch. The
 * invariants: every non-NULL out_docs[i] is freeable regardless of
 * out_statuses[i] (per-document failures still hand back a doc for
 * teptris_document_error), ERR_ALLOC means nothing to free, and the
 * n == 0 no-op is safe. ASan/UBSan verify the zeroing and partial
 * failure paths; the shared opts pointer is exercised with an
 * input-driven max_depth. */
#include <stdint.h>
#include <stdlib.h>

#include "teptris/teptris.h"

#define MAX_DOCS 8

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size == 0 || size > (1u << 20)) return 0;

    uint8_t sel = data[0];
    size_t n = (size_t)(sel % (MAX_DOCS + 1)); /* 0..8: 0 exercises the no-op */
    const uint8_t *body = data + 1;
    size_t body_len = size - 1;

    const char *docs[MAX_DOCS];
    size_t lens[MAX_DOCS];
    for (size_t i = 0; i < n; i++) {
        size_t chunk = body_len / n;
        docs[i] = (const char *)(body + i * chunk);
        lens[i] = chunk;
    }

    teptris_options opts;
    opts.max_depth = (sel & 0x10) ? 2 : 0; /* some cells take the deep-reject path */

    teptris_document *out[MAX_DOCS];
    teptris_status sts[MAX_DOCS];
    teptris_status st =
        teptris_parse_batch(docs, lens, n, &opts, out, sts);

    if (st == TEPTRIS_ERR_ALLOC) {
        /* no documents created - nothing to free (contract) */
        return 0;
    }
    for (size_t i = 0; i < n; i++) {
        if (out[i] != NULL) {
            (void)teptris_document_error(out[i]);
            teptris_document_free(out[i]);
        }
    }
    return 0;
}
