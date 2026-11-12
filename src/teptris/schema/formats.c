/* The format battery: the exact set of format names the jsonschema
 * crate (taplo's validation engine, 0.17) asserts on, mirrored
 * per-format from its implementations. Names outside this set are
 * annotations on both sides. Only plain strings reach this file
 * (TOML datetime values get their formats by kind, policy 3); every
 * check is byte- and length-based because document strings may carry
 * embedded NULs and UTF-8. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "idna_tables.h"

#include "schema_internal.h"

static bool is_digit(char c) { return c >= '0' && c <= '9'; }

static bool is_hex(char c) {
    return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static bool is_ascii_alnum(char c) {
    return is_digit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static bool all_digits(const char *s, size_t from, size_t n) {
    for (size_t i = from; i < from + n; i++) {
        if (!is_digit(s[i])) return false;
    }
    return true;
}

static int two_digits(const char *s, size_t from) {
    return (s[from] - '0') * 10 + (s[from + 1] - '0');
}

static size_t cp_count(const char *s, size_t len) {
    size_t count = 0;
    size_t i = 0;
    while (i < len) {
        unsigned char b = (unsigned char)s[i];
        size_t step = b < 0x80   ? 1
                      : b < 0xE0 ? 2
                      : b < 0xF0 ? 3
                                 : 4;
        i += step > len - i ? 1 : step;
        count++;
    }
    return count;
}

/* ------------------------------------------------- date / time atoms -- */

static bool leap_year(int y) {
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

static int days_in_month(int y, int m) {
    static const int dim[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m == 2 && leap_year(y)) return 29;
    return dim[m - 1];
}

/* YYYY-MM-DD with real calendar ranges (the crate parses the date,
 * then re-checks the zero-padded shape; a strict digits+range check
 * answers both) */
static bool check_date(const char *s, size_t len) {
    if (len != 10 || s[4] != '-' || s[7] != '-') return false;
    if (!all_digits(s, 0, 4) || !all_digits(s, 5, 2) ||
        !all_digits(s, 8, 2)) {
        return false;
    }
    int y = (s[0] - '0') * 1000 + (s[1] - '0') * 100 + (s[2] - '0') * 10 +
            (s[3] - '0');
    int m = two_digits(s, 5);
    int d = two_digits(s, 8);
    return m >= 1 && m <= 12 && d >= 1 && d <= days_in_month(y, m);
}

/* HH:MM:SS with ranges; answers the consumed length (8) or 0 */
static size_t check_hms(const char *s, size_t len, bool allow_leap,
                        int *hour_out, int *minute_out, int *sec_out) {
    /* allow_leap: the date-time format's leap second (:60 at the UTC
     * day end — the caller validates the moment); the time format
     * and the local shapes cap at 59 */
    if (len < 8 || s[2] != ':' || s[5] != ':') return 0;
    if (!all_digits(s, 0, 2) || !all_digits(s, 3, 2) ||
        !all_digits(s, 6, 2)) {
        return 0;
    }
    int h = two_digits(s, 0);
    int m = two_digits(s, 3);
    int sec = two_digits(s, 6);
    if (h > 23 || m > 59) return 0;
    if (sec > 59 && !(allow_leap && sec == 60)) return 0;
    if (hour_out) *hour_out = h;
    if (minute_out) *minute_out = m;
    if (sec_out) *sec_out = sec;
    return 8;
}

/* the offset tail: Z/z or +HH:MM / -HH:MM; answers its length or 0 */
static size_t check_offset(const char *s, size_t len) {
    if (len == 0) return 0;
    if (s[0] == 'Z' || s[0] == 'z') return 1;
    if (s[0] != '+' && s[0] != '-') return 0;
    if (len < 6 || s[3] != ':') return 0;
    if (!all_digits(s, 1, 2) || !all_digits(s, 4, 2)) return 0;
    int h = two_digits(s, 1);
    int m = two_digits(s, 4);
    return h <= 23 && m <= 59 ? 6 : 0;
}

/* full-date "T" full-time: the offset is mandatory (RFC 3339, and the
 * crate parses with its RFC 3339 parser); lowercase t/z allowed */
static bool check_datetime(const char *s, size_t len) {
    if (len < 20) return false;
    if (!check_date(s, 10)) return false;
    if (s[10] != 'T' && s[10] != 't') return false;
    int h = 0, minute = 0, sec = 0;
    size_t used = check_hms(s + 11, len - 11, true, &h, &minute, &sec);
    if (used == 0) return false;
    size_t pos = 11 + used;
    if (pos < len && s[pos] == '.') {
        size_t frac = 0;
        pos++;
        while (pos < len && is_digit(s[pos])) {
            pos++;
            frac++;
        }
        if (frac == 0) return false;
    }
    if (pos >= len) return false;
    /* a leap second exists only at the UTC day end: the local clock
     * must read :59:60 and land at second 86400 after the offset */
    if (sec == 60) {
        if (s[pos] == 'Z' || s[pos] == 'z') {
            if (h * 3600 + minute * 60 + 60 != 86400) return false;
        } else {
            if (pos + 6 > len) return false;
            bool neg = s[pos] == '-';
            int oh = two_digits(s + pos, 1);
            int om = two_digits(s + pos, 4);
            /* (both read s[from], s[from+1] — signature is (s, from)) */
            int off = (neg ? -1 : 1) * (oh * 3600 + om * 60);
            if (h * 3600 + minute * 60 + 60 - off != 86400) return false;
        }
    }
    return len > pos && check_offset(s + pos, len - pos) == len - pos;
}

/* ------------------------------------------------------- rfc atoms -- */

/* the crate's email check: a leading dot fails, ".." fails, a dot
 * directly before "@" fails, an "@" ends the (unvalidated) domain;
 * everything else about the domain is accepted */
/* RFC 5322 addr-spec (the practical ASCII subset the suite tests).
 * Local: dot-atom (atext incl. ~) or a quoted string (\ escapes one
 * VCHAR; internal dots/spaces/@ fine; "" empty fine). Domain: LDH
 * dot-atom labels (no leading/trailing '-', single-label "io" fine)
 * or an address literal [dotted-quad 0-255 | IPv6:]. One @, ASCII
 * only, no CR/LF/space/controls anywhere, no display names. */
static bool atext(char c)
{
    if (is_ascii_alnum(c)) return true;
    return c == '!' || c == '#' || c == '$' || c == '%' || c == '&' ||
           c == '\'' || c == '*' || c == '+' || c == '-' || c == '/' ||
           c == '=' || c == '?' || c == '^' || c == '_' || c == '`' ||
           c == '{' || c == '|' || c == '}' || c == '~';
}

static bool quoted_local(const char *s, size_t len, size_t *end, bool utf8)
{
    if (s[0] != '"') return false;
    size_t i = 1;
    for (;;) {
        if (i >= len) return false; /* unterminated */
        unsigned char c = (unsigned char)s[i];
        if (c == '"') {
            *end = i + 1;
            return true; /* "" alone is legal; the caller checks what
                            follows */
        }
        if (c == '\\') {
            if (i + 1 >= len) return false;
            unsigned char n = (unsigned char)s[i + 1];
            if (n < 0x20 || n == 0x7f) return false;
            if (!utf8 && n >= 0x80) return false;
            i += 2;
            continue;
        }
        if (c < 0x20 || c == 0x7f) return false;
        if (!utf8 && c >= 0x80) return false;
        i++;
    }
}

static bool dot_atom(const char *s, size_t len, bool atext_only, bool utf8)
{
    if (len == 0 || s[0] == '.' || s[len - 1] == '.') return false;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '.') {
            if (i + 1 < len && s[i + 1] == '.') return false;
            continue;
        }
        bool ok = atext_only ? atext((char)c)
                             : (is_ascii_alnum(c) || c == '-');
        if (!ok && !(utf8 && c >= 0x80)) return false;
    }
    return true;
}

static bool check_ipv6(const char *s, size_t len);

static bool domain_literal(const char *s, size_t len)
{
    /* s = "[...]" inclusive */
    if (len < 3 || s[0] != '[' || s[len - 1] != ']') return false;
    const char *in = s + 1;
    size_t n = len - 2;
    if (n >= 5 && (in[0] == 'I' || in[0] == 'i') &&
        (in[1] == 'P' || in[1] == 'p') && in[2] == 'v' && in[3] == '6' &&
        in[4] == ':') {
        return check_ipv6(in + 5, n - 5);
    }
    /* dotted quad, each octet 0-255 */
    int octets = 0, val = -1, digits = 0;
    for (size_t i = 0; i <= n; i++) {
        if (i == n || in[i] == '.') {
            if (digits == 0 || digits > 3 || val < 0 || val > 255)
                return false;
            octets++;
            val = -1;
            digits = 0;
        } else if (is_digit(in[i])) {
            if (digits == 0) val = 0;
            val = val * 10 + (in[i] - '0');
            digits++;
        } else {
            return false;
        }
    }
    return octets == 4;
}

static bool check_domain(const char *s, size_t len, bool utf8)
{
    if (len == 0) return false;
    if (s[0] == '[') return domain_literal(s, len);
    /* LDH dot-atom labels: alnum/hyphen, no leading/trailing '-' per
     * label; "io"/"123.com"/"c--n.com" valid; non-ASCII only for the
     * idn flavor */
    size_t label = 0;
    for (size_t i = 0; i <= len; i++) {
        unsigned char c = i < len ? (unsigned char)s[i] : '.';
        if (i == len || c == '.') {
            if (label == 0) return false;
            if (s[i - label] == '-' || s[i - 1] == '-') return false;
            label = 0;
            if (i == len) break;
        } else if (is_ascii_alnum(c) || c == '-' || (utf8 && c >= 0x80)) {
            label++;
        } else {
            return false;
        }
    }
    return true;
}

static bool check_email_generic(const char *s, size_t len, bool utf8)
{
    if (len == 0) return false;
    for (size_t i = 0; i < len; i++) {
        /* space/tab ride inside quoted locals; the unquoted paths
         * reject them via atext/label rules. CRLF and controls are
         * banned outright (quoted or not); non-ASCII only for the idn
         * flavor. */
        unsigned char c = (unsigned char)s[i];
        if (c == '\n' || c == '\r') return false;
        if (c < 0x20 && c != ' ' && c != '\t') return false;
        if (!utf8 && c >= 0x7f) return false;
    }
    size_t lend = 0, at = 0;
    if (s[0] == '"') {
        /* quoted local: scan the closing quote, then exactly one @ */
        if (!quoted_local(s, len, &lend, utf8)) return false;
        if (lend >= len || s[lend] != '@') return false;
        at = lend;
    } else {
        int nat = 0;
        for (size_t i = 0; i < len; i++) {
            if (s[i] == '@') {
                nat++;
                at = i;
            }
        }
        if (nat != 1) return false;
        lend = at;
        if (!dot_atom(s, at, true, utf8)) return false;
    }
    return check_domain(s + at + 1, len - at - 1, utf8);
}

static bool check_email(const char *s, size_t len)
{
    return check_email_generic(s, len, false);
}

static bool check_idn_email(const char *s, size_t len)
{
    return check_email_generic(s, len, true);
}

/* dotted quad, each group 1-3 digits without a leading zero (unless
 * the group is exactly "0"), at most 255 */
static bool check_ipv4(const char *s, size_t len) {
    size_t pos = 0;
    for (int g = 0; g < 4; g++) {
        if (g > 0) {
            if (pos >= len || s[pos] != '.') return false;
            pos++;
        }
        size_t start = pos;
        while (pos < len && is_digit(s[pos])) pos++;
        size_t n = pos - start;
        if (n < 1 || n > 3) return false;
        if (n > 1 && s[start] == '0') return false;
        int v = 0;
        for (size_t i = start; i < pos; i++) v = v * 10 + (s[i] - '0');
        if (v > 255) return false;
    }
    return pos == len;
}

/* IPv6 per the std parser the crate uses: 8 groups of 1-4 hex digits
 * with one optional "::" compression and an optional IPv4 tail */
static bool group_ok(const char *t, size_t n) {
    if (n < 1 || n > 4) return false;
    for (size_t i = 0; i < n; i++) {
        if (!is_hex(t[i])) return false;
    }
    return true;
}

static bool check_ipv6(const char *s, size_t len) {
    /* '::' compresses once: a colon run longer than 2 (':::') is
     * invalid outright ('1:2:3:4:5:::8') */
    size_t run = 0;
    for (size_t i = 0; i < len; i++) {
        run = s[i] == ':' ? run + 1 : 0;
        if (run > 2) return false;
    }
    const char *tok[9];
    size_t tl[9];
    size_t n = 0, start = 0;
    for (size_t i = 0; i <= len; i++) {
        if (i == len || s[i] == ':') {
            if (n >= 9) return false;
            tok[n] = s + start;
            tl[n] = i - start;
            n++;
            start = i + 1;
        }
    }
    size_t empty_pos[9], ne = 0;
    for (size_t i = 0; i < n; i++) {
        if (tl[i] == 0) empty_pos[ne++] = i;
    }
    bool v4 = tl[n - 1] > 0 && memchr(tok[n - 1], '.', tl[n - 1]) != NULL;
    if (ne == 0) {
        if (n != (v4 ? 7 : 8)) return false;
    } else if (ne == 1) {
        /* a single empty token is a mid-string "::" ("a::b"); at the
         * ends it would be a lone leading/trailing colon */
        if (empty_pos[0] == 0 || empty_pos[0] == n - 1) return false;
    } else if (ne == 2) {
        if (empty_pos[1] != empty_pos[0] + 1) return false;
    } else if (ne == 3) {
        if (n != 3) return false; /* the bare "::" */
    } else {
        return false;
    }
    size_t groups = 0;
    for (size_t i = 0; i < n; i++) {
        if (tl[i] == 0) continue;
        if (i == n - 1 && v4) {
            if (!check_ipv4(tok[i], tl[i])) return false;
            groups += 2;
        } else {
            if (!group_ok(tok[i], tl[i])) return false;
            groups += 1;
        }
    }
    return ne == 0 ? groups == 8 : groups <= 7;
}

/* absolute URL, the practical shape of the url-crate parse: a scheme
 * (letter-first, alnum/+/-/.), a colon, and no control characters or
 * spaces anywhere */
/* ---- RFC 3986 / RFC 3987 (iri) component parse — the unified
 * suite runs the assert-flavored uri family against a real grammar:
 * scheme [//authority] path ? query # fragment, pct = %HH only,
 * reg-name/port/host-literal rules, bare IPv6 hosts rejected,
 * non-ASCII only for iri. ---- */

static size_t utf8_one(const char *s, size_t len, uint32_t *cp) {
    unsigned char c = (unsigned char)s[0];
    if (c < 0x80) { *cp = c; return 1; }
    size_t n;
    uint32_t v;
    if ((c & 0xE0) == 0xC0) { n = 2; v = c & 0x1F; }
    else if ((c & 0xF0) == 0xE0) { n = 3; v = c & 0x0F; }
    else if ((c & 0xF8) == 0xF0) { n = 4; v = c & 0x07; }
    else return 0;
    if (len < n) return 0;
    for (size_t k = 1; k < n; k++) {
        unsigned char cc = (unsigned char)s[k];
        if ((cc & 0xC0) != 0x80) return 0;
        v = (v << 6) | (cc & 0x3F);
    }
    *cp = v;
    return n;
}

static bool uri_subdelim(uint32_t cp) {
    switch (cp) {
    case '!': case '$': case '&': case '\'': case '(': case ')':
    case '*': case '+': case ',': case ';': case '=': return true;
    }
    return false;
}

static bool uri_unreserved(uint32_t cp, bool unicode) {
    if (cp < 0x80) return is_ascii_alnum((char)cp) || cp == '-' ||
                            cp == '.' || cp == '_' || cp == '~';
    return unicode; /* iri: any non-ASCII code point is an iunreserved */
}

/* pchar / '/' / '?' across [from, to): pct must be %HH, disallowed
 * bytes and non-iri non-ASCII fail */
static bool uri_span_ok(const char *s, size_t from, size_t to,
                        bool unicode) {
    size_t i = from;
    while (i < to) {
        if (s[i] == '%') {
            if (i + 2 >= to || !is_hex(s[i + 1]) || !is_hex(s[i + 2]))
                return false;
            i += 3;
            continue;
        }
        if (s[i] == '/' || s[i] == '?' || s[i] == '#') { i++; continue; }
        uint32_t cp;
        size_t adv = utf8_one(s + i, to - i, &cp);
        if (adv == 0) return false;
        if (uri_unreserved(cp, unicode) || uri_subdelim(cp) ||
            cp == ':' || cp == '@') {
            i += adv;
            continue;
        }
        return false;
    }
    return true;
}

static bool uri_reg_name(const char *s, size_t from, size_t to,
                         bool unicode) {
    size_t i = from;
    while (i < to) {
        if (s[i] == '%') {
            if (i + 2 >= to || !is_hex(s[i + 1]) || !is_hex(s[i + 2]))
                return false;
            i += 3;
            continue;
        }
        uint32_t cp;
        size_t adv = utf8_one(s + i, to - i, &cp);
        if (adv == 0) return false;
        if (!(uri_unreserved(cp, unicode) || uri_subdelim(cp))) return false;
        i += adv;
    }
    return true;
}

static bool check_uri_generic(const char *s, size_t len,
                              bool require_scheme, bool unicode) {
    if (len == 0) return !require_scheme;
    size_t i = 0;
    bool has_scheme = false;
    bool via_authority = false;
    /* scheme = ALPHA *( ALPHA / DIGIT / '+' / '-' / '.' ) ':' */
    if (((unsigned char)s[0] | 32) >= 'a' && ((unsigned char)s[0] | 32) <= 'z') {
        size_t j = 1;
        while (j < len) {
            unsigned char c = (unsigned char)s[j];
            if (c == ':') { has_scheme = true; i = j + 1; break; }
            if (!(is_ascii_alnum(c) || c == '+' || c == '-' || c == '.'))
                break;
            j++;
        }
    }
    if (!has_scheme) {
        if (require_scheme) return false;
        /* relative reference */
        if (len >= 2 && s[0] == '/' && s[1] == '/') {
            i = 2; /* authority below, shared path */
            via_authority = true;
        } else {
            /* path-noscheme / path-absolute / empty */
            size_t qend = len;
            for (size_t k = 0; k < len; k++) {
                if (s[k] == '?' || s[k] == '#') { qend = k; break; }
            }
            if (s[0] != '/') {
                /* the first segment may not carry ':' unless it opens
                 * with a dot segment ('./this:that' is fine) */
                size_t seg = 0;
                while (seg < qend && s[seg] != '/') seg++;
                if (seg > 0 && s[0] != '.') {
                    for (size_t k = 0; k < seg; k++) {
                        if (s[k] == ':') return false;
                    }
                }
            }
            return uri_span_ok(s, 0, len, unicode);
        }
    }
    /* authority: '//' after the scheme, or a leading '//' in a
     * relative reference */
    size_t rest_from = i;
    if (via_authority || (i + 1 < len && s[i] == '/' && s[i + 1] == '/')) {
        size_t auth = via_authority ? i : i + 2;
        size_t aend = auth;
        while (aend < len && s[aend] != '/' && s[aend] != '?' &&
               s[aend] != '#') {
            aend++;
        }
        size_t host_start = auth;
        size_t nat = 0, at = (size_t)-1;
        for (size_t k = auth; k < aend; k++) {
            if (s[k] == '@') { nat++; at = k; }
        }
        if (nat > 1) return false; /* '@' cannot ride in userinfo */
        if (nat == 1) {
            if (!uri_span_ok(s, host_start, at, unicode)) return false;
            host_start = at + 1;
        }
        if (host_start < aend && s[host_start] == '[') {
            size_t close = host_start;
            while (close < aend && s[close] != ']') close++;
            if (close >= aend) return false;
            if (!check_ipv6(s + host_start + 1, close - host_start - 1))
                return false;
            if (close + 1 < aend && s[close + 1] == ':') {
                for (size_t k = close + 2; k < aend; k++) {
                    if (!is_digit((unsigned char)s[k])) return false;
                }
            } else if (close + 1 != aend) {
                return false;
            }
        } else {
            /* reg-name up to an optional ':' port (digits only) */
            size_t cend = aend;
            for (size_t k = host_start; k < aend; k++) {
                if (s[k] == ':') { cend = k; break; }
                if (s[k] == '[' || s[k] == ']') return false;
            }
            if (!uri_reg_name(s, host_start, cend, unicode)) return false;
            if (cend < aend) {
                if (cend + 1 == aend) return false; /* 'joe.bloggs@'-class:
                                                        an empty port is
                                                        not a port */
                for (size_t k = cend + 1; k < aend; k++) {
                    if (!is_digit((unsigned char)s[k])) return false;
                }
            }
        }
        rest_from = aend;
    }
    return uri_span_ok(s, rest_from, len, unicode);
}

static bool check_uri(const char *s, size_t len) {
    return check_uri_generic(s, len, true, false);
}

static bool check_uri_reference(const char *s, size_t len) {
    return check_uri_generic(s, len, false, false);
}

static bool check_iri_like(const char *s, size_t len, bool require_scheme) {
    return check_uri_generic(s, len, require_scheme, true);
}

/* JSON pointer (RFC 6901): empty, or '/'-led with '~' only in the
 * escapes ~0/~1 */
static bool check_json_pointer(const char *s, size_t len) {
    if (len == 0) return true;
    if (s[0] != '/') return false;
    for (size_t i = 1; i < len; i++) {
        if (s[i] == '~') {
            if (i + 1 >= len || (s[i + 1] != '0' && s[i + 1] != '1')) {
                return false;
            }
            i++;
        }
    }
    return true;
}

/* relative-json-pointer: non-negative base-10 index without a
 * leading zero, then "#" or a '/'-led pointer tail */
static bool check_relative_json_pointer(const char *s, size_t len) {
    /* RFC 6901: the prefix is "0" or a non-zero-leading integer, and
     * the bare integer IS a valid pointer (the # / json-pointer part
     * is optional) */
    size_t i = 0;
    if (len == 0) return false;
    if (s[0] == '0') {
        i = 1;
        if (i < len && is_digit(s[i])) return false; /* 01, 012... */
    } else if (s[0] >= '1' && s[0] <= '9') {
        i = 1;
        while (i < len && is_digit(s[i])) i++;
    } else {
        return false;
    }
    if (i == len) return true; /* the bare integer alone */
    if (s[i] == '#') return i + 1 == len;
    if (s[i] != '/') return false;
    return check_json_pointer(s + i, len - i);
}

/* uuid: the crate's FromStr accepts the simple (32 hex) and
 * hyphenated (8-4-4-4-12) spellings, case-insensitive */
static bool check_uuid(const char *s, size_t len) {
    /* RFC 4122 abnf: exactly 8-4-4-4-12; the 32-char undashed form
     * is not a uuid-literal (the unified suite's stance) */
    if (len != 36) return false;
    if (s[8] != '-' || s[13] != '-' || s[18] != '-' || s[23] != '-')
        return false;
    for (size_t i = 0; i < len; i++) {
        if (i == 8 || i == 13 || i == 18 || i == 23) continue;
        if (!is_hex(s[i])) return false;
    }
    return true;
}

/* ISO 8601 duration, the practical core the iso8601 crate accepts:
 * an optional sign, P, then at least one nY/nM/nW/nD component and an
 * optional T-section of nH/nM/nS; a fraction is allowed on any unit */
static bool check_duration(const char *s, size_t len) {
    /* RFC 3339 duration, strict: P 1*DIGIT unit, canonical order
     * Y -> M -> D -> T -> H -> M -> S. Within each section the FIRST
     * component may be any of the section's units, every following
     * one is exactly the next index (no skips, no duplicates:
     * PT1H2S fails, PT1M2S passes); W is legal ONLY as the sole
     * component; no sign, no fraction, ASCII digits. */
    if (len < 2 || s[0] != 'P') return false;
    size_t wpos = (size_t)-1;
    for (size_t i = 1; i < len; i++) {
        if (s[i] == 'W') wpos = i;
    }
    if (wpos != (size_t)-1) {
        if (wpos != len - 1) return false;
        for (size_t i = 1; i < len - 1; i++) {
            if (!is_digit((unsigned char)s[i])) return false;
        }
        return len > 2;
    }
    int last = -1;         /* canonical index of the previous unit */
    bool first_date = true;
    bool first_time = true;
    bool in_time = false;
    int components = 0;
    size_t i = 1;
    while (i < len) {
        if (s[i] == 'T') {
            if (in_time) return false;
            in_time = true;
            first_time = true;
            i++;
            if (i >= len) return false; /* trailing T */
            continue;
        }
        if (!is_digit((unsigned char)s[i])) return false;
        while (i < len && is_digit((unsigned char)s[i])) i++;
        if (i >= len) return false;
        int idx;
        switch (s[i]) {
        case 'Y': idx = 0; break;
        case 'M': idx = in_time ? 4 : 1; break;
        case 'D': idx = 2; break;
        case 'H': idx = 3; break;
        case 'S': idx = 5; break;
        default: return false;
        }
        if (idx == 2 && in_time) return false;  /* no date units in the
                                                   time section */
        if ((idx == 3 || idx == 5) && !in_time) return false;
        if (in_time) {
            if (first_time) {
                first_time = false;
            } else if (idx != last + 1) {
                return false;
            }
        } else {
            if (first_date) {
                first_date = false;
            } else if (idx != last + 1) {
                return false;
            }
        }
        last = idx;
        components++;
        i++;
    }
    return components > 0;
}

/* ----------------------------------------------------- uri-template -- */

/* RFC 6570: literals are the unreserved set, %XX pairs, unicode and
 * ":/?#[]@!$&'()*+,;="; expressions are '{' [op] varspec *(, varspec)
 * '}' with op from "+#./;?&", varname = alnum/underscore/pct with
 * single internal dots, and a ':' prefix (1-4 digits, no leading
 * zero) or '*' modifier — but never both */
static bool check_uri_template(const char *s, size_t len) {
    size_t i = 0;
    while (i < len) {
        char c = s[i];
        if (c == '%') {
            if (i + 2 >= len || !is_hex(s[i + 1]) || !is_hex(s[i + 2])) {
                return false;
            }
            i += 3;
            continue;
        }
        if (c == '{') {
            i++;
            if (i < len && strchr("+#./;?&", s[i]) != NULL) {
                i++;
            }
            for (;;) {
                /* one varspec: name (:N or *)? */
                size_t name = 0;
                bool dot = false;
                while (i < len && s[i] != '}' && s[i] != ',' &&
                       s[i] != ':' && s[i] != '*') {
                    if (s[i] == '%') {
                        if (i + 2 >= len || !is_hex(s[i + 1]) ||
                            !is_hex(s[i + 2])) {
                            return false;
                        }
                        i += 3;
                        dot = false;
                    } else if (is_ascii_alnum(s[i]) || s[i] == '_') {
                        i++;
                        dot = false;
                    } else if (s[i] == '.' && name > 0 && !dot) {
                        i++;
                        dot = true;
                    } else {
                        return false;
                    }
                    if (dot == false) name++;
                }
                if (name == 0 || dot) return false;
                if (i < len && s[i] == ':') {
                    i++;
                    size_t ds = i;
                    while (i < len && is_digit(s[i])) i++;
                    size_t dn = i - ds;
                    if (dn < 1 || dn > 4) return false;
                    if (s[ds] == '0') return false;
                } else if (i < len && s[i] == '*') {
                    i++;
                }
                if (i < len && s[i] == ',') {
                    i++;
                    continue; /* the next varspec is mandatory */
                }
                break;
            }
            if (i >= len || s[i] != '}') return false;
            i++; /* the closing brace */
            continue;
        }
        unsigned char b = (unsigned char)c;
        if (b <= 0x20 || b == 0x7F || c == '"' || c == '<' ||
            c == '>' || c == '\\' || c == '^' || c == '`' || c == '|' ||
            c == '}') {
            return false;
        }
        i++;
    }
    return true;
}

/* --------------------------------------------------------- semver -- */

/* numeric identifier: "0" or [1-9][0-9]* (no leading zeros) */
static bool semver_numeric(const char *s, size_t len) {
    if (len == 0 || len > 19) return false;
    if (s[0] == '0' && len > 1) return false;
    return all_digits(s, 0, len);
}

static bool pre_ident_ok(const char *s, size_t len) {
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        if (!(is_ascii_alnum(c) || c == '-')) return false;
    }
    return len > 0;
}

/* a dot-separated identifier list; numeric identifiers must not
 * carry leading zeros (semver spec 9/11) */
static bool pre_list_ok(const char *s, size_t len) {
    size_t start = 0;
    for (size_t i = 0; i <= len; i++) {
        if (i == len || s[i] == '.') {
            size_t n = i - start;
            if (!pre_ident_ok(s + start, n)) return false;
            bool numeric = true;
            for (size_t k = 0; k < n; k++) {
                if (!is_digit(s[start + k])) numeric = false;
            }
            if (numeric && !semver_numeric(s + start, n)) return false;
            start = i + 1;
        }
    }
    return true;
}

/* MAJOR.MINOR.PATCH, then an optional -prerelease and +build */
static bool check_semver(const char *s, size_t len) {
    size_t num_start[3] = {0, 0, 0}, num_len[3] = {0, 0, 0};
    size_t i = 0;
    for (int part = 0; part < 3; part++) {
        num_start[part] = i;
        while (i < len && is_digit(s[i])) i++;
        num_len[part] = i - num_start[part];
        if (!semver_numeric(s + num_start[part], num_len[part])) return false;
        if (part < 2) {
            if (i >= len || s[i] != '.') return false;
            i++;
        }
    }
    if (i == len) return true;
    if (s[i] == '-') {
        i++;
        size_t start = i;
        while (i < len && s[i] != '+') i++;
        if (!pre_list_ok(s + start, i - start)) return false;
    }
    if (i < len) {
        if (s[i] != '+') return false;
        i++;
        size_t start = i;
        while (i < len && s[i] != '+') i++;
        if (i != len) return false; /* a second '+' is invalid */
        if (!pre_list_ok(s + start, len - start)) return false;
    }
    return i == len;
}

/* semver requirement (the semver crate's VersionReq grammar): a
 * comma-separated comparator list; each is a version with an
 * optional operator (= > >= < <= ^ ~; bare means ^), where MINOR or
 * PATCH may be a wildcard (*, x, X), or a bare wildcard */
static bool check_semver_req(const char *s, size_t len) {
    size_t i = 0;
    bool any = false; /* at least one comparator */
    while (i < len) {
        while (i < len && (s[i] == ' ' || s[i] == '\t')) i++;
        if (i == len) break;
        if (s[i] == ',') return false; /* empty comparator */
        /* operator */
        if (s[i] == '=' || s[i] == '>' || s[i] == '<' || s[i] == '^' ||
            s[i] == '~') {
            i++;
            if (i < len && (s[i] == '=')) i++; /* >= or <= */
        }
        while (i < len && (s[i] == ' ' || s[i] == '\t')) i++;
        if (i >= len) return false;
        /* MAJOR */
        size_t start = i;
        while (i < len && is_digit(s[i])) i++;
        if (i > start) {
            if (!semver_numeric(s + start, i - start)) return false;
        } else if (s[i] == '*' || s[i] == 'x' || s[i] == 'X') {
            i++;
            any = true;
            /* a bare wildcard ends the comparator */
            while (i < len && (s[i] == ' ' || s[i] == '\t')) i++;
            if (i == len) return true;
            if (s[i] != ',') return false;
            i++;
            continue;
        } else {
            return false;
        }
        any = true;
        /* optional .MINOR */
        if (i < len && s[i] == '.') {
            i++;
            start = i;
            while (i < len && is_digit(s[i])) i++;
            if (i > start) {
                if (!semver_numeric(s + start, i - start)) return false;
            } else if (i < len && (s[i] == '*' || s[i] == 'x' ||
                                   s[i] == 'X')) {
                i++;
                any = true;
            } else {
                return false;
            }
            /* optional .PATCH */
            if (i < len && s[i] == '.') {
                i++;
                start = i;
                while (i < len && is_digit(s[i])) i++;
                if (i > start) {
                    if (!semver_numeric(s + start, i - start)) return false;
                } else if (i < len && (s[i] == '*' || s[i] == 'x' ||
                                       s[i] == 'X')) {
                    i++;
                    any = true;
                } else {
                    return false;
                }
                /* a full version may carry -prerelease */
                if (i < len && s[i] == '-') {
                    i++;
                    size_t ps = i;
                    while (i < len && s[i] != '+') i++;
                    if (!pre_list_ok(s + ps, i - ps)) return false;
                }
            }
        }
        while (i < len && (s[i] == ' ' || s[i] == '\t')) i++;
        if (i == len) break;
        if (s[i] != ',') return false;
        i++;
    }
    return any;
}

/* ---------------------------------------------------------- idna -- */

/* utf-8: decode one code point (strict: overlongs, surrogates and
 * out-of-range rejected); i advances past the sequence */
static bool idna_utf8_next(const char *s, size_t len, size_t *i,
                           unsigned *cp) {
    unsigned char b = (unsigned char)s[*i];
    size_t adv;
    unsigned v;
    if (b < 0x80) {
        adv = 1;
        v = b;
    } else if ((b & 0xE0) == 0xC0) {
        adv = 2;
        v = b & 0x1F;
    } else if ((b & 0xF0) == 0xE0) {
        adv = 3;
        v = b & 0x0F;
    } else if ((b & 0xF8) == 0xF0) {
        adv = 4;
        v = b & 0x07;
    } else {
        return false;
    }
    if (*i + adv > len) return false;
    for (size_t k = 1; k < adv; k++) {
        unsigned char c = (unsigned char)s[*i + k];
        if ((c & 0xC0) != 0x80) return false;
        v = (v << 6) | (c & 0x3F);
    }
    static const unsigned lo[4] = {0, 0x80, 0x800, 0x10000};
    if (v < lo[adv - 1] || v > 0x10FFFF ||
        (v >= 0xD800 && v <= 0xDFFF)) {
        return false;
    }
    *cp = v;
    *i += adv;
    return true;
}

static size_t idna_utf8_put(char *out, unsigned v) {
    if (v < 0x80) {
        out[0] = (char)v;
        return 1;
    }
    if (v < 0x800) {
        out[0] = (char)(0xC0 | (v >> 6));
        out[1] = (char)(0x80 | (v & 0x3F));
        return 2;
    }
    if (v < 0x10000) {
        out[0] = (char)(0xE0 | (v >> 12));
        out[1] = (char)(0x80 | ((v >> 6) & 0x3F));
        out[2] = (char)(0x80 | (v & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (v >> 18));
    out[1] = (char)(0x80 | ((v >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((v >> 6) & 0x3F));
    out[3] = (char)(0x80 | (v & 0x3F));
    return 4;
}

static size_t idna_utf8_len(unsigned v) {
    return v < 0x80 ? 1 : v < 0x800 ? 2 : v < 0x10000 ? 3 : 4;
}

/* range-table lookups (the tables are sorted by `first`) */
static const idna_map_row *idna_map_of(unsigned v) {
    size_t lo = 0, hi = idna_map_len;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (idna_map[mid].last < v) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (lo < idna_map_len && idna_map[lo].first <= v) return &idna_map[lo];
    return NULL;
}

static unsigned char idna_range_of(const idna_range_row *t, size_t n,
                                   unsigned v) {
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (t[mid].last < v) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (lo < n && t[lo].first <= v) return t[lo].v;
    return 0;
}

#define idna_bidi_of(v) idna_range_of(idna_bidi, idna_bidi_len, (v))
#define idna_join_of(v) idna_range_of(idna_join, idna_join_len, (v))
#define idna_script_of(v) idna_range_of(idna_script, idna_script_len, (v))
#define idna_ccc_of(v) ((int)idna_range_of(idna_ccc, idna_ccc_len, (v)))

static bool idna_is_virama(unsigned v) {
    for (size_t i = 0; i < idna_virama_len; i++) {
        if (idna_virama[i] == v) return true;
    }
    return false;
}

/* gc=M (UTS-46 validity criterion 6: no leading combining mark) */
static bool idna_is_mark(unsigned v) {
    size_t lo = 0, hi = idna_mark_len;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (idna_mark[mid].last < v) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo < idna_mark_len && idna_mark[lo].first <= v;
}

static const idna_decomp_row *idna_decomp_of(unsigned v) {
    size_t lo = 0, hi = idna_decomp_len;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (idna_decomp[mid].cp < v) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (lo < idna_decomp_len && idna_decomp[lo].cp == v)
        return &idna_decomp[lo];
    return NULL;
}

static unsigned idna_comp_of(unsigned a, unsigned b) {
    for (size_t i = 0; i < idna_comp_len; i++) {
        if (idna_comp[i].a == a && idna_comp[i].b == b) return idna_comp[i].cp;
    }
    return 0;
}

/* hangul constants (the algorithmic decompositions/compositions) */
#define IDNA_SBASE 0xAC00
#define IDNA_LBASE 0x1100
#define IDNA_VBASE 0x1161
#define IDNA_TBASE 0x11A7
#define IDNA_LCOUNT 19
#define IDNA_VCOUNT 21
#define IDNA_TCOUNT 28
#define IDNA_NCOUNT (IDNA_VCOUNT * IDNA_TCOUNT)
#define IDNA_SCOUNT (IDNA_LCOUNT * IDNA_NCOUNT)

/* NFC (canonical decomposition, ordering, composition); in place,
 * answers the new length, or 0 when the scratch runs out */
static size_t idna_nfc(unsigned *cp, size_t n, size_t cap) {
    unsigned tmp[2048];
    size_t m = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned v = cp[i];
        if (v >= IDNA_SBASE && v < IDNA_SBASE + IDNA_SCOUNT) {
            unsigned sidx = v - IDNA_SBASE;
            tmp[m++] = IDNA_LBASE + sidx / IDNA_NCOUNT;
            tmp[m++] = IDNA_VBASE + (sidx % IDNA_NCOUNT) / IDNA_TCOUNT;
            unsigned t = sidx % IDNA_TCOUNT;
            if (t != 0) tmp[m++] = IDNA_TBASE + t;
        } else {
            const idna_decomp_row *d = idna_decomp_of(v);
            if (d != NULL) {
                for (unsigned k = 0; k < d->len; k++) {
                    if (m >= cap || m >= sizeof tmp / sizeof tmp[0]) return 0;
                    tmp[m++] = idna_seq_pool[d->off + k];
                }
            } else {
                if (m >= cap || m >= sizeof tmp / sizeof tmp[0]) return 0;
                tmp[m++] = v;
            }
        }
    }
    /* canonical order: stable bubble on combining class */
    for (;;) {
        bool moved = false;
        for (size_t i = 1; i < m; i++) {
            int a = idna_ccc_of(tmp[i - 1]);
            int b = idna_ccc_of(tmp[i]);
            if (a > b && b != 0) {
                unsigned t = tmp[i - 1];
                tmp[i - 1] = tmp[i];
                tmp[i] = t;
                moved = true;
            }
        }
        if (!moved) break;
    }
    /* compose: blocked iff a char between starter and current has a
     * combining class >= the current's; a successful merge removes
     * the blocker, so the last class stays at the starter's 0 */
    size_t out = 0;
    size_t starter = (size_t)-1;
    unsigned last_cc = 0;
    for (size_t i = 0; i < m; i++) {
        unsigned v = tmp[i];
        unsigned cc = (unsigned)idna_ccc_of(v);
        bool merged = false;
        if (starter != (size_t)-1 && (last_cc == 0 || last_cc < cc)) {
            unsigned comp = idna_comp_of(tmp[starter], v);
            if (comp == 0 &&
                tmp[starter] >= IDNA_LBASE &&
                tmp[starter] < IDNA_LBASE + IDNA_LCOUNT &&
                v >= IDNA_VBASE && v < IDNA_VBASE + IDNA_VCOUNT) {
                comp = IDNA_SBASE +
                       (tmp[starter] - IDNA_LBASE) * IDNA_NCOUNT +
                       (v - IDNA_VBASE) * IDNA_TCOUNT;
            } else if (comp == 0 &&
                       tmp[starter] >= IDNA_SBASE &&
                       tmp[starter] < IDNA_SBASE + IDNA_SCOUNT &&
                       (tmp[starter] - IDNA_SBASE) % IDNA_TCOUNT == 0 &&
                       v >= IDNA_TBASE && v < IDNA_TBASE + IDNA_TCOUNT) {
                comp = tmp[starter] + (v - IDNA_TBASE);
            }
            if (comp != 0) {
                tmp[starter] = comp;
                merged = true;
            }
        }
        if (merged) continue;
        if (cc == 0) {
            starter = out;
            last_cc = 0;
        } else {
            last_cc = cc;
        }
        tmp[out++] = v;
    }
    for (size_t i = 0; i < out; i++) cp[i] = tmp[i];
    return out;
}

/* UTS #46 map (nontransitional): answers the code point count, or
 * SIZE_MAX on a disallowed character or malformed utf-8 */
#define IDNA_CPS_CAP 1100
static size_t idna_ut46_map(const char *s, size_t len, unsigned *out) {
    size_t i = 0, n = 0;
    while (i < len) {
        unsigned v;
        if (!idna_utf8_next(s, len, &i, &v)) return SIZE_MAX;
        const idna_map_row *r = idna_map_of(v);
        if (r == NULL || r->st == IDNA_DISALLOWED) return SIZE_MAX;
        if (r->st == IDNA_IGNORED) continue;
        if (r->st == IDNA_MAPPED) {
            size_t k = 0;
            while (k < r->len) {
                unsigned mv;
                if (!idna_utf8_next((const char *)(idna_map_pool + r->off),
                                    r->len, &k, &mv)) {
                    return SIZE_MAX;
                }
                if (n >= IDNA_CPS_CAP) return SIZE_MAX;
                out[n++] = mv;
            }
            continue;
        }
        if (n >= IDNA_CPS_CAP) return SIZE_MAX;
        out[n++] = v;
    }
    return n;
}

/* punycode (RFC 3492 appendix C, the ANSI C reference adapted to the
 * engine's size_t/bool; the case-flag array is dropped — the
 * canonical re-encode form is lowercase) */
enum { PC_BASE = 36, PC_TMIN = 1, PC_TMAX = 26, PC_SKEW = 38,
       PC_DAMP = 700, PC_INITIAL_BIAS = 72, PC_INITIAL_N = 0x80 };

static unsigned pc_decode_digit(char c) {
    unsigned u = (unsigned char)c;
    return u - 48 < 10 ? u - 22 : u - 65 < 26 ? u - 65 :
           u - 97 < 26 ? u - 97 : PC_BASE;
}

static char pc_encode_digit(unsigned d) {
    return (char)(d + 22 + 75 * (d < 26));
}

static unsigned pc_adapt(unsigned delta, unsigned numpoints,
                         bool firsttime) {
    unsigned k;
    delta = firsttime ? delta / PC_DAMP : delta >> 1;
    delta += delta / numpoints;
    for (k = 0; delta > ((PC_BASE - PC_TMIN) * PC_TMAX) / 2; k += PC_BASE) {
        delta /= PC_BASE - PC_TMIN;
    }
    return k + (PC_BASE - PC_TMIN + 1) * delta / (delta + PC_SKEW);
}

static int pc_encode(const unsigned *input, size_t input_length,
                     char *output, size_t *output_length) {
    size_t n, delta, h, b, out, max_out, bias, j, m, q, k, t;
    n = PC_INITIAL_N;
    delta = out = 0;
    max_out = *output_length;
    bias = PC_INITIAL_BIAS;
    for (j = 0; j < input_length; ++j) {
        if (input[j] < 0x80) {
            if (max_out - out < 2) return -2;
            output[out++] = (char)input[j];
        }
    }
    h = b = out;
    if (b > 0) output[out++] = '-';
    while (h < input_length) {
        for (m = SIZE_MAX, j = 0; j < input_length; ++j) {
            if (input[j] >= n && input[j] < m) m = input[j];
        }
        if (m - n > (SIZE_MAX - delta) / (h + 1)) return -3;
        delta += (m - n) * (h + 1);
        n = m;
        for (j = 0; j < input_length; ++j) {
            if (input[j] < n) {
                if (++delta == 0) return -3;
            }
            if (input[j] == n) {
                for (q = delta, k = PC_BASE;; k += PC_BASE) {
                    if (out >= max_out) return -2;
                    t = k <= bias ? PC_TMIN :
                        k >= bias + PC_TMAX ? PC_TMAX : k - bias;
                    if (q < t) break;
                    output[out++] =
                        pc_encode_digit(t + (q - t) % (PC_BASE - t));
                    q = (q - t) / (PC_BASE - t);
                }
                output[out++] = pc_encode_digit(q);
                bias = pc_adapt((unsigned)delta, (unsigned)(h + 1), h == b);
                delta = 0;
                ++h;
            }
        }
        ++delta, ++n;
    }
    *output_length = out;
    return 0;
}

static int pc_decode(const char *input, size_t input_length,
                     unsigned *output, size_t *output_length) {
    unsigned n, bias, digit, t;
    size_t out = 0, i = 0, max_out = *output_length, b, j, in, oldi, w, k;
    n = PC_INITIAL_N;
    bias = PC_INITIAL_BIAS;
    for (b = j = 0; j < input_length; ++j) {
        if ((unsigned char)input[j] == '-') b = j;
    }
    if (b > max_out) return -2;
    for (j = 0; j < b; ++j) {
        if ((unsigned char)input[j] >= 0x80) return -1;
        output[out++] = (unsigned char)input[j];
    }
    for (in = b > 0 ? b + 1 : 0; in < input_length; ++out) {
        for (oldi = i, w = 1, k = PC_BASE;; k += PC_BASE) {
            if (in >= input_length) return -1;
            digit = pc_decode_digit(input[in++]);
            if (digit >= PC_BASE) return -1;
            if (digit > (SIZE_MAX - i) / w) return -3;
            i += digit * w;
            t = k <= bias ? PC_TMIN :
                k >= bias + PC_TMAX ? PC_TMAX : k - bias;
            if (digit < t) break;
            if (w > SIZE_MAX / (PC_BASE - t)) return -3;
            w *= (PC_BASE - t);
        }
        bias = pc_adapt((unsigned)(i - oldi), (unsigned)(out + 1),
                        oldi == 0);
        if (i / (out + 1) > SIZE_MAX - n) return -3;
        n += (unsigned)(i / (out + 1));
        i %= (out + 1);
        if (out >= max_out) return -2;
        if (n > 0x10FFFF || (n >= 0xD800 && n <= 0xDFFF)) return -1;
        memmove(output + i + 1, output + i, (out - i) * sizeof *output);
        output[i++] = n;
    }
    *output_length = out;
    return 0;
}

/* RFC 5892 appendix A: the nine context rules, evaluated on a label */
static bool idna_context_ok(const unsigned *cp, size_t n) {
    bool an_arabic = false, an_ext = false, kana_ctx = false;
    for (size_t i = 0; i < n; i++) {
        if (cp[i] >= 0x0660 && cp[i] <= 0x0669) an_arabic = true;
        if (cp[i] >= 0x06F0 && cp[i] <= 0x06F9) an_ext = true;
        unsigned char sc = idna_script_of(cp[i]);
        if (sc == SC_HIRAGANA || sc == SC_KATAKANA || sc == SC_HAN) {
            kana_ctx = true;
        }
    }
    if (an_arabic && an_ext) return false; /* A.8 / A.9 */
    for (size_t i = 0; i < n; i++) {
        unsigned v = cp[i];
        unsigned before = i > 0 ? cp[i - 1] : 0;
        unsigned after = i + 1 < n ? cp[i + 1] : 0;
        switch (v) {
        case 0x200C: {
            /* A.1: virama, or the joining-type regexp */
            if (i > 0 && idna_is_virama(before)) break;
            size_t k = i;
            while (k > 0 && idna_join_of(cp[k - 1]) == J_T) k--;
            if (k == 0) return false;
            unsigned char jl = idna_join_of(cp[k - 1]);
            if (jl != J_L && jl != J_D) return false;
            k = i + 1;
            while (k < n && idna_join_of(cp[k]) == J_T) k++;
            if (k >= n) return false;
            unsigned char jr = idna_join_of(cp[k]);
            if (jr != J_R && jr != J_D) return false;
            break;
        }
        case 0x200D: /* A.2 */
            if (i == 0 || !idna_is_virama(before)) return false;
            break;
        case 0x00B7: /* A.3 */
            if (before != 'l' || after != 'l') return false;
            break;
        case 0x0375: /* A.4 */
            if (i + 1 >= n || idna_script_of(after) != SC_GREEK)
                return false;
            break;
        case 0x05F3: /* A.5 / A.6 */
        case 0x05F4:
            if (i == 0 || idna_script_of(before) != SC_HEBREW) return false;
            break;
        case 0x30FB: /* A.7 */
            if (!kana_ctx) return false;
            break;
        default:
            break;
        }
    }
    return true;
}

/* RFC 5893: the six bidi rules; applies to every label of a bidi
 * domain (labels outside bidi domains ride free) */
static bool idna_bidi_ok(const unsigned *cp, size_t n, bool in_bidi_domain) {
    unsigned char cls[256];
    bool rtl = false;
    if (n == 0 || n > sizeof cls) return false;
    for (size_t i = 0; i < n; i++) {
        cls[i] = idna_bidi_of(cp[i]);
        if (cls[i] == B_R || cls[i] == B_AL || cls[i] == B_AN) rtl = true;
    }
    if (!rtl) {
        if (!in_bidi_domain) return true;
        /* an LTR label: rule 1 (first char L), rule 5, rule 6 */
        if (cls[0] != B_L) return false;
        for (size_t i = 0; i < n; i++) {
            switch (cls[i]) {
            case B_L: case B_EN: case B_ES: case B_CS: case B_ET:
            case B_ON: case B_BN: case B_NSM:
                break;
            default:
                return false;
            }
        }
        size_t e = n;
        while (e > 0 && cls[e - 1] == B_NSM) e--;
        if (e == 0 || (cls[e - 1] != B_L && cls[e - 1] != B_EN))
            return false;
        return true;
    }
    /* an RTL label: rules 1-4 */
    if (cls[0] != B_R && cls[0] != B_AL) return false;
    bool has_en = false, has_an = false;
    for (size_t i = 0; i < n; i++) {
        switch (cls[i]) {
        case B_R: case B_AL: case B_AN: case B_EN: case B_ES:
        case B_CS: case B_ET: case B_ON: case B_BN: case B_NSM:
            break;
        default:
            return false;
        }
        if (cls[i] == B_EN) has_en = true;
        if (cls[i] == B_AN) has_an = true;
    }
    if (has_en && has_an) return false;
    size_t e = n;
    while (e > 0 && cls[e - 1] == B_NSM) e--;
    if (e == 0) return false;
    if (cls[e - 1] != B_R && cls[e - 1] != B_AL && cls[e - 1] != B_EN &&
        cls[e - 1] != B_AN) {
        return false;
    }
    return true;
}

static bool idna_label_chars_ok(const unsigned *cp, size_t n) {
    for (size_t i = 0; i < n; i++) {
        const idna_map_row *r = idna_map_of(cp[i]);
        if (r == NULL || r->st != IDNA_VALID) return false;
    }
    return true;
}

/* decode an A-label body (past xn--) and gate the U-label: chars,
 * context rules, the 3rd/4th '--' reservation, and the canonical
 * re-encode; answers the decoded code points for the bidi pass */
static bool idna_alabel_decode(const char *body, size_t blen,
                               unsigned *dec, size_t *dn) {
    size_t cap = 128;
    *dn = cap;
    if (pc_decode(body, blen, dec, dn) != 0) return false;
    if (*dn == 0) return false;
    bool nonascii = false;
    for (size_t i = 0; i < *dn; i++) {
        if (dec[i] >= 0x80) nonascii = true;
    }
    if (!nonascii) return false; /* an A-label decodes to a U-label */
    if (idna_is_mark(dec[0])) return false; /* criterion 6 */
    if (*dn >= 4 && dec[2] == '-' && dec[3] == '-') return false;
    char ub[256];
    size_t un = 0;
    for (size_t i = 0; i < *dn; i++) un += idna_utf8_put(ub + un, dec[i]);
    if (un > 63) return false;
    if (!idna_label_chars_ok(dec, *dn)) return false;
    if (!idna_context_ok(dec, *dn)) return false;
    char enc[128];
    size_t en = sizeof enc;
    if (pc_encode(dec, *dn, enc, &en) != 0) return false;
    if (en != blen) return false;
    for (size_t i = 0; i < blen; i++) {
        char c = body[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        if (c != enc[i]) return false;
    }
    return true;
}

/* a direct (non-ACE) label: chars, context, and the wire-form length;
 * hyphen edges and the '--' reservation ride RFC 5891 4.2.3.1 */
static bool idna_ulabel_ok(const unsigned *cp, size_t n) {
    if (n == 0 || n > 63) return false;
    if (idna_is_mark(cp[0])) return false;
    if (cp[0] == '-' || cp[n - 1] == '-') return false;
    if (n >= 4 && cp[2] == '-' && cp[3] == '-') return false;
    if (!idna_label_chars_ok(cp, n)) return false;
    if (!idna_context_ok(cp, n)) return false;
    bool ascii = true;
    for (size_t i = 0; i < n; i++) {
        if (cp[i] >= 0x80) {
            ascii = false;
            break;
        }
    }
    if (ascii) return true; /* stays ASCII; the octet cap is the cps cap */
    char enc[128];
    size_t en = sizeof enc;
    if (pc_encode(cp, n, enc, &en) != 0) return false;
    if (en + 4 > 63) return false; /* xn-- plus the body must fit */
    return true;
}

static bool check_hostname(const char *s, size_t len) {
    if (len == 0 || s[0] == '-' || s[len - 1] == '-') return false;
    if (cp_count(s, len) > 255) return false;
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        if (!(is_ascii_alnum(c) || c == '-' || c == '.')) return false;
    }
    size_t label = 0;
    for (size_t i = 0; i <= len; i++) {
        if (i == len || s[i] == '.') {
            const char *lp = s + i - label;
            if (label == 0) return false; /* empty label: '.', 'a..b' */
            if (cp_count(lp, label) > 63) return false;
            /* an ACE label (any-case prefix, RFC 5890 lookup) must
             * decode to a valid, canonical U-label */
            if (label >= 4 && (lp[0] | 32) == 'x' && (lp[1] | 32) == 'n' &&
                (lp[2] | 32) == '-' && (lp[3] | 32) == '-') {
                unsigned dec[128];
                size_t dn;
                if (!idna_alabel_decode(lp + 4, label - 4, dec, &dn))
                    return false;
            }
            label = 0;
        } else {
            label++;
        }
    }
    return true;
}

#define IDNA_DOM_CAP 1100
static bool check_idn_hostname(const char *s, size_t len) {
    if (len == 0 || len > 253) return false;
    unsigned cps[IDNA_DOM_CAP];
    size_t n = idna_ut46_map(s, len, cps);
    if (n == SIZE_MAX) return false;
    size_t octets = 0;
    for (size_t i = 0; i < n; i++) octets += idna_utf8_len(cps[i]);
    if (octets > 253) return false;
    n = idna_nfc(cps, n, IDNA_DOM_CAP);
    if (n == 0) return false;
    /* bidi domains gate every label; the RTL-ness rides the U-label
     * form (a decoded ACE label counts) */
    bool bidi_domain = false;
    size_t ls = 0;
    for (size_t i = 0; i <= n; i++) {
        if (i != n && cps[i] != '.') continue;
        size_t ln = i - ls;
        const unsigned *lp = cps + ls;
        if (ln == 0) return false;
        if (ln >= 4 && lp[0] == 'x' && lp[1] == 'n' && lp[2] == '-' &&
            lp[3] == '-') {
            unsigned dec[128];
            size_t dn;
            char body[128];
            size_t bn = 0;
            for (size_t k = 4; k < ln; k++) {
                if (lp[k] >= 0x80 || bn >= sizeof body) return false;
                body[bn++] = (char)lp[k];
            }
            if (!idna_alabel_decode(body, bn, dec, &dn)) return false;
            if (idna_bidi_ok(dec, dn, true)) bidi_domain = true;
        } else if (idna_bidi_ok(lp, ln, true)) {
            bidi_domain = true;
        }
        ls = i + 1;
    }
    ls = 0;
    for (size_t i = 0; i <= n; i++) {
        if (i != n && cps[i] != '.') continue;
        size_t ln = i - ls;
        const unsigned *lp = cps + ls;
        if (ln >= 4 && lp[0] == 'x' && lp[1] == 'n' && lp[2] == '-' &&
            lp[3] == '-') {
            unsigned dec[128];
            size_t dn;
            char body[128];
            size_t bn = 0;
            for (size_t k = 4; k < ln; k++) {
                if (lp[k] >= 0x80 || bn >= sizeof body) return false;
                body[bn++] = (char)lp[k];
            }
            if (!idna_alabel_decode(body, bn, dec, &dn)) return false;
            if (!idna_bidi_ok(dec, dn, bidi_domain)) return false;
        } else {
            if (!idna_ulabel_ok(lp, ln)) return false;
            if (!idna_bidi_ok(lp, ln, bidi_domain)) return false;
        }
        ls = i + 1;
    }
    return true;
}

/* ----------------------------------------------------------- entry -- */

bool sch_format_string_check(int format, const char *s, size_t len,
                             teptris_arena *scratch) {
    switch (format) {
    case FMT_DATE: return check_date(s, len);
    case FMT_DATETIME: return check_datetime(s, len);
    case FMT_DATETIME_LOCAL: {
        /* the local shape: full-date T HH:MM:SS[.frac], no offset */
        if (len < 19 || !check_date(s, 10)) return false;
        if (s[10] != 'T' && s[10] != 't') return false;
        size_t used = check_hms(s + 11, len - 11, false, NULL, NULL, NULL);
        if (used == 0) return false;
        size_t pos = 11 + used;
        if (pos < len && s[pos] == '.') {
            size_t frac = 0;
            pos++;
            while (pos < len && is_digit(s[pos])) {
                pos++;
                frac++;
            }
            if (frac == 0) return false;
        }
        return pos == len;
    }
    case FMT_TIME: {
        size_t used = check_hms(s, len, false, NULL, NULL, NULL);
        if (used == 0) return false;
        size_t pos = used;
        if (pos < len && s[pos] == '.') {
            size_t frac = 0;
            pos++;
            while (pos < len && is_digit(s[pos])) {
                pos++;
                frac++;
            }
            if (frac == 0) return false; /* any precision >= 1 */
        }
        return len > pos && check_offset(s + pos, len - pos) == len - pos;
    }
    case FMT_TIME_LOCAL: {
        /* HH:MM:SS[.frac] with no offset (TOML's local time) */
        size_t used = check_hms(s, len, false, NULL, NULL, NULL);
        if (used == 0) return false;
        size_t pos = used;
        if (pos < len && s[pos] == '.') {
            size_t frac = 0;
            pos++;
            while (pos < len && is_digit(s[pos])) {
                pos++;
                frac++;
            }
            if (frac == 0) return false;
        }
        return pos == len;
    }
    case FMT_EMAIL: return check_email(s, len);
    case FMT_IDN_EMAIL: return check_idn_email(s, len);
    case FMT_HOSTNAME: return check_hostname(s, len);
    case FMT_IDN_HOSTNAME: return check_idn_hostname(s, len);
    case FMT_IPV4: return check_ipv4(s, len);
    case FMT_IPV6: return check_ipv6(s, len);
    case FMT_URI: return check_uri(s, len);
    case FMT_IRI: return check_iri_like(s, len, true);
    case FMT_URI_REFERENCE: return check_uri_reference(s, len);
    case FMT_IRI_REFERENCE: return check_iri_like(s, len, false);
    case FMT_JSON_POINTER: return check_json_pointer(s, len);
    case FMT_RELATIVE_JSON_POINTER:
        return check_relative_json_pointer(s, len);
    case FMT_REGEX: {
        char rerr[128] = {0};
        /* a compile trial in the engine's own NFA: the dialect is
         * teptris's (backreferences and lookaround reject here even
         * though the crate's engine takes them) */
        return sch_regex_compile(scratch, s, len, rerr, sizeof rerr) != NULL;
    }
    case FMT_URI_TEMPLATE: return check_uri_template(s, len);
    case FMT_UUID: return check_uuid(s, len);
    case FMT_DURATION: return check_duration(s, len);
    case FMT_SEMVER: return check_semver(s, len);
    case FMT_SEMVER_REQUIREMENT: return check_semver_req(s, len);
    default: return true;
    }
}
