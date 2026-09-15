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

teptris_status teptris_parse_datetime(teptris_parser *ps, teptris_node **out)
{
    const char *p = ps->p;
    const char *end = ps->end;
    const char *start = p;
    teptris_datetime dt;
    memset(&dt, 0, sizeof(dt));

    if (p[4] == '-') {
        int y = num4(p);
        int mo = d2(p + 5);
        int da = d2(p + 8);
        if (mo < 1 || mo > 12) {
            return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX, "invalid month");
        }
        if (da < 1 || da > days_in_month(y, mo)) {
            return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX, "invalid day");
        }
        dt.year = (int32_t)y;
        dt.month = (uint8_t)mo;
        dt.day = (uint8_t)da;
        p += 10;

        bool has_time =
            p < end && (*p == 'T' || *p == 't' || *p == ' ') &&
            (size_t)(end - p) >= 6 && is_dig(p + 1) && is_dig(p + 2) &&
            p[3] == ':' && is_dig(p + 4) && is_dig(p + 5);
        if (!has_time) {
            return finish_node(ps, TEPTRIS_DATE_LOCAL, &dt,
                               (size_t)(p - start), out);
        }
        p++; /* separator */

        int h = d2(p);
        int mi = d2(p + 3);
        int s = 0; /* TOML 1.1: seconds optional */
        size_t consumed = 5;
        if (p[5] == ':') {
            if ((size_t)(end - p) < 8 || !is_dig(p + 6) || !is_dig(p + 7)) {
                return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX,
                                   "invalid second");
            }
            s = d2(p + 6);
            consumed = 8;
        }
        if (h > 23) {
            return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX, "invalid hour");
        }
        if (mi > 59) {
            return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX, "invalid minute");
        }
        if (s > 60) {
            return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX, "invalid second");
        }
        dt.hour = (uint8_t)h;
        dt.minute = (uint8_t)mi;
        dt.second = (uint8_t)s;
        p += consumed;

        if (p < end && *p == '.') {
            p++;
            if (p >= end || !is_dig(p)) {
                return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX,
                                   "expected fractional digits");
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
            dt.nanosecond = ns;
        }

        if (p < end && (*p == 'Z' || *p == 'z')) {
            dt.offset_seconds = 0;
            p++;
            return finish_node(ps, TEPTRIS_DATETIME_OFFSET, &dt,
                               (size_t)(p - start), out);
        }
        if (p < end && (*p == '+' || *p == '-') &&
            (size_t)(end - p) >= 6 && is_dig(p + 1) && is_dig(p + 2) &&
            p[3] == ':' && is_dig(p + 4) && is_dig(p + 5)) {
            int sign = (*p == '-') ? -1 : 1;
            int oh = d2(p + 1);
            int om = d2(p + 4);
            if (oh > 23 || om > 59) {
                return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX,
                                   "invalid UTC offset");
            }
            dt.offset_seconds = sign * (oh * 3600 + om * 60);
            p += 6;
            return finish_node(ps, TEPTRIS_DATETIME_OFFSET, &dt,
                               (size_t)(p - start), out);
        }
        return finish_node(ps, TEPTRIS_DATETIME_LOCAL, &dt,
                           (size_t)(p - start), out);
    }

    /* time-only; TOML 1.1: seconds optional */
    int h = d2(p);
    int mi = d2(p + 3);
    int s = 0;
    size_t consumed = 5;
    if ((size_t)(end - p) >= 8 && p[5] == ':') {
        if (!is_dig(p + 6) || !is_dig(p + 7)) {
            return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX, "invalid second");
        }
        s = d2(p + 6);
        consumed = 8;
    }
    if (h > 23) {
        return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX, "invalid hour");
    }
    if (mi > 59) {
        return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX, "invalid minute");
    }
    if (s > 60) {
        return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX, "invalid second");
    }
    dt.hour = (uint8_t)h;
    dt.minute = (uint8_t)mi;
    dt.second = (uint8_t)s;
    p += consumed;

    if (p < end && *p == '.') {
        p++;
        if (p >= end || !is_dig(p)) {
            return tep_fail_at(ps, NULL, TEPTRIS_ERR_SYNTAX,
                               "expected fractional digits");
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
        dt.nanosecond = ns;
    }

    return finish_node(ps, TEPTRIS_TIME_LOCAL, &dt, (size_t)(p - start), out);
}
