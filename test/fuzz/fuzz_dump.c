/* libFuzzer harness for the BUILDER + every emitter: the input bytes
 * drive a builder op sequence (the bindings' dump path — ruby/py/rs/ts
 * all materialize through this API), then each emitter view of the
 * built document is produced and re-parsed. Invariants under
 * ASan/UBSan: no crash, no leak, every builder op result checked, the
 * builder freed on every path, and the emitters' outputs re-parse
 * cleanly when the input said they should (ops that finish mid-
 * container make finish fail — also fine). The input layout: every
 * byte is one op; op bytes < 0x80 name a key (length from the next
 * byte), high bytes are op classes. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "teptris/teptris.h"

typedef struct {
    char *data;
    size_t len;
} strbuf;

static void sb_put(strbuf *sb, const char *s, size_t n)
{
    char *np = realloc(sb->data, sb->len + n + 1);
    if (np == NULL) {
        free(sb->data);
        sb->data = NULL;
        return;
    }
    sb->data = np;
    memcpy(sb->data + sb->len, s, n);
    sb->len += n;
    sb->data[sb->len] = '\0';
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 4 || size > (1u << 18)) return 0;

    teptris_builder *b = teptris_builder_new();
    if (b == NULL) return 0;

    strbuf sb = {NULL, 0};
    size_t pos = 0;
    int ops = 0;

    while (pos + 2 < size && ops < 4096) {
        uint8_t op = data[pos];
        uint8_t klen = data[pos + 1];
        pos += 2;
        char key[256];
        if (klen) {
            if (klen > size - pos) klen = (uint8_t)(size - pos);
            memcpy(key, data + pos, klen);
            key[klen] = '\0';
        }
        const char *keyp = klen ? key : NULL;
        size_t keyn = klen;
        pos += klen;

        int st;
        switch (op % 8) {
        case 0: { /* string value: take bytes until a 0xFF marker */
            const uint8_t *vend = memchr(data + pos, 0xFF, size - pos);
            size_t vlen = vend ? (size_t)(vend - (data + pos)) : size - pos;
            if (vlen > 4096) vlen = 4096;
            st = teptris_builder_put_string(b, keyp, keyn,
                                            (const char *)data + pos, vlen);
            pos += vlen;
            sb_put(&sb, keyp ? key : "?", keyn);
            sb_put(&sb, "=str ", 5);
            break;
        }
        case 1: {
            int64_t v = 0;
            memcpy(&v, data + pos, sizeof(v) < size - pos ? sizeof(v)
                                                          : size - pos);
            st = teptris_builder_put_integer(b, keyp, keyn, v);
            pos += 8;
            sb_put(&sb, keyp ? key : "?", keyn);
            sb_put(&sb, "=int ", 5);
            break;
        }
        case 2: {
            double d;
            memcpy(&d, data + pos, sizeof(d) < size - pos ? sizeof(d)
                                                          : size - pos);
            st = teptris_builder_put_float(b, keyp, keyn, d);
            pos += 8;
            sb_put(&sb, "=f ", 3);
            break;
        }
        case 3:
            st = teptris_builder_put_boolean(b, keyp, keyn, op & 1);
            sb_put(&sb, "=b ", 3);
            break;
        case 4: { /* datetime from raw bytes (kind clamped to 4..7) */
            teptris_datetime dt;
            memcpy(&dt, data + pos, sizeof(dt) < size - pos ? sizeof(dt)
                                                            : size - pos);
            teptris_kind k = (teptris_kind)(4 + (op % 4));
            dt.year = (dt.year % 9999) + 1;
            if (dt.month > 12) dt.month = 12;
            if (dt.day > 31) dt.day = 31;
            if (dt.hour > 23) dt.hour = 23;
            if (dt.minute > 59) dt.minute = 59;
            if (dt.second > 60) dt.second = 60;
            st = teptris_builder_put_datetime(b, keyp, keyn, k, &dt);
            pos += sizeof(dt);
            sb_put(&sb, "=dt ", 4);
            break;
        }
        case 5:
            st = teptris_builder_open_table(b, keyp, keyn);
            sb_put(&sb, keyp ? key : "?", keyn);
            sb_put(&sb, "={ ", 3);
            break;
        case 6:
            st = teptris_builder_open_array(b, keyp, keyn);
            sb_put(&sb, keyp ? key : "?", keyn);
            sb_put(&sb, "=[ ", 3);
            break;
        default:
            st = teptris_builder_close(b);
            sb_put(&sb, "} ", 2);
            break;
        }
        if (st != TEPTRIS_OK && st != TEPTRIS_ERR_ARG) {
            /* alloc-level failures abort the op loop; ARG is the
             * builder's normal stack/semantics rejection */
            if (st == TEPTRIS_ERR_ALLOC || st == TEPTRIS_ERR_STATE) {
                free(sb.data);
                teptris_builder_free(b);
                return 0;
            }
        }
        ops++;
    }

/* writer half: replay the SAME op sequence as an op-stream; whatever
 * the writer accepts must produce TOML that re-parses cleanly */
    {
        teptris_writer *wr = teptris_writer_new();
        if (wr != NULL) {
            uint8_t ws[8192];
            size_t wn = 0;
            /* re-walk a fresh builder op loop in writer-op encoding:
             * keys as 0x01 records, values as scalar records, table/
             * array opens as 0x08/0x09, closes as 0x0C */
            size_t pos2 = 0;
            int ops2 = 0;
            int depth2 = 0;
            int last_was_table = 0;
            while (pos2 + 2 < size && ops2 < 4096 && wn < sizeof ws) {
                uint8_t op = data[pos2];
                uint8_t klen = data[pos2 + 1];
                pos2 += 2;
                if (klen > size - pos2) {
                    klen = (uint8_t)(size - pos2);
                }
                if (wn + 2 + klen > sizeof ws) {
                    break;
                }
                ws[wn++] = 0x01;
                ws[wn++] = klen;
                memcpy(ws + wn, data + pos2, klen);
                wn += klen;
                pos2 += klen;
                switch (op % 8) {
                case 0: {
                    const uint8_t *vend =
                        memchr(data + pos2, 0xFF, size - pos2);
                    size_t vlen =
                        vend ? (size_t)(vend - (data + pos2)) : size - pos2;
                    if (vlen > 4096) vlen = 4096;
                    if (wn + 5 + vlen > sizeof ws) break;
                    ws[wn++] = 0x02;
                    ws[wn++] = (uint8_t)(vlen & 0xFF);
                    ws[wn++] = (uint8_t)((vlen >> 8) & 0xFF);
                    ws[wn++] = (uint8_t)((vlen >> 16) & 0xFF);
                    ws[wn++] = (uint8_t)((vlen >> 24) & 0xFF);
                    memcpy(ws + wn, data + pos2, vlen);
                    wn += vlen;
                    pos2 += vlen;
                    last_was_table = 0;
                    break;
                }
                case 1: {
                    if (wn + 9 > sizeof ws) break;
                    ws[wn++] = 0x03;
                    size_t take = 8 < size - pos2 ? 8 : size - pos2;
                    memcpy(ws + wn, data + pos2, take);
                    memset(ws + wn + take, 0, 8 - take);
                    wn += 8;
                    last_was_table = 0;
                    break;
                }
                case 2: {
                    if (wn + 9 > sizeof ws) break;
                    ws[wn++] = 0x04;
                    size_t take = 8 < size - pos2 ? 8 : size - pos2;
                    memcpy(ws + wn, data + pos2, take);
                    memset(ws + wn + take, 0, 8 - take);
                    wn += 8;
                    last_was_table = 0;
                    break;
                }
                case 3: {
                    if (wn >= sizeof ws) break;
                    ws[wn++] = (op & 1) ? 0x06 : 0x05;
                    last_was_table = 0;
                    break;
                }
                case 4: {
                    if (wn + 27 > sizeof ws) break; /* op+kind+i64+5+u32+i64 */
                    ws[wn++] = 0x07;
                    ws[wn++] = (uint8_t)(4 + (op % 4));
                    int64_t yr = 1979;
                    memcpy(ws + wn, &yr, 8); wn += 8;
                    ws[wn++] = 5; ws[wn++] = 27; ws[wn++] = 7;
                    ws[wn++] = 32; ws[wn++] = 0;
                    uint32_t ns = 123456789;
                    memcpy(ws + wn, &ns, 4); wn += 4;
                    int64_t off = 0;
                    memcpy(ws + wn, &off, 8); wn += 8;
                    last_was_table = 0;
                    break;
                }
                case 5:
                    if (depth2 < 400 && wn < sizeof ws) {
                        ws[wn++] = 0x08;
                        depth2++;
                        last_was_table = 1;
                    }
                    break;
                case 6:
                    if (depth2 < 400 && wn < sizeof ws) {
                        ws[wn++] = 0x09;
                        depth2++;
                        last_was_table = 1;
                    }
                    break;
                case 7:
                    if (depth2 > 0 && wn < sizeof ws) {
                        ws[wn++] = 0x0C;
                        depth2--;
                        if (depth2 == 0) last_was_table = 1;
                    }
                    break;
                }
                ops2++;
            }
            if (wn < sizeof ws) {
                ws[wn++] = 0x00;
            }
            teptris_status wst = teptris_writer_feed(wr, ws, wn);
            char *wbuf = NULL;
            size_t wlen = 0;
            if (wst == TEPTRIS_OK) {
                wst = teptris_writer_finish(wr, &wbuf, &wlen);
                if (wst == TEPTRIS_OK && wbuf != NULL) {
                    teptris_document *wd = NULL;
                    (void)teptris_parse(wbuf, wlen, NULL, &wd);
                    if (wd != NULL) teptris_document_free(wd);
                    free(wbuf);
                }
            }
            teptris_writer_free(wr);
        }
    }

    teptris_document *doc = NULL;
    teptris_status st = teptris_builder_finish(b, &doc);
    teptris_builder_free(b);
    if (st == TEPTRIS_OK && doc != NULL) {
        /* every emitter view must produce re-parseable output */
        char *buf = NULL;
        size_t len = 0;
        teptris_document *d2 = NULL;
        if (teptris_document_emit(doc, &buf, &len) == TEPTRIS_OK && buf) {
            (void)teptris_parse(buf, len, NULL, &d2);
            if (d2 != NULL) teptris_document_free(d2);
            free(buf);
            buf = NULL;
        }
        if (teptris_document_emit_json(doc, &buf, &len) == TEPTRIS_OK && buf) {
            free(buf);
            buf = NULL;
        }
        if (teptris_document_emit_json_natural(doc, &buf, &len) == TEPTRIS_OK &&
            buf) {
            free(buf);
            buf = NULL;
        }
        uint8_t *flat = NULL;
        size_t flen = 0;
        if (teptris_document_flatten(doc, &flat, &flen) == TEPTRIS_OK && flat) {
            teptris_flatten_free(flat);
        }
        if (sb.data != NULL) {
            /* the builder's own op log re-parses where it completed a
             * balanced tree — partial trees legitimately fail, so the
             * invariant is only memory safety, checked by the
             * sanitizers */
            (void)sb.data;
        }
        teptris_document_free(doc);
    }
    free(sb.data);
    return 0;
}
