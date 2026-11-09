#include <string.h>

#include "teptris/common/chartype.h"
#include "teptris/memory/arena.h"
#include "teptris/parse/parse.h"

static int d2(const char *p)
{
    return (p[0] - '0') * 10 + (p[1] - '0');
}

static int num4(const char *p)
{
    return d2(p) * 100 + d2(p + 2);
}

static bool is_dig(const char *p)
{
    return tep_is_dec((unsigned char)*p);
}

static int days_in_month(int y, int m)
{
    static const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m == 2) {
        bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
        return leap ? 29 : 28;
    }
    return d[m - 1];
}

bool teptris_datetime_lookahead(const teptris_parser *ps)
{
    const char *p = ps->p;
    size_t avail = (size_t)(ps->end - p);
    /* Separator bytes first: every int/float value pays this check on
     * its way to parse_number, and plain numbers fail both in two
     * loads (p[4] is a digit or delimiter, never '-'; p[2] likewise
     * never ':'). Digit verification only runs when a datetime shape
     * is actually possible. */
    if (avail >= 10 && p[4] == '-' && p[7] == '-' && is_dig(p) &&
        is_dig(p + 1) && is_dig(p + 2) && is_dig(p + 3) && is_dig(p + 5) &&
        is_dig(p + 6) && is_dig(p + 8) && is_dig(p + 9)) {
        return true;
    }
    /* TOML 1.1: seconds are optional — HH:MM alone dispatches here
     * (HH:MM:SS still matches: p[5] == ':'). */
    if (avail >= 5 && p[2] == ':' && is_dig(p) && is_dig(p + 1) &&
        is_dig(p + 3) && is_dig(p + 4)) {
        return true;
    }
    return false;
}

static teptris_status finish_node(teptris_parser *ps, teptris_kind kind,
                                  const teptris_datetime *dt, size_t total,
                                  teptris_node **out)
{
    teptris_node *n = teptris_dom_new_node(ps->doc, kind);
    if (n == NULL) {
        return tep_fail_at(ps, NULL, TEPTRIS_ERR_ALLOC, "out of memory");
    }
    n->as.dt = *dt;
    /* the consumed span is datetime grammar only — no newline can
     * occur in it, so advance directly (skips tep_adv's memchr) */
    ps->p += total;
    *out = n;
    return TEPTRIS_OK;
}

/* The datetime grammar core: parse one datetime at p..end into dt.
 * No DOM, no parser state — teptris_parse_datetime wraps this with
 * node creation, and teptris_datetime_from_str uses it to classify a
 * standalone literal (the json-to-toml dump path). Errors come back
 * as ERR_SYNTAX with a message in *err. */
static teptris_status dt_scan(const char *p, const char *end,
                              teptris_datetime *dt, teptris_kind *kind,
                              size_t *consumed, const char **err)
{
    const char *start = p;
    memset(dt, 0, sizeof(*dt));

    if (end - p < 5) {
        *err = "datetime too short";
        return TEPTRIS_ERR_SYNTAX;
    }

    if (end - p >= 10 && p[4] == '-') {
        int y = num4(p);
        int mo = d2(p + 5);
        int da = d2(p + 8);
        if (mo < 1 || mo > 12) {
            *err = "invalid month";
            return TEPTRIS_ERR_SYNTAX;
        }
        if (da < 1 || da > days_in_month(y, mo)) {
            *err = "invalid day";
            return TEPTRIS_ERR_SYNTAX;
        }
        dt->year = (int32_t)y;
        dt->month = (uint8_t)mo;
        dt->day = (uint8_t)da;
        p += 10;

        bool has_time =
            p < end && (*p == 'T' || *p == 't' || *p == ' ') &&
            (size_t)(end - p) >= 6 && is_dig(p + 1) && is_dig(p + 2) &&
            p[3] == ':' && is_dig(p + 4) && is_dig(p + 5);
        if (!has_time) {
            *kind = TEPTRIS_DATE_LOCAL;
            *consumed = (size_t)(p - start);
            return TEPTRIS_OK;
        }
        p++; /* separator */

        int h = d2(p);
        int mi = d2(p + 3);
        int s = 0; /* TOML 1.1: seconds optional */
        size_t sec_consumed = 5;
        if ((size_t)(end - p) >= 6 && p[5] == ':') {
            if ((size_t)(end - p) < 8 || !is_dig(p + 6) || !is_dig(p + 7)) {
                *err = "invalid second";
                return TEPTRIS_ERR_SYNTAX;
            }
            s = d2(p + 6);
            sec_consumed = 8;
        }
        if (h > 23) {
            *err = "invalid hour";
            return TEPTRIS_ERR_SYNTAX;
        }
        if (mi > 59) {
            *err = "invalid minute";
            return TEPTRIS_ERR_SYNTAX;
        }
        if (s > 60) {
            *err = "invalid second";
            return TEPTRIS_ERR_SYNTAX;
        }
        dt->hour = (uint8_t)h;
        dt->minute = (uint8_t)mi;
        dt->second = (uint8_t)s;
        p += sec_consumed;

        if (p < end && *p == '.') {
            p++;
            if (p >= end || !is_dig(p)) {
                *err = "expected fractional digits";
                return TEPTRIS_ERR_SYNTAX;
            }
            uint32_t scale = 100000000u; /* 9 digits */
            uint32_t ns = 0;
            while (p < end && is_dig(p)) {
                if (scale > 0) {
                    ns += (uint32_t)(*p - '0') * scale;
                    scale /= 10;
                }
                p++; /* beyond 9 digits: validated, then truncated */
            }
            dt->nanosecond = ns;
        }

        if (p < end && (*p == 'Z' || *p == 'z')) {
            dt->offset_seconds = 0;
            p++;
            *kind = TEPTRIS_DATETIME_OFFSET;
            *consumed = (size_t)(p - start);
            return TEPTRIS_OK;
        }
        if (p < end && (*p == '+' || *p == '-') &&
            (size_t)(end - p) >= 6 && is_dig(p + 1) && is_dig(p + 2) &&
            p[3] == ':' && is_dig(p + 4) && is_dig(p + 5)) {
            int sign = (*p == '-') ? -1 : 1;
            int oh = d2(p + 1);
            int om = d2(p + 4);
            if (oh > 23 || om > 59) {
                *err = "invalid UTC offset";
                return TEPTRIS_ERR_SYNTAX;
            }
            dt->offset_seconds = sign * (oh * 3600 + om * 60);
            p += 6;
            *kind = TEPTRIS_DATETIME_OFFSET;
            *consumed = (size_t)(p - start);
            return TEPTRIS_OK;
        }
        *kind = TEPTRIS_DATETIME_LOCAL;
        *consumed = (size_t)(p - start);
        return TEPTRIS_OK;
    }

    /* time-only; TOML 1.1: seconds optional */
    int h = d2(p);
    int mi = d2(p + 3);
    int s = 0;
    size_t sec_consumed = 5;
    if ((size_t)(end - p) >= 8 && p[5] == ':') {
        if (!is_dig(p + 6) || !is_dig(p + 7)) {
            *err = "invalid second";
            return TEPTRIS_ERR_SYNTAX;
        }
        s = d2(p + 6);
        sec_consumed = 8;
    }
    if (h > 23) {
        *err = "invalid hour";
        return TEPTRIS_ERR_SYNTAX;
    }
    if (mi > 59) {
        *err = "invalid minute";
        return TEPTRIS_ERR_SYNTAX;
    }
    if (s > 60) {
        *err = "invalid second";
        return TEPTRIS_ERR_SYNTAX;
    }
    dt->hour = (uint8_t)h;
    dt->minute = (uint8_t)mi;
    dt->second = (uint8_t)s;
    p += sec_consumed;

    if (p < end && *p == '.') {
        p++;
        if (p >= end || !is_dig(p)) {
            *err = "expected fractional digits";
            return TEPTRIS_ERR_SYNTAX;
        }
        uint32_t scale = 100000000u;
        uint32_t ns = 0;
        while (p < end && is_dig(p)) {
            if (scale > 0) {
                ns += (uint32_t)(*p - '0') * scale;
                scale /= 10;
            }
            p++;
        }
        dt->nanosecond = ns;
    }

    *kind = TEPTRIS_TIME_LOCAL;
    *consumed = (size_t)(p - start);
    return TEPTRIS_OK;
}

teptris_status teptris_parse_datetime(teptris_parser *ps, teptris_node **out)
{
    const char *p = ps->p;
    const char *end = ps->end;
    const char *err = NULL;
    teptris_datetime dt;
    teptris_kind kind;
    size_t consumed;
    teptris_status st = dt_scan(p, end, &dt, &kind, &consumed, &err);
    if (st != TEPTRIS_OK) {
        return tep_fail_at(ps, NULL, st, err);
    }
    return finish_node(ps, kind, &dt, consumed, out);
}

teptris_status teptris_datetime_from_str(const char *s, size_t len,
                                         teptris_datetime *dt,
                                         teptris_kind *kind)
{
    const char *err = NULL;
    size_t consumed;
    teptris_status st = dt_scan(s, s + len, dt, kind, &consumed, &err);
    if (st != TEPTRIS_OK) {
        return st;
    }
    /* a standalone literal must be the WHOLE string: trailing bytes
     * make it a plain string, not a datetime */
    if (consumed != len) {
        return TEPTRIS_ERR_SYNTAX;
    }
    return TEPTRIS_OK;
}
