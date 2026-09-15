#include <string.h>

#include "teptris/common/chartype.h"
#include "teptris/memory/arena.h"
#include "teptris/parse/parse.h"

/* Quoted key: single-line basic or literal string, decoded to an
 * arena-owned NUL-terminated view. Opening delimiter at the cursor. */
static teptris_status quoted_key(teptris_parser *ps, teptris_view *out)
{
    bool basic = *ps->p == '"';
    tep_adv(ps, 1);

    const char *content;
    size_t plen;
    if (teptris_try_plain_body(ps, basic ? '"' : '\'', &content, &plen)) {
        char *buf = teptris_arena_alloc(&ps->doc->arena, plen + 1);
        if (buf == NULL) {
            return tep_fail_at(ps, NULL, TEPTRIS_ERR_ALLOC, "out of memory");
        }
        memcpy(buf, content, plen);
        buf[plen] = '\0';
        tep_adv(ps, plen + 1);
        out->ptr = buf;
        out->len = plen;
        return TEPTRIS_OK;
    }

    teptris_parser save = *ps;
    size_t len = 0;
    teptris_status st = basic ? tep_scan_basic(ps, false, NULL, &len)
                              : tep_scan_literal(ps, false, NULL, &len);
    if (st != TEPTRIS_OK) {
        return st;
    }
    char *buf = teptris_arena_alloc(&ps->doc->arena, len + 1);
    if (buf == NULL) {
        return tep_fail_at(ps, NULL, TEPTRIS_ERR_ALLOC, "out of memory");
    }
    *ps = save;
    st = basic ? tep_scan_basic(ps, false, buf, &len)
               : tep_scan_literal(ps, false, buf, &len);
    if (st != TEPTRIS_OK) {
        return st;
    }
    buf[len] = '\0';
    out->ptr = buf;
    out->len = len;
    return TEPTRIS_OK;
}

teptris_status teptris_parse_key_path(teptris_parser *ps,
                                      teptris_view *sbuf, size_t scap,
                                      teptris_view **parts, size_t *count)
{
    teptris_arena *a = &ps->doc->arena;
    teptris_view *v = sbuf;
    size_t n = 0, cap = scap;

    for (;;) {
        teptris_status st = tep_skip_ws(ps);
        if (st != TEPTRIS_OK) {
            return st;
        }
        if (ps->p >= ps->end) {
            return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX, "expected key");
        }
        unsigned char c = (unsigned char)*ps->p;

        teptris_view key;
        if (c == '"' || c == '\'') {
            st = quoted_key(ps, &key);
            if (st != TEPTRIS_OK) {
                return st;
            }
        } else if (tep_is_barekey(c)) {
            const char *start = ps->p;
            while (ps->p < ps->end && tep_is_barekey((unsigned char)*ps->p)) {
                ps->p++;
            }
            key.ptr = start;
            key.len = (size_t)(ps->p - start);
        } else {
            return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX,
                               "expected key (bare or quoted)");
        }

        if (n == cap) {
            size_t ncap = cap * 2;
            teptris_view *nv = teptris_arena_try_grow(
                a, v, cap * sizeof(teptris_view), ncap * sizeof(teptris_view));
            if (nv == NULL) {
                nv = teptris_arena_alloc(a, ncap * sizeof(teptris_view));
                if (nv == NULL) {
                    return tep_fail_at(ps, NULL, TEPTRIS_ERR_ALLOC,
                                       "out of memory");
                }
                memcpy(nv, v, n * sizeof(teptris_view));
                v = nv;
            } else {
                v = nv;
            }
            cap = ncap;
        }
        v[n++] = key;

        st = tep_skip_ws(ps);
        if (st != TEPTRIS_OK) {
            return st;
        }
        if (ps->p < ps->end && *ps->p == '.') {
            tep_adv(ps, 1);
            continue;
        }
        break;
    }

    *parts = v;
    *count = n;
    return TEPTRIS_OK;
}
