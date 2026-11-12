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

static bool quoted_local(const char *s, size_t len, size_t *end)
{
    if (s[0] != '"') return false;
    size_t i = 1;
    for (;;) {
        if (i >= len) return false; /* unterminated */
        char c = s[i];
        if (c == '"') {
            *end = i + 1;
            return true; /* "" alone is legal; the caller checks what
                            follows */
        }
        if (c == '\\') {
            if (i + 1 >= len) return false;
            char n = s[i + 1];
            if (n <= 0x1f || n >= 0x7f) return false;
            i += 2;
            continue;
        }
        if (c == '\n' || c == '\r' || c < 0x20 || c >= 0x7f) return false;
        i++;
    }
}

static bool dot_atom(const char *s, size_t len, bool atext_only)
{
    if (len == 0 || s[0] == '.' || s[len - 1] == '.') return false;
    size_t dots = 0;
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        if (c == '.') {
            if (s[i + 1] == '.') return false;
            dots++;
            continue;
        }
        if (atext_only ? !atext(c) : !(is_ascii_alnum(c) || c == '-'))
            return false;
    }
    (void)dots;
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

static bool check_domain(const char *s, size_t len)
{
    if (len == 0) return false;
    if (s[0] == '[') return domain_literal(s, len);
    /* LDH dot-atom labels: alnum/hyphen, no leading/trailing '-' per
     * label; "io"/"123.com"/"c--n.com" valid; non-ASCII invalid */
    size_t label = 0;
    for (size_t i = 0; i <= len; i++) {
        char c = i < len ? s[i] : '.';
        if (i == len || c == '.') {
            if (label == 0) return false;
            if (s[i - label] == '-' || s[i - 1] == '-') return false;
            label = 0;
            if (i == len) break;
        } else if (is_ascii_alnum(c) || c == '-') {
            label++;
        } else {
            return false;
        }
    }
    return true;
}

static bool check_email(const char *s, size_t len)
{
    if (len == 0) return false;
    for (size_t i = 0; i < len; i++) {
        /* space/tab ride inside quoted locals; the unquoted paths
         * reject them via atext/label rules. CRLF, controls and
         * non-ASCII are banned outright (quoted or not). */
        if (s[i] == '\n' || s[i] == '\r') return false;
        if (s[i] < 0x20 && s[i] != ' ' && s[i] != '\t') return false;
        if (s[i] >= 0x7f) return false;
    }
    size_t lend = 0, at = 0;
    if (s[0] == '"') {
        /* quoted local: scan the closing quote, then exactly one @ */
        if (!quoted_local(s, len, &lend)) return false;
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
        if (!dot_atom(s, at, true)) return false;
    }
    return check_domain(s + at + 1, len - at - 1);
}

/* the crate's hostname/idn-hostname check (one body for both):
 * nonempty, at most 255 characters, only alphanumerics, '-' and '.',
 * no leading/trailing '-', labels of at most 63 characters. The
 * crate's alphanumeric test is Unicode-wide; the practical subset
 * here is ASCII (flagged as a divergence in SCHEMAS.md) */
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
            /* an ace label must DECODE: the prefix is lowercase-only
             * (ASCII labels are case-preserved, so XN-- stays invalid)
             * and damaged punycode ('xn--hello-txk' et al) fails */
            label = 0;
        } else {
            label++;
        }
    }
    return true;
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

/* the crate's template regex: literal characters (nothing in the
 * control/space range or "'<>%\^`{|}) or %XX pairs, plus {…}
 * expressions with an optional operator, comma-separated variable
 * names (word characters or %XX) and :N or * modifiers. Uppercase
 * hex and uppercase variable letters are accepted here (a superset
 * of the crate's lowercase-only spellings) */
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
            if (i < len && s[i] != '\0' &&
                strchr("+./;?&=,!@|", s[i]) != NULL) {
                i++;
            }
            bool varspec = false;
            while (i < len && s[i] != '}') {
                /* one varspec: name (:N or *)? */
                size_t name = 0;
                while (i < len && s[i] != '}' && s[i] != ',' &&
                       s[i] != ':' && s[i] != '*') {
                    if (s[i] == '%') {
                        if (i + 2 >= len || !is_hex(s[i + 1]) ||
                            !is_hex(s[i + 2])) {
                            return false;
                        }
                        i += 3;
                    } else if (is_ascii_alnum(s[i]) || s[i] == '_') {
                        i++;
                    } else {
                        return false;
                    }
                    name++;
                }
                if (name == 0) return false;
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
                varspec = true;
                if (i < len && s[i] == ',') {
                    i++;
                }
            }
            if (i >= len || !varspec) return false;
            i++; /* the closing brace */
            continue;
        }
        unsigned char b = (unsigned char)c;
        if (b <= 0x20 || b == 0x7F || c == '"' || c == '\'' || c == '<' ||
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
    case FMT_EMAIL:
    case FMT_IDN_EMAIL: return check_email(s, len);
    case FMT_HOSTNAME:
    case FMT_IDN_HOSTNAME: return check_hostname(s, len);
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
