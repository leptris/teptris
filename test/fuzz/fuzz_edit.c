/* libFuzzer harness for the format-preserving edit API: the input is
 * sliced into a source document, a key path and a replacement. The
 * invariants: OK hands back exactly one malloc'd buffer of the
 * reported length and out_len <= src_len + strlen(replacement);
 * every other status hands back nothing; and a successful splice fed
 * back as the source is at worst a rejection - never a crash. Memory
 * safety under ASan/UBSan is the contract here; toml-test and the
 * unit suite own semantics. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "teptris/teptris.h"

static size_t copy_token(char *dst, size_t cap, const uint8_t *src,
                         size_t len)
{
    size_t n = len < cap - 1 ? len : cap - 1;
    size_t i = 0;
    for (; i < n && src[i] != 0; i++) {
        dst[i] = (char)src[i];
    }
    dst[i] = '\0';
    return i;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 4 || size > (1u << 20)) return 0;

    uint8_t sel = data[0];
    const uint8_t *body = data + 1;
    size_t n = size - 1;

    size_t s1 = (n / 4) * (1 + (sel & 3));
    size_t s2 = s1 + (n / 4) * (1 + ((sel >> 2) & 3));
    if (s1 > n) s1 = n;
    if (s2 > n) s2 = n;
    if (s2 < s1) {
        size_t t = s1;
        s1 = s2;
        s2 = t;
    }

    char key[256];
    char rep[256];
    size_t klen = copy_token(key, sizeof key, body + s1, s2 - s1);
    size_t rlen = copy_token(rep, sizeof rep, body + s2, n - s2);
    if (klen == 0 || rlen == 0) return 0;

    char *buf = NULL;
    size_t out_len = 0;
    teptris_status st = teptris_edit_scalar((const char *)body, s1, key,
                                            rep, &buf, &out_len);
    if (st != TEPTRIS_OK) {
        return 0; /* nothing handed back (contract) */
    }
    if (buf == NULL || out_len > s1 + rlen) {
        free(buf);
        abort(); /* violated the allocation contract */
    }

    /* the spliced bytes re-fed as source: rejection is fine, any
     * crash/leak is not */
    if (out_len > 0) {
        char *buf2 = NULL;
        size_t l2 = 0;
        teptris_status st2 = teptris_edit_scalar(buf, out_len, key, rep,
                                                 &buf2, &l2);
        if (st2 == TEPTRIS_OK) {
            free(buf2);
        }
    }
    free(buf);
    return 0;
}
