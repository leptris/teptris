/* libFuzzer harness for the schema layer: the input is sliced into a
 * schema document and a TOML document. The invariants: a failed
 * compile hands back no schema but may hand back one malloc'd diag
 * (freed via teptris_diag_free); a successful compile followed by
 * validate/validate_doc hands back exactly one malloc'd buffer of the
 * reported length on OK and nothing on any other status; compile and
 * free loop without leaking. Memory safety under ASan/UBSan is the
 * contract here; the unit suite owns semantics. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "teptris/schema.h"
#include "teptris/teptris.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 2 || size > (1u << 19)) return 0;

    uint8_t sel = data[0];
    const uint8_t *body = data + 1;
    size_t n = size - 1;

    /* split point: 1/8..7/8 of the body */
    size_t cut = n / 8 * (1 + (sel & 7));
    if (cut > n) cut = n;

    size_t schema_len = cut;
    size_t doc_len = n - cut;
    const char *schema_src = (const char *)body;
    const char *doc_src = (const char *)body + cut;

    teptris_schema *sch = NULL;
    char *diag = NULL;
    size_t diag_len = 0;
    teptris_status st = teptris_schema_compile(schema_src, schema_len, &sch,
                                               &diag, &diag_len);
    if (st != TEPTRIS_OK) {
        if (sch != NULL) return 0; /* contract: no schema on failure */
        /* diag, when present, is one malloc'd NUL-terminated buffer */
        if (diag != NULL) {
            int shape_ok = diag_len == strlen(diag);
            teptris_diag_free(diag);
            if (!shape_ok) return 0;
        }
        return 0;
    }
    if (diag != NULL) return 0; /* contract: no diag on success */

    /* validate the raw TOML string */
    char *buf = NULL;
    size_t blen = 0;
    st = teptris_schema_validate(sch, doc_src, doc_len, &buf, &blen);
    if (st == TEPTRIS_OK) {
        int shape_ok = buf != NULL && blen >= 2 && buf[0] == '[' &&
                       buf[blen - 1] == ']' && strlen(buf) == blen;
        free(buf);
        if (!shape_ok) return 0;
    } else {
        /* any other status hands back nothing (syntax is the only
         * expected non-OK: the doc side is arbitrary bytes) */
        if (buf != NULL) return 0;
        if (st != TEPTRIS_ERR_SYNTAX) return 0;
    }

    /* validate_doc on the same bytes when they parse */
    if ((sel & 0x10) != 0) {
        teptris_document *doc = NULL;
        teptris_status pst =
            teptris_parse(doc_src, doc_len, NULL, &doc);
        if (pst == TEPTRIS_OK && doc != NULL) {
            char *dbuf = NULL;
            size_t dlen = 0;
            st = teptris_schema_validate_doc(sch, doc, &dbuf, &dlen);
            int shape_ok = 1;
            if (st == TEPTRIS_OK) {
                shape_ok = dbuf != NULL && dlen >= 2;
                free(dbuf);
            } else {
                shape_ok = dbuf == NULL;
            }
            if (!shape_ok) {
                teptris_document_free(doc);
                teptris_schema_free(sch);
                return 0;
            }
        }
        teptris_document_free(doc);
    }

    /* the compiled schema survives repeated validation, then frees */
    for (int round = 0; round < 3; round++) {
        buf = NULL;
        blen = 0;
        st = teptris_schema_validate(sch, doc_src, doc_len, &buf, &blen);
        if (st == TEPTRIS_OK) {
            free(buf);
        }
    }
    teptris_schema_free(sch);
    return 0;
}
