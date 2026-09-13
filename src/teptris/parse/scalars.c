#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "teptris/common/chartype.h"
#include "teptris/common/ryu/ryu_parse.h"
#include "teptris/memory/arena.h"
#include "teptris/parse/parse.h"

#define PUTCH(out, n, ch)                                                      \
    do {                                                                       \
        if (out != NULL) {                                                     \
            out[n] = (char)(ch);                                               \
        }                                                                      \
        (void)(n += 1);                                                        \
    } while (0)

static void utf8_put(char *out, size_t *n, uint32_t cp)
{
    if (cp < 0x80) {
        PUTCH(out, *n, cp);
    } else if (cp < 0x800) {
        PUTCH(out, *n, 0xC0 | (cp >> 6));
        PUTCH(out, *n, 0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        PUTCH(out, *n, 0xE0 | (cp >> 12));
        PUTCH(out, *n, 0x80 | ((cp >> 6) & 0x3F));
        PUTCH(out, *n, 0x80 | (cp & 0x3F));
    } else {
        PUTCH(out, *n, 0xF0 | (cp >> 18));
        PUTCH(out, *n, 0x80 | ((cp >> 12) & 0x3F));
        PUTCH(out, *n, 0x80 | ((cp >> 6) & 0x3F));
        PUTCH(out, *n, 0x80 | (cp & 0x3F));
    }
}

static teptris_status read_hex(teptris_parser *ps, int digits, uint32_t *cp)
{
    uint32_t v = 0;
    for (int i = 0; i < digits; i++) {
        if (ps->p >= ps->end || !tep_is_hex((unsigned char)*ps->p)) {
            return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX,
                               "invalid unicode escape");
        }
        char c = *ps->p;
        uint32_t d = (c <= '9') ? (uint32_t)(c - '0')
                                : (uint32_t)((c | 0x20) - 'a' + 10);
        v = v * 16 + d;
        tep_adv(ps, 1);
    }
    *cp = v;
    return TEPTRIS_OK;
}

teptris_status tep_scan_basic(teptris_parser *ps, bool ml, char *out,
                              size_t *out_len)
{
    size_t n = 0;
    for (;;) {
        if (ps->p >= ps->end) {
            return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX,
                               "unterminated string");
        }
        unsigned char c = (unsigned char)*ps->p;

        if (c == '"') {
            if (!ml) {
                tep_adv(ps, 1);
                *out_len = n;
                return TEPTRIS_OK;
            }
            size_t run = 0;
            const char *q = ps->p;
            while (q < ps->end && *q == '"') {
                run++;
                q++;
            }
            if (run >= 6) {
                return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX,
                                   "too many consecutive quotes");
            }
            size_t content = (run >= 3) ? run - 3 : run;
            for (size_t k = 0; k < content; k++) {
                PUTCH(out, n, '"');
            }
            tep_adv(ps, run);
            if (run <= 2) {
                continue;
            }
            *out_len = n;
            return TEPTRIS_OK;
        }

        if (c == '\\') {
            tep_adv(ps, 1);
            if (ps->p >= ps->end) {
                return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX,
                                   "unterminated escape");
            }
            unsigned char e = (unsigned char)*ps->p;
            if (ml && (e == ' ' || e == '\t' || e == '\n' || e == '\r')) {
                const char *q = ps->p;
                while (q < ps->end && (*q == ' ' || *q == '\t')) {
                    q++;
                }
                if (q >= ps->end ||
                    (*q != '\n' && !(*q == '\r' && q + 1 < ps->end && q[1] == '\n'))) {
                    return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX,
                                       "invalid escape");
                }
                while (ps->p < ps->end) {
                    char w = *ps->p;
                    if (w == ' ' || w == '\t' || w == '\n') {
                        tep_adv(ps, 1);
                    } else if (w == '\r' && ps->p + 1 < ps->end &&
                               ps->p[1] == '\n') {
                        tep_adv(ps, 2);
                    } else {
                        break;
                    }
                }
                continue;
            }
            switch (e) {
            case 'b':
                PUTCH(out, n, '\b');
                tep_adv(ps, 1);
                break;
            case 't':
                PUTCH(out, n, '\t');
                tep_adv(ps, 1);
                break;
            case 'n':
                PUTCH(out, n, '\n');
                tep_adv(ps, 1);
                break;
            case 'f':
                PUTCH(out, n, '\f');
                tep_adv(ps, 1);
                break;
            case 'r':
                PUTCH(out, n, '\r');
                tep_adv(ps, 1);
                break;
            case '"':
                PUTCH(out, n, '"');
                tep_adv(ps, 1);
                break;
            case '\\':
                PUTCH(out, n, '\\');
                tep_adv(ps, 1);
                break;
            case 'u':
            case 'U': {
                tep_adv(ps, 1);
                uint32_t cp;
                teptris_status st = read_hex(ps, (e == 'u') ? 4 : 8, &cp);
                if (st != TEPTRIS_OK) {
                    return st;
                }
                if ((cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF) {
                    return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX,
                                       "invalid unicode scalar value");
                }
                utf8_put(out, &n, cp);
                break;
            }
            default:
                return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX,
                                   "invalid escape");
            }
            continue;
        }

        if (c == '\n') {
            if (!ml) {
                return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX,
                                   "unterminated string");
            }
            PUTCH(out, n, '\n');
            tep_adv(ps, 1);
            continue;
        }
        if (c == '\r') {
            if (ml && ps->p + 1 < ps->end && ps->p[1] == '\n') {
                PUTCH(out, n, '\r');
                tep_adv(ps, 2);
                continue;
            }
            return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX,
                               "control character in string");
        }
        if ((c < 0x20 && c != '\t') || c == 0x7F) {
            return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX,
                               "control character in string");
        }
        PUTCH(out, n, c);
        tep_adv(ps, 1);
    }
}

teptris_status tep_scan_literal(teptris_parser *ps, bool ml, char *out,
                                size_t *out_len)
{
    size_t n = 0;
    for (;;) {
        if (ps->p >= ps->end) {
            return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX,
                               "unterminated string");
        }
        unsigned char c = (unsigned char)*ps->p;

        if (c == '\'') {
            if (!ml) {
                tep_adv(ps, 1);
                *out_len = n;
                return TEPTRIS_OK;
            }
            size_t run = 0;
            const char *q = ps->p;
            while (q < ps->end && *q == '\'') {
                run++;
                q++;
            }
            if (run >= 6) {
                return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX,
                                   "too many consecutive quotes");
            }
            size_t content = (run >= 3) ? run - 3 : run;
            for (size_t k = 0; k < content; k++) {
                PUTCH(out, n, '\'');
            }
            tep_adv(ps, run);
            if (run <= 2) {
                continue;
            }
            *out_len = n;
            return TEPTRIS_OK;
        }

        if (c == '\n') {
            if (!ml) {
                return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX,
                                   "unterminated string");
            }
            PUTCH(out, n, '\n');
            tep_adv(ps, 1);
            continue;
        }
        if (c == '\r') {
            if (ml && ps->p + 1 < ps->end && ps->p[1] == '\n') {
                PUTCH(out, n, '\r');
                tep_adv(ps, 2);
                continue;
            }
            return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX,
                               "control character in string");
        }
        if ((c < 0x20 && c != '\t') || c == 0x7F) {
            return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX,
                               "control character in string");
        }
        PUTCH(out, n, c);
        tep_adv(ps, 1);
    }
}

/* -------------------------------------------------------------- values -- */

bool teptris_try_plain_body(teptris_parser *ps, char close, const char **content,
                            size_t *len)
{
    const char *p = ps->p;
    const char *end = ps->end;
    while (p < end) {
        unsigned char c = (unsigned char)*p;
        if (c == (unsigned char)close) {
            *content = ps->p;
            *len = (size_t)(p - ps->p);
            return true;
        }
        if (c == '\\' || c == 0x7F || (c < 0x20 && c != '\t')) {
            return false;
        }
        p++;
    }
    return false; /* unterminated / escaped / control: two-pass path reports */
}

static teptris_status parse_string(teptris_parser *ps, teptris_node **out)
{
    bool basic = *ps->p == '"';
    const char *d = ps->p;
    bool ml = (size_t)(ps->end - ps->p) >= 3 && d[1] == d[0] && d[2] == d[0];

    tep_adv(ps, ml ? 3 : 1);
    if (ml) {
        if (ps->p + 1 < ps->end && *ps->p == '\r' && ps->p[1] == '\n') {
            tep_adv(ps, 2);
        } else if (ps->p < ps->end && *ps->p == '\n') {
            tep_adv(ps, 1);
        }
    } else {
        const char *content;
        size_t len;
        if (teptris_try_plain_body(ps, basic ? '"' : '\'', &content, &len)) {
            teptris_node *n = teptris_dom_new_node(ps->doc, TEPTRIS_STRING);
            if (n == NULL) {
                return tep_fail_at(ps, NULL, TEPTRIS_ERR_ALLOC, "out of memory");
            }
            char *buf = teptris_arena_alloc(&ps->doc->arena, len + 1);
            if (buf == NULL) {
                return tep_fail_at(ps, NULL, TEPTRIS_ERR_ALLOC, "out of memory");
            }
            memcpy(buf, content, len);
            buf[len] = '\0';
            n->as.str.ptr = buf;
            n->as.str.len = len;
            tep_adv(ps, len + 1); /* single line: closing quote included */
            *out = n;
            return TEPTRIS_OK;
        }
    }

    teptris_parser save = *ps;
    size_t len = 0;
    teptris_status st = basic ? tep_scan_basic(ps, ml, NULL, &len)
                              : tep_scan_literal(ps, ml, NULL, &len);
    if (st != TEPTRIS_OK) {
        return st;
    }
    char *buf = teptris_arena_alloc(&ps->doc->arena, len + 1);
    if (buf == NULL) {
        return tep_fail_at(ps, NULL, TEPTRIS_ERR_ALLOC, "out of memory");
    }
    *ps = save;
    st = basic ? tep_scan_basic(ps, ml, buf, &len)
               : tep_scan_literal(ps, ml, buf, &len);
    if (st != TEPTRIS_OK) {
        return st;
    }
    buf[len] = '\0';

    teptris_node *n = teptris_dom_new_node(ps->doc, TEPTRIS_STRING);
    if (n == NULL) {
        return tep_fail_at(ps, NULL, TEPTRIS_ERR_ALLOC, "out of memory");
    }
    n->as.str.ptr = buf;
    n->as.str.len = len;
    *out = n;
    return TEPTRIS_OK;
}

static teptris_status expect_word(teptris_parser *ps, const char *w)
{
    size_t n = strlen(w);
    if ((size_t)(ps->end - ps->p) < n || memcmp(ps->p, w, n) != 0) {
        return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX, "invalid value");
    }
    tep_adv(ps, n);
    return TEPTRIS_OK;
}

static bool digit_for(unsigned char c, int radix)
{
    switch (radix) {
    case 10:
        return tep_is_dec(c);
    case 16:
        return tep_is_hex(c);
    case 8:
        return c >= '0' && c <= '7';
    case 2:
        return c == '0' || c == '1';
    default:
        return false;
    }
}

static unsigned hexval(char c)
{
    return (c <= '9') ? (unsigned)(c - '0')
                      : (unsigned)((c | 0x20) - 'a' + 10);
}

#define TEP_NUM_MAX 512

static teptris_status scan_run(teptris_parser *ps, int radix, char *buf,
                               size_t *blen)
{
    size_t n = 0;
    bool prev_digit = false;
    while (ps->p < ps->end) {
        unsigned char c = (unsigned char)*ps->p;
        if (digit_for(c, radix)) {
            if (n == TEP_NUM_MAX - 1) {
                return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX,
                                   "number too long");
            }
            buf[n++] = (char)c;
            prev_digit = true;
            tep_adv(ps, 1);
        } else if (c == '_') {
            if (!prev_digit) {
                return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX,
                                   "underscore must be between digits");
            }
            prev_digit = false;
            tep_adv(ps, 1);
        } else {
            break;
        }
    }
    buf[n] = '\0';
    if (n == 0) {
        return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX, "expected digits");
    }
    if (!prev_digit) {
        return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX,
                           "trailing underscore in number");
    }
    *blen = n;
    return TEPTRIS_OK;
}

static bool mag_from(const char *s, size_t n, int radix, uint64_t *out)
{
    uint64_t v = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned d = hexval(s[i]);
        if (v > (UINT64_MAX - d) / (unsigned)radix) {
            return false;
        }
        v = v * (unsigned)radix + d;
    }
    *out = v;
    return true;
}

static teptris_status parse_number(teptris_parser *ps, teptris_node **out,
                                   bool signed_input)
{
    bool neg = false;
    if (signed_input) {
        neg = (*ps->p == '-');
        tep_adv(ps, 1);
    }
    if (ps->p >= ps->end) {
        return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX, "invalid number");
    }
    unsigned char c = (unsigned char)*ps->p;

    if (c == 'i') {
        teptris_status st = expect_word(ps, "inf");
        if (st != TEPTRIS_OK) {
            return st;
        }
        teptris_node *n = teptris_dom_new_node(ps->doc, TEPTRIS_FLOAT);
        if (n == NULL) {
            return tep_fail_at(ps, NULL, TEPTRIS_ERR_ALLOC, "out of memory");
        }
        n->as.f = neg ? -INFINITY : INFINITY;
        *out = n;
        return TEPTRIS_OK;
    }
    if (c == 'n') {
        teptris_status st = expect_word(ps, "nan");
        if (st != TEPTRIS_OK) {
            return st;
        }
        teptris_node *n = teptris_dom_new_node(ps->doc, TEPTRIS_FLOAT);
        if (n == NULL) {
            return tep_fail_at(ps, NULL, TEPTRIS_ERR_ALLOC, "out of memory");
        }
        n->as.f = NAN;
        *out = n;
        return TEPTRIS_OK;
    }
    if (!tep_is_dec(c)) {
        return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX, "invalid number");
    }

    char buf[TEP_NUM_MAX];
    size_t blen = 0;

    if (c == '0' && ps->p + 1 < ps->end) {
        char r = ps->p[1];
        if (r == 'x' || r == 'o' || r == 'b') {
            if (signed_input) {
                return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX,
                                   "sign not allowed on radix integer");
            }
            int radix = (r == 'x') ? 16 : (r == 'o') ? 8 : 2;
            tep_adv(ps, 2);
            teptris_status st = scan_run(ps, radix, buf, &blen);
            if (st != TEPTRIS_OK) {
                return st;
            }
            uint64_t mag;
            if (!mag_from(buf, blen, radix, &mag) || mag > (uint64_t)INT64_MAX) {
                return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX,
                                   "integer out of range");
            }
            teptris_node *n = teptris_dom_new_node(ps->doc, TEPTRIS_INTEGER);
            if (n == NULL) {
                return tep_fail_at(ps, NULL, TEPTRIS_ERR_ALLOC, "out of memory");
            }
            n->as.i = (int64_t)mag;
            *out = n;
            return TEPTRIS_OK;
        }
    }

    /* Decimal fast path: plain digits, no underscores, <=18 of them.
     * Digits cannot contain newlines, so the cursor moves directly. */
    {
        const char *dstart = ps->p;
        uint64_t mag = 0;
        while (ps->p < ps->end && tep_is_dec((unsigned char)*ps->p)) {
            mag = mag * 10 + (uint64_t)((unsigned char)*ps->p - '0');
            ps->p++;
        }
        size_t dlen = (size_t)(ps->p - dstart);
        bool plain = dlen > 0 && dlen <= 18 &&
                     !(ps->p < ps->end && *ps->p == '_');
        if (plain) {
            if (dstart[0] == '0' && dlen > 1) {
                return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX,
                                   "leading zero in number");
            }
            bool is_float = ps->p < ps->end &&
                            (*ps->p == '.' || *ps->p == 'e' || *ps->p == 'E');
            if (!is_float) {
                teptris_node *n = teptris_dom_new_node(ps->doc, TEPTRIS_INTEGER);
                if (n == NULL) {
                    return tep_fail_at(ps, NULL, TEPTRIS_ERR_ALLOC,
                                       "out of memory");
                }
                n->as.i = neg ? -(int64_t)mag : (int64_t)mag;
                *out = n;
                return TEPTRIS_OK;
            }
            /* Zero-copy fast path: peek the frac/exp spans without
             * copying, then let ryu read the contiguous input span —
             * '-' is part of it; a leading '+' (ryu rejects it) falls
             * through to the copying path. */
            if (!signed_input || neg) {
                const char *q = ps->p; /* at '.', 'e' or 'E' */
                bool ok = true;
                if (q < ps->end && *q == '.') {
                    q++;
                    if (q < ps->end && tep_is_dec((unsigned char)*q)) {
                        while (q < ps->end && tep_is_dec((unsigned char)*q)) {
                            q++;
                        }
                        if (q < ps->end && *q == '_') {
                            ok = false;
                        }
                    } else {
                        ok = false; /* copy path reports "expected digits" */
                    }
                }
                if (ok && q < ps->end && (*q == 'e' || *q == 'E')) {
                    q++;
                    if (q < ps->end && (*q == '+' || *q == '-')) {
                        q++;
                    }
                    if (q < ps->end && tep_is_dec((unsigned char)*q)) {
                        while (q < ps->end && tep_is_dec((unsigned char)*q)) {
                            q++;
                        }
                        if (q < ps->end && *q == '_') {
                            ok = false;
                        }
                    } else {
                        ok = false;
                    }
                }
                if (ok) {
                    const char *nstart = neg ? dstart - 1 : dstart;
                    size_t nlen = (size_t)(q - nstart);
                    double dv = 0.0;
                    if (teptris_ryu_s2d_n(nstart, (int)nlen, &dv) ==
                        TEPTRIS_RYU_SUCCESS) {
                        teptris_node *n =
                            teptris_dom_new_node(ps->doc, TEPTRIS_FLOAT);
                        if (n == NULL) {
                            return tep_fail_at(ps, NULL, TEPTRIS_ERR_ALLOC,
                                               "out of memory");
                        }
                        n->as.f = dv;
                        ps->p = (char *)q; /* digits only: no newlines */
                        *out = n;
                        return TEPTRIS_OK;
                    }
                }
            }

            char fbuf[TEP_NUM_MAX * 2 + 8];
            size_t fn = 0;
            if (neg) {
                fbuf[fn++] = '-';
            }
            memcpy(fbuf + fn, dstart, dlen);
            fn += dlen;
            if (*ps->p == '.') {
                fbuf[fn++] = '.';
                ps->p++;
                const char *fs = ps->p;
                while (ps->p < ps->end && tep_is_dec((unsigned char)*ps->p)) {
                    if (fn < sizeof(fbuf) - 24) {
                        fbuf[fn++] = *ps->p;
                    }
                    ps->p++;
                }
                if (fs == ps->p) {
                    return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX,
                                       "expected digits");
                }
                if (ps->p < ps->end && *ps->p == '_') {
                    ps->p = dstart;
                    goto slow_decimal;
                }
            }
            if (ps->p < ps->end && (*ps->p == 'e' || *ps->p == 'E')) {
                fbuf[fn++] = 'e';
                ps->p++;
                if (ps->p < ps->end && (*ps->p == '+' || *ps->p == '-')) {
                    fbuf[fn++] = *ps->p;
                    ps->p++;
                }
                const char *es = ps->p;
                while (ps->p < ps->end && tep_is_dec((unsigned char)*ps->p)) {
                    if (fn < sizeof(fbuf) - 8) {
                        fbuf[fn++] = *ps->p;
                    }
                    ps->p++;
                }
                if (es == ps->p) {
                    return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX,
                                       "expected digits");
                }
                if (ps->p < ps->end && *ps->p == '_') {
                    ps->p = dstart;
                    goto slow_decimal;
                }
            }
            fbuf[fn] = '\0';
            teptris_node *n = teptris_dom_new_node(ps->doc, TEPTRIS_FLOAT);
            if (n == NULL) {
                return tep_fail_at(ps, NULL, TEPTRIS_ERR_ALLOC, "out of memory");
            }
            double d = 0.0;
            if (teptris_ryu_s2d_n(fbuf, (int)fn, &d) != TEPTRIS_RYU_SUCCESS) {
                d = strtod(fbuf, NULL);
            }
            n->as.f = d;
            *out = n;
            return TEPTRIS_OK;
        }
        if (dlen > 0) {
            ps->p = dstart; /* underscores or >18 digits: generic path */
        }
    }

slow_decimal:;
    teptris_status st = scan_run(ps, 10, buf, &blen);
    if (st != TEPTRIS_OK) {
        return st;
    }
    if (buf[0] == '0' && blen > 1) {
        return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX,
                           "leading zero in number");
    }

    bool is_float = ps->p < ps->end && (*ps->p == '.' || *ps->p == 'e' ||
                                        *ps->p == 'E');
    if (is_float) {
        char fbuf[TEP_NUM_MAX * 2 + 8];
        size_t fn = 0;
        if (neg) {
            fbuf[fn++] = '-';
        }
        memcpy(fbuf + fn, buf, blen);
        fn += blen;
        if (*ps->p == '.') {
            fbuf[fn++] = '.';
            tep_adv(ps, 1);
            char frac[TEP_NUM_MAX];
            size_t flen = 0;
            st = scan_run(ps, 10, frac, &flen);
            if (st != TEPTRIS_OK) {
                return st;
            }
            memcpy(fbuf + fn, frac, flen);
            fn += flen;
        }
        if (ps->p < ps->end && (*ps->p == 'e' || *ps->p == 'E')) {
            fbuf[fn++] = 'e';
            tep_adv(ps, 1);
            if (ps->p < ps->end && (*ps->p == '+' || *ps->p == '-')) {
                fbuf[fn++] = *ps->p;
                tep_adv(ps, 1);
            }
            char ex[TEP_NUM_MAX];
            size_t elen = 0;
            st = scan_run(ps, 10, ex, &elen);
            if (st != TEPTRIS_OK) {
                return st;
            }
            memcpy(fbuf + fn, ex, elen);
            fn += elen;
        }
        fbuf[fn] = '\0';
        teptris_node *n = teptris_dom_new_node(ps->doc, TEPTRIS_FLOAT);
        if (n == NULL) {
            return tep_fail_at(ps, NULL, TEPTRIS_ERR_ALLOC, "out of memory");
        }
        double d = 0.0;
        if (teptris_ryu_s2d_n(fbuf, (int)fn, &d) != TEPTRIS_RYU_SUCCESS) {
            d = strtod(fbuf, NULL); /* >17 significant digits: ryu gap */
        }
        n->as.f = d;
        *out = n;
        return TEPTRIS_OK;
    }

    uint64_t mag;
    if (!mag_from(buf, blen, 10, &mag)) {
        return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX, "integer out of range");
    }
    teptris_node *n = teptris_dom_new_node(ps->doc, TEPTRIS_INTEGER);
    if (n == NULL) {
        return tep_fail_at(ps, NULL, TEPTRIS_ERR_ALLOC, "out of memory");
    }
    if (neg) {
        if (mag > 9223372036854775808ULL) {
            return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX,
                               "integer out of range");
        }
        n->as.i = (mag == 9223372036854775808ULL) ? INT64_MIN : -(int64_t)mag;
    } else {
        if (mag > (uint64_t)INT64_MAX) {
            return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX,
                               "integer out of range");
        }
        n->as.i = (int64_t)mag;
    }
    *out = n;
    return TEPTRIS_OK;
}

teptris_status teptris_parse_value(teptris_parser *ps, teptris_node **out)
{
    if (ps->p >= ps->end) {
        return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX, "expected value");
    }
    unsigned char c = (unsigned char)*ps->p;

    switch (c) {
    case '"':
    case '\'':
        return parse_string(ps, out);
    case '[':
        return teptris_parse_array(ps, out);
    case '{':
        return teptris_parse_inline(ps, out);
    case 't':
    case 'f': {
        teptris_status st = expect_word(ps, (c == 't') ? "true" : "false");
        if (st != TEPTRIS_OK) {
            return st;
        }
        teptris_node *n = teptris_dom_new_node(ps->doc, TEPTRIS_BOOLEAN);
        if (n == NULL) {
            return tep_fail_at(ps, NULL, TEPTRIS_ERR_ALLOC, "out of memory");
        }
        n->as.b = (c == 't');
        *out = n;
        return TEPTRIS_OK;
    }
    case 'i':
    case 'n':
        return parse_number(ps, out, false);
    case '+':
    case '-':
        return parse_number(ps, out, true);
    default:
        if (teptris_datetime_lookahead(ps)) {
            return teptris_parse_datetime(ps, out);
        }
        if (tep_is_dec(c)) {
            return parse_number(ps, out, false);
        }
        return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX, "expected value");
    }
}
