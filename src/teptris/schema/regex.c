/* A practical ECMA-regex subset compiled to a Thompson NFA and
 * simulated in linear time: immune to ReDoS by construction (no
 * backtracking, no step limits to tune). Supports literals, '.',
 * classes ([...] with ranges, negation, \d \w \s and negations),
 * groups (non-capturing (?:...), named (?<name>...)), alternation,
 * greedy quantifiers (* + ? {n} {n,} {n,m}), anchors (^ $, whole-text
 * semantics), \b \B, \xNN, \uNNNN, and a leading (?i) ASCII
 * case-folding prefix.
 *
 * REJECTED AT COMPILE (clear diagnostics, never silently wrong):
 * backreferences, lookahead, lookbehind, \p{...}, other inline flags.
 * Lazy quantifiers are accepted: they change which match is found,
 * not whether one exists, which is all boolean `pattern` matching
 * needs.
 *
 * Matching is over Unicode code points (the caller hands UTF-8), so
 * '.' matches a whole code point and classes range over code points. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "regex.h"
#include "unicode_props.h"

/* ---------------------------------------------------------------- AST -- */

enum {
    RA_CHAR,
    RA_ANY,
    RA_CLASS,
    RA_SEQ,
    RA_ALT,
    RA_QUANT,
    RA_BOL,
    RA_EOL,
    RA_WORDB,
    RA_NWORDB
};

typedef struct ra_range {
    uint32_t lo;
    uint32_t hi;
} ra_range;

typedef struct ra ra;
struct ra {
    int kind;
    uint32_t cp;              /* RA_CHAR */
    bool ci;                  /* RA_CHAR: folded ASCII letter, match both cases */
    ra_range *ranges;         /* RA_CLASS */
    int n_ranges;
    bool negated;             /* RA_CLASS */
    ra **seq;                 /* RA_SEQ branches / RA_ALT branches */
    int n;
    ra *child;                /* RA_QUANT */
    long qmin, qmax;          /* RA_QUANT; qmax -1 = unbounded */
};

typedef struct {
    teptris_arena *a;
    const uint32_t *s;
    size_t n;
    size_t pos;
    bool fold;
    char *err;
    size_t err_cap;
} pctx;

static void pfail(pctx *c, const char *msg) {
    if (c->err != NULL && c->err[0] == '\0') {
        snprintf(c->err, c->err_cap, "%s", msg);
    }
}

static ra *rn(pctx *c, int kind) {
    ra *r = teptris_arena_alloc(c->a, sizeof(ra));
    *r = (ra){ .kind = kind };
    return r;
}

static uint32_t fold_cp(uint32_t cp) {
    if (cp >= 'A' && cp <= 'Z') return cp + 32;
    return cp;
}

static bool is_word_cp(uint32_t cp) {
    if (cp >= 'a' && cp <= 'z') return true;
    if (cp >= 'A' && cp <= 'Z') return true;
    if (cp >= '0' && cp <= '9') return true;
    return cp == '_';
}

static void utf8_to_cp(pctx *c, const char *src, size_t len) {
    size_t count = 0;
    for (size_t i = 0; i < len;) {
        unsigned char b = (unsigned char)src[i];
        size_t step = b < 0x80   ? 1
                      : b < 0xE0 ? 2
                      : b < 0xF0 ? 3
                                 : 4;
        i += step > len - i ? 1 : step;
        count++;
    }
    uint32_t *cps = teptris_arena_alloc(c->a, (count + 1) * sizeof(uint32_t));
    size_t o = 0;
    for (size_t i = 0; i < len;) {
        unsigned char b = (unsigned char)src[i];
        uint32_t cp;
        size_t step;
        if (b < 0x80) {
            cp = b;
            step = 1;
        } else if ((b & 0xE0) == 0xC0 && i + 1 < len &&
                   ((unsigned char)src[i + 1] & 0xC0) == 0x80) {
            cp = ((uint32_t)(b & 0x1F)) << 6 | ((unsigned char)src[i + 1] & 0x3F);
            step = 2;
        } else if ((b & 0xF0) == 0xE0 && i + 2 < len &&
                   ((unsigned char)src[i + 1] & 0xC0) == 0x80 &&
                   ((unsigned char)src[i + 2] & 0xC0) == 0x80) {
            cp = ((uint32_t)(b & 0x0F)) << 12 |
                 ((uint32_t)((unsigned char)src[i + 1] & 0x3F)) << 6 |
                 ((unsigned char)src[i + 2] & 0x3F);
            step = 3;
        } else if ((b & 0xF8) == 0xF0 && i + 3 < len &&
                   ((unsigned char)src[i + 1] & 0xC0) == 0x80 &&
                   ((unsigned char)src[i + 2] & 0xC0) == 0x80 &&
                   ((unsigned char)src[i + 3] & 0xC0) == 0x80) {
            cp = ((uint32_t)(b & 0x07)) << 18 |
                 ((uint32_t)((unsigned char)src[i + 1] & 0x3F)) << 12 |
                 ((uint32_t)((unsigned char)src[i + 2] & 0x3F)) << 6 |
                 ((unsigned char)src[i + 3] & 0x3F);
            step = 4;
        } else {
            cp = 0xFFFD; /* invalid byte: one replacement code point */
            step = 1;
        }
        cps[o++] = cp;
        i += step;
    }
    c->s = cps;
    c->n = o;
}

static int jhexv(uint32_t cp) {
    if (cp >= '0' && cp <= '9') return (int)(cp - '0');
    if (cp >= 'a' && cp <= 'f') return (int)(cp - 'a' + 10);
    if (cp >= 'A' && cp <= 'F') return (int)(cp - 'A' + 10);
    return -1;
}

/* add a range under the fold flag: ranges intersecting ASCII letters
 * add their case-swapped mirror so both cases always match */
static void cls_add(pctx *c, ra *cl, uint32_t lo, uint32_t hi) {
    if (c->fold) {
        /* an uppercase intersection mirrors DOWN (fold_cp); a
         * lowercase intersection mirrors UP (+32) */
        if (lo <= 'Z' && hi >= 'A') {
            uint32_t mlo = lo < 'A' ? 'A' : lo;
            uint32_t mhi = hi > 'Z' ? 'Z' : hi;
            if (mlo <= mhi) {
                cl->ranges[cl->n_ranges].lo = fold_cp(mlo);
                cl->ranges[cl->n_ranges].hi = fold_cp(mhi);
                cl->n_ranges++;
            }
        }
        if (lo <= 'z' && hi >= 'a') {
            uint32_t mlo = lo < 'a' ? 'a' : lo;
            uint32_t mhi = hi > 'z' ? 'z' : hi;
            if (mlo <= mhi) {
                cl->ranges[cl->n_ranges].lo = mlo - 32; /* raised */
                cl->ranges[cl->n_ranges].hi = mhi - 32;
                cl->n_ranges++;
            }
        }
    }
    cl->ranges[cl->n_ranges].lo = lo;
    cl->ranges[cl->n_ranges].hi = hi;
    cl->n_ranges++;
}

/* a single code point under fold: the folded char (plus the original
 * when the two differ) */
static void cls_add_cp(pctx *c, ra *cl, uint32_t cp) {
    if (c->fold && cp >= 'A' && cp <= 'Z') {
        cl->ranges[cl->n_ranges].lo = cp;
        cl->ranges[cl->n_ranges].hi = cp;
        cl->n_ranges++;
        cl->ranges[cl->n_ranges].lo = fold_cp(cp);
        cl->ranges[cl->n_ranges].hi = fold_cp(cp);
        cl->n_ranges++;
        return;
    }
    if (c->fold && cp >= 'a' && cp <= 'z') {
        cl->ranges[cl->n_ranges].lo = fold_cp(cp);
        cl->ranges[cl->n_ranges].hi = fold_cp(cp);
        cl->n_ranges++;
    }
    cl->ranges[cl->n_ranges].lo = cp;
    cl->ranges[cl->n_ranges].hi = cp;
    cl->n_ranges++;
}

/* ------------------------------------------------- \p{...} properties -- */

static void cls_reserve(pctx *c, ra *cl, size_t cap);

/* the property named by s[0..n) (decoded code points): binary search
 * over the sorted table */
static const teptris_uc_prop *uc_find(const uint32_t *s, size_t n) {
    int lo = 0, hi = teptris_n_unicode_props - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        const char *name = teptris_unicode_props[mid].name;
        int cmp = 0;
        for (size_t i = 0; i < n; i++) {
            unsigned char c = (unsigned char)name[i];
            if (c == '\0' || (uint32_t)c != s[i]) {
                cmp = (uint32_t)c < s[i] ? -1 : 1;
                break;
            }
        }
        if (cmp == 0 && name[n] == '\0') return &teptris_unicode_props[mid];
        if (cmp == 0) cmp = 1; /* longer table name: s sorts first */
        if (cmp < 0) lo = mid + 1;
        else hi = mid - 1;
    }
    return NULL;
}

/* the complement of sorted ranges over the whole codepoint space (the
 * caller reserved headroom) */
static void cls_append_complement(pctx *c, ra *cl,
                                  const teptris_uc_range *rs, int n) {
    (void)c;
    uint32_t expect = 0;
    for (int i = 0; i <= n; i++) {
        uint32_t lo = i < n ? rs[i].lo : 0x110000;
        uint32_t hi = i < n ? rs[i].hi : 0x110000;
        if (lo > expect) {
            cl->ranges[cl->n_ranges].lo = expect;
            cl->ranges[cl->n_ranges].hi = lo - 1;
            cl->n_ranges++;
        }
        if (hi + 1 > expect) expect = hi + 1;
    }
}

/* parse \p{...} / \P{...} / \p{^...} with c->pos after the letter;
 * the ranges (or their complement) append into cl */
static void prop_escape(pctx *c, ra *cl, bool negate_letter) {
    bool inner_neg = false;
    if (c->pos >= c->n || c->s[c->pos] != '{') {
        pfail(c, "\\p needs a {property} name");
        return;
    }
    c->pos++;
    if (c->pos < c->n && c->s[c->pos] == '^') {
        inner_neg = true;
        c->pos++;
    }
    size_t start = c->pos;
    while (c->pos < c->n && c->s[c->pos] != '}') c->pos++;
    if (c->pos >= c->n) {
        pfail(c, "unterminated \\p{...}");
        return;
    }
    size_t n = c->pos - start;
    c->pos++; /* the closing brace */
    if (n == 0) {
        pfail(c, "empty \\p{} property name");
        return;
    }
    const teptris_uc_prop *p = uc_find(c->s + start, n);
    if (p == NULL) {
        pfail(c, "unknown \\p{...} property name");
        return;
    }
    bool negate = negate_letter != inner_neg;
    cls_reserve(c, cl, negate ? (size_t)2 * p->n + 8 : (size_t)p->n + 8);
    if (negate) {
        cls_append_complement(c, cl, p->ranges, p->n);
    } else {
        for (int i = 0; i < p->n; i++) {
            cl->ranges[cl->n_ranges].lo = p->ranges[i].lo;
            cl->ranges[cl->n_ranges].hi = p->ranges[i].hi;
            cl->n_ranges++;
        }
    }
}

/* expand a class shorthand (\d \w \s) into its ranges */
static void cls_shorthand(pctx *c, ra *cl, uint32_t e) {
    if (e == 'd' || e == 'D') {
        cl->ranges[cl->n_ranges].lo = '0';
        cl->ranges[cl->n_ranges].hi = '9';
        cl->n_ranges++;
    } else if (e == 'w' || e == 'W') {
        cls_add(c, cl, 'a', 'z');
        cls_add(c, cl, 'A', 'Z');
        cls_add(c, cl, '0', '9');
        cls_add(c, cl, '_', '_');
    } else {
        cls_add_cp(c, cl, ' ');
        cls_add_cp(c, cl, '\t');
        cls_add_cp(c, cl, '\n');
        cls_add_cp(c, cl, '\r');
        cls_add_cp(c, cl, '\f');
        cls_add_cp(c, cl, '\v');
    }
}

static void cls_reserve(pctx *c, ra *cl, size_t cap) {
    void *nr = teptris_arena_alloc(c->a, cap * sizeof(*cl->ranges));
    if (cl->n_ranges > 0) {
        memcpy(nr, cl->ranges, (size_t)cl->n_ranges * sizeof(*cl->ranges));
    }
    cl->ranges = nr;
}

/* class escapes: returns the code point, or -1 for a shorthand handled
 * here, or -2 on error */
static long class_escape(pctx *c, ra *cl) {
    if (c->pos >= c->n) {
        pfail(c, "trailing backslash in pattern");
        return -2;
    }
    uint32_t e = c->s[c->pos];
    switch (e) {
    case 'd': case 'D': case 'w': case 'W': case 's': case 'S':
        c->pos++;
        cls_shorthand(c, cl, e);
        return -1;
    case 'p': case 'P':
        c->pos++;
        prop_escape(c, cl, e == 'P');
        return -1;
    case 'x': {
        c->pos++;
        if (c->pos + 2 > c->n) {
            pfail(c, "incomplete \\x escape");
            return -2;
        }
        int hi = jhexv(c->s[c->pos]);
        int lo = jhexv(c->s[c->pos + 1]);
        if (hi < 0 || lo < 0) {
            pfail(c, "invalid \\x escape");
            return -2;
        }
        c->pos += 2;
        return (long)(hi * 16 + lo);
    }
    case 'u': {
        c->pos++;
        if (c->pos + 4 > c->n) {
            pfail(c, "incomplete \\u escape");
            return -2;
        }
        long cp = 0;
        for (int k = 0; k < 4; k++) {
            int d = jhexv(c->s[c->pos + (size_t)k]);
            if (d < 0) {
                pfail(c, "invalid \\u escape");
                return -2;
            }
            cp = cp * 16 + d;
        }
        c->pos += 4;
        return cp;
    }
    case 'n': c->pos++; return '\n';
    case 'r': c->pos++; return '\r';
    case 't': c->pos++; return '\t';
    case 'f': c->pos++; return '\f';
    case 'v': c->pos++; return '\v';
    case '0': c->pos++; return '\0';
    default:
        if (e >= '1' && e <= '9') {
            pfail(c, "backreferences are not supported");
            return -2;
        }
        c->pos++;
        return (long)e;
    }
}

/* parse the body of a [...]; c->pos sits after '[' */
/* sort + merge a class's ranges in place (property tables arrive
 * sorted; literals interleave arbitrarily) so matching can binary
 * search and negation sees a canonical form */
static void cls_sort_merge(pctx *c, ra *cl) {
    (void)c;
    int n = cl->n_ranges;
    for (int i = 1; i < n; i++) {
        ra_range r = cl->ranges[i];
        int j = i - 1;
        while (j >= 0 && (cl->ranges[j].lo > r.lo)) {
            cl->ranges[j + 1] = cl->ranges[j];
            j--;
        }
        cl->ranges[j + 1] = r;
    }
    int w = 0;
    for (int i = 1; i < n; i++) {
        if (cl->ranges[i].lo <= cl->ranges[w].hi + 1) {
            if (cl->ranges[i].hi > cl->ranges[w].hi) {
                cl->ranges[w].hi = cl->ranges[i].hi;
            }
        } else {
            cl->ranges[++w] = cl->ranges[i];
        }
    }
    cl->n_ranges = w + 1;
}

static ra *rclass(pctx *c) {
    ra *cl = rn(c, RA_CLASS);
    if (c->pos < c->n && c->s[c->pos] == '^') {
        cl->negated = true;
        c->pos++;
    }
    size_t cap = 8;
    cls_reserve(c, cl, cap);
    bool first = true;
    while (c->pos < c->n) {
        if (c->s[c->pos] == ']' && !first) {
            c->pos++;
            cls_sort_merge(c, cl);
            return cl;
        }
        first = false;
        long lo;
        if (c->s[c->pos] == '\\') {
            c->pos++;
            lo = class_escape(c, cl);
            if (lo == -2) return NULL;
            if (lo == -1) continue; /* shorthand expanded */
        } else {
            lo = (long)c->s[c->pos++];
        }
        if (c->pos + 1 < c->n && c->s[c->pos] == '-' && c->s[c->pos + 1] != ']') {
            c->pos++;
            long hi;
            if (c->s[c->pos] == '\\') {
                c->pos++;
                hi = class_escape(c, cl);
                if (hi == -2) return NULL;
                if (hi == -1) {
                    pfail(c, "class shorthand as a range endpoint");
                    return NULL;
                }
            } else {
                hi = (long)c->s[c->pos++];
            }
            if (hi < lo) {
                pfail(c, "class range out of order");
                return NULL;
            }
            if ((size_t)cl->n_ranges + 2 >= cap) {
                cap *= 2;
                cls_reserve(c, cl, cap);
            }
            cls_add(c, cl, (uint32_t)lo, (uint32_t)hi);
        } else {
            if ((size_t)cl->n_ranges + 2 >= cap) {
                cap *= 2;
                cls_reserve(c, cl, cap);
            }
            cls_add_cp(c, cl, (uint32_t)lo);
        }
    }
    pfail(c, "unterminated character class");
    return NULL;
}

/* parse a quantifier after an atom: 1 set, 0 none (position restored),
 * -1 error */
static int rquant(pctx *c, long *qmin, long *qmax) {
    if (c->pos >= c->n) return 0;
    uint32_t ch = c->s[c->pos];
    if (ch == '*') {
        c->pos++;
        *qmin = 0; *qmax = -1;
    } else if (ch == '+') {
        c->pos++;
        *qmin = 1; *qmax = -1;
    } else if (ch == '?') {
        c->pos++;
        *qmin = 0; *qmax = 1;
    } else if (ch == '{') {
        size_t save = c->pos;
        c->pos++;
        long lo = 0;
        bool any = false;
        while (c->pos < c->n && c->s[c->pos] >= '0' && c->s[c->pos] <= '9') {
            lo = lo * 10 + (long)(c->s[c->pos] - '0');
            c->pos++;
            any = true;
        }
        if (!any) {
            c->pos = save;
            return 0; /* a literal '{' */
        }
        if (c->pos < c->n && c->s[c->pos] == '}') {
            c->pos++;
            *qmin = lo; *qmax = lo;
        } else if (c->pos < c->n && c->s[c->pos] == ',') {
            c->pos++;
            if (c->pos < c->n && c->s[c->pos] == '}') {
                c->pos++;
                *qmin = lo; *qmax = -1;
            } else {
                long hi = 0;
                bool anyh = false;
                while (c->pos < c->n && c->s[c->pos] >= '0' &&
                       c->s[c->pos] <= '9') {
                    hi = hi * 10 + (long)(c->s[c->pos] - '0');
                    c->pos++;
                    anyh = true;
                }
                if (!anyh || c->pos >= c->n || c->s[c->pos] != '}') {
                    c->pos = save;
                    return 0;
                }
                c->pos++;
                *qmin = lo; *qmax = hi;
                if (*qmax < *qmin) {
                    pfail(c, "quantifier range out of order");
                    return -1;
                }
            }
        } else {
            c->pos = save;
            return 0;
        }
    } else {
        return 0;
    }
    if (c->pos < c->n && c->s[c->pos] == '?') {
        c->pos++; /* lazy: no semantic difference for boolean matching */
    }
    return 1;
}

static ra *ralternation(pctx *c);

static ra *ratom(pctx *c) {
    if (c->pos >= c->n) return NULL;
    uint32_t ch = c->s[c->pos];
    switch (ch) {
    case ')':
        return NULL; /* the enclosing group's terminator */
    case '^':
        c->pos++;
        return rn(c, RA_BOL);
    case '$':
        c->pos++;
        return rn(c, RA_EOL);
    case '.':
        c->pos++;
        return rn(c, RA_ANY);
    case '[':
        c->pos++;
        return rclass(c);
    case '(': {
        c->pos++;
        if (c->pos < c->n && c->s[c->pos] == '?') {
            c->pos++;
            if (c->pos < c->n && c->s[c->pos] == ':') {
                c->pos++; /* non-capturing */
            } else if (c->pos < c->n &&
                       (c->s[c->pos] == '=' || c->s[c->pos] == '!')) {
                pfail(c, "lookahead is not supported");
                return NULL;
            } else if (c->pos < c->n && c->s[c->pos] == '<') {
                if (c->pos + 1 < c->n &&
                    (c->s[c->pos + 1] == '=' || c->s[c->pos + 1] == '!')) {
                    pfail(c, "lookbehind is not supported");
                    return NULL;
                }
                while (c->pos < c->n && c->s[c->pos] != '>') c->pos++;
                if (c->pos >= c->n) {
                    pfail(c, "unterminated group name");
                    return NULL;
                }
                c->pos++;
            } else {
                pfail(c, "inline flags other than a leading (?i) are not supported");
                return NULL;
            }
        }
        ra *g = rn(c, RA_ALT);
        ra *body = ralternation(c);
        if (body == NULL && c->err[0] != '\0') return NULL;
        g->seq = teptris_arena_alloc(c->a, sizeof(ra *));
        g->seq[0] = body != NULL ? body : rn(c, RA_SEQ); /* () = empty seq */
        g->n = 1;
        if (c->pos >= c->n || c->s[c->pos] != ')') {
            pfail(c, "unterminated group");
            return NULL;
        }
        c->pos++;
        return g;
    }
    case '\\': {
        c->pos++;
        if (c->pos >= c->n) {
            pfail(c, "trailing backslash in pattern");
            return NULL;
        }
        uint32_t e = c->s[c->pos];
        if (e == 'd' || e == 'w' || e == 's' || e == 'D' || e == 'W' ||
            e == 'S') {
            c->pos++;
            ra *cl = rn(c, RA_CLASS);
            cl->negated = (e == 'D' || e == 'W' || e == 'S');
            cls_reserve(c, cl, 8);
            cls_shorthand(c, cl, e);
            return cl;
        }
        if (e == 'p' || e == 'P') {
            c->pos++;
            ra *cl = rn(c, RA_CLASS);
            prop_escape(c, cl, e == 'P');
            if (c->err[0] != '\0') return NULL;
            return cl;
        }
        if (e == 'b') {
            c->pos++;
            return rn(c, RA_WORDB);
        }
        if (e == 'B') {
            c->pos++;
            return rn(c, RA_NWORDB);
        }
        long cp = class_escape(c, NULL);
        if (cp == -2) return NULL;
        if (cp == -1) {
            pfail(c, "class shorthand outside a class");
            return NULL;
        }
        ra *r = rn(c, RA_CHAR);
        r->cp = c->fold ? fold_cp((uint32_t)cp) : (uint32_t)cp;
        r->ci = c->fold && cp >= 'a' && cp <= 'z';
        return r;
    }
    default: {
        c->pos++;
        ra *r = rn(c, RA_CHAR);
        r->cp = c->fold ? fold_cp(ch) : ch;
        r->ci = c->fold && ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z'));
        return r;
    }
    }
}

static ra *rrepeat(pctx *c) {
    ra *atom = ratom(c);
    if (atom == NULL) return NULL;
    long qmin, qmax;
    int st = rquant(c, &qmin, &qmax);
    if (st < 0) return NULL;
    if (st == 0) return atom;
    if (atom->kind == RA_BOL || atom->kind == RA_EOL ||
        atom->kind == RA_WORDB || atom->kind == RA_NWORDB) {
        pfail(c, "cannot quantify an anchor");
        return NULL;
    }
    ra *q = rn(c, RA_QUANT);
    q->child = atom;
    q->qmin = qmin;
    q->qmax = qmax;
    return q;
}

static ra *rconcat(pctx *c) {
    size_t cap = 8;
    size_t n = 0;
    ra **seq = teptris_arena_alloc(c->a, cap * sizeof(*seq));
    for (;;) {
        /* alternation splits ABOVE concat: stop at the bar */
        if (c->pos < c->n && c->s[c->pos] == '|') break;
        ra *piece = rrepeat(c);
        if (piece == NULL) {
            if (c->err[0] != '\0') return NULL;
            break;
        }
        if (n == cap) {
            cap *= 2;
            void *ns = teptris_arena_alloc(c->a, cap * sizeof(*seq));
            memcpy(ns, seq, n * sizeof(*seq));
            seq = ns;
        }
        seq[n++] = piece;
    }
    ra *r = rn(c, RA_SEQ);
    r->seq = seq;
    r->n = (int)n;
    return r;
}

static ra *ralternation(pctx *c) {
    ra *first = rconcat(c);
    if (first == NULL) return NULL;
    if (c->pos >= c->n || c->s[c->pos] != '|') return first;
    size_t cap = 4;
    size_t n = 0;
    ra **branches = teptris_arena_alloc(c->a, cap * sizeof(*branches));
    branches[n++] = first;
    while (c->pos < c->n && c->s[c->pos] == '|') {
        c->pos++;
        ra *b = rconcat(c);
        if (b == NULL) return NULL;
        if (n == cap) {
            cap *= 2;
            void *nb = teptris_arena_alloc(c->a, cap * sizeof(*branches));
            memcpy(nb, branches, n * sizeof(*branches));
            branches = nb;
        }
        branches[n++] = b;
    }
    ra *r = rn(c, RA_ALT);
    r->seq = branches;
    r->n = (int)n;
    return r;
}

/* -------------------------------------------------------- NFA compile -- */

enum { RS_CHAR, RS_SPLIT, RS_MATCH, RS_BOL, RS_EOL, RS_WORDB, RS_NWORDB };

typedef struct {
    int kind;
    bool any;
    ra_range *ranges;
    int n_ranges;
    bool negated;
    int a, b;
} rstate;

struct sch_regex {
    rstate *states;
    int n_states;
    int start;
    int match;
};

#define REGEX_MAX_STATES 20000

typedef struct {
    teptris_arena *a;
    rstate *states;
    int n;
    int cap;
    char *err;
    size_t err_cap;
} nctx;

static int nstate(nctx *c, int kind) {
    if (c->n >= REGEX_MAX_STATES) {
        if (c->err[0] == '\0') {
            snprintf(c->err, c->err_cap, "pattern compiles to too many states");
        }
        return -1;
    }
    if (c->n == c->cap) {
        c->cap = c->cap == 0 ? 64 : c->cap * 2;
        void *ns = teptris_arena_alloc(c->a, (size_t)c->cap * sizeof(rstate));
        if (c->n > 0) memcpy(ns, c->states, (size_t)c->n * sizeof(rstate));
        c->states = ns;
    }
    rstate *s = &c->states[c->n];
    memset(s, 0, sizeof(*s));
    s->kind = kind;
    s->a = -1;
    s->b = -1;
    return c->n++;
}

/* The subgraph under construction is described by an ENTRY state (or
 * -1 when no state exists yet: the piece is pure epsilon) and a
 * DANGLING TAIL: out-slots that the next piece's entry (or, at the
 * very end, the match state) will be patched into. The entry NEVER
 * moves once set; later pieces only consume the tail. This shape makes
 * the entry-loss bug class structurally impossible. */
typedef struct {
    int state;
    bool is_b;
} pslot;

typedef struct {
    pslot *slots;
    size_t n, cap;
} plist;

static void plist_add(nctx *c, plist *p, int state, bool is_b) {
    if (p->n == p->cap) {
        p->cap = p->cap == 0 ? 8 : p->cap * 2;
        void *np = teptris_arena_alloc(c->a, p->cap * sizeof(pslot));
        if (p->n > 0) memcpy(np, p->slots, p->n * sizeof(pslot));
        p->slots = np;
    }
    p->slots[p->n].state = state;
    p->slots[p->n].is_b = is_b;
    p->n++;
}

typedef struct {
    int entry;
    plist tail;
} frag;

static void frag_eps(frag *f) {
    f->entry = -1;
    memset(&f->tail, 0, sizeof(f->tail));
}

static void frag_thread(nctx *c, frag *f, int to) {
    for (size_t i = 0; i < f->tail.n; i++) {
        if (f->tail.slots[i].is_b) {
            c->states[f->tail.slots[i].state].b = to;
        } else {
            c->states[f->tail.slots[i].state].a = to;
        }
    }
}

static bool emit_node(nctx *c, ra *r, frag *f) {
    switch (r->kind) {
    case RA_CHAR: {
        int s = nstate(c, RS_CHAR);
        if (s < 0) return false;
        if (r->ci) {
            /* a folded letter matches both cases (stored ascending:
             * cls_match binary-searches) */
            c->states[s].ranges = teptris_arena_alloc(c->a, 2 * sizeof(ra_range));
            c->states[s].ranges[0].lo = fold_cp(r->cp) - 32;
            c->states[s].ranges[0].hi = fold_cp(r->cp) - 32;
            c->states[s].ranges[1].lo = r->cp;
            c->states[s].ranges[1].hi = r->cp;
            c->states[s].n_ranges = 2;
        } else {
            c->states[s].ranges = teptris_arena_alloc(c->a, sizeof(ra_range));
            c->states[s].ranges[0].lo = r->cp;
            c->states[s].ranges[0].hi = r->cp;
            c->states[s].n_ranges = 1;
        }
        f->entry = s;
        memset(&f->tail, 0, sizeof(f->tail));
        plist_add(c, &f->tail, s, false);
        return true;
    }
    case RA_ANY: {
        int s = nstate(c, RS_CHAR);
        if (s < 0) return false;
        c->states[s].any = true;
        f->entry = s;
        memset(&f->tail, 0, sizeof(f->tail));
        plist_add(c, &f->tail, s, false);
        return true;
    }
    case RA_CLASS: {
        int s = nstate(c, RS_CHAR);
        if (s < 0) return false;
        c->states[s].ranges = r->ranges;
        c->states[s].n_ranges = r->n_ranges;
        c->states[s].negated = r->negated;
        f->entry = s;
        memset(&f->tail, 0, sizeof(f->tail));
        plist_add(c, &f->tail, s, false);
        return true;
    }
    case RA_BOL: case RA_EOL: case RA_WORDB: case RA_NWORDB: {
        int kind = r->kind == RA_BOL     ? RS_BOL
                   : r->kind == RA_EOL   ? RS_EOL
                   : r->kind == RA_WORDB ? RS_WORDB
                                         : RS_NWORDB;
        int s = nstate(c, kind);
        if (s < 0) return false;
        f->entry = s;
        memset(&f->tail, 0, sizeof(f->tail));
        plist_add(c, &f->tail, s, false);
        return true;
    }
    case RA_SEQ: {
        frag_eps(f);
        for (int i = 0; i < r->n; i++) {
            frag piece = {-1, {0}};
            if (!emit_node(c, r->seq[i], &piece)) return false;
            frag_thread(c, f, piece.entry);
            if (piece.entry >= 0 && f->entry < 0) f->entry = piece.entry;
            f->tail = piece.tail;
        }
        return true;
    }
    case RA_ALT: {
        /* N-way: split0 threaded from the tail, routing a to branch 0
         * and b to split1, which routes a to branch 1 and b to
         * split2... the last branch hangs off the last split's b. */
        if (r->n == 1) return emit_node(c, r->seq[0], f);
        frag_eps(f);
        plist merged;
        memset(&merged, 0, sizeof(merged));
        int split = nstate(c, RS_SPLIT);
        if (split < 0) return false;
        frag_thread(c, f, split);
        if (f->entry < 0) f->entry = split;
        int prev_split = split;
        for (int i = 0; i < r->n; i++) {
            frag b = {-1, {0}};
            if (!emit_node(c, r->seq[i], &b)) return false;
            /* emit_node may have grown (and re-allocated) the states
             * array: all writes re-fetch c->states by INDEX, never via
             * held pointers */
            int use = -1;
            bool use_b = false;
            if (i == 0) {
                use = prev_split;
                use_b = false;
            } else if (i == r->n - 1) {
                use = prev_split;
                use_b = true;
            } else {
                int s2 = nstate(c, RS_SPLIT);
                if (s2 < 0) return false;
                c->states[prev_split].b = s2;
                use = s2;
                use_b = false;
            }
            if (b.entry >= 0) {
                if (use_b) {
                    c->states[use].b = b.entry;
                } else {
                    c->states[use].a = b.entry;
                }
            } else {
                /* an epsilon branch: its entry slot IS an exit */
                plist_add(c, &merged, use, use_b);
            }
            for (size_t k = 0; k < b.tail.n; k++) {
                plist_add(c, &merged, b.tail.slots[k].state,
                          b.tail.slots[k].is_b);
            }
            prev_split = use;
        }
        f->tail = merged;
        return true;
    }
    case RA_QUANT: {
        if (r->qmax == 0) {
            frag_eps(f); /* {0} */
            return true;
        }
        /* mandatory copies */
        if (r->qmin >= 1) {
            frag first = {-1, {0}};
            if (!emit_node(c, r->child, &first)) return false;
            frag_thread(c, f, first.entry);
            if (f->entry < 0) f->entry = first.entry;
            f->tail = first.tail;
            for (long k = 1; k < r->qmin; k++) {
                frag piece = {-1, {0}};
                if (!emit_node(c, r->child, &piece)) return false;
                frag_thread(c, f, piece.entry);
                f->tail = piece.tail;
            }
        }
        long extra = r->qmax < 0 ? -1 : r->qmax - r->qmin;
        if (extra < 0) {
            /* the unbounded tail: split{body loops back, b exits} */
            int split = nstate(c, RS_SPLIT);
            if (split < 0) return false;
            frag_thread(c, f, split);
            frag body = {-1, {0}};
            if (!emit_node(c, r->child, &body)) return false;
            c->states[split].a = body.entry >= 0 ? body.entry : -1;
            if (body.entry >= 0) {
                frag_thread(c, &body, split); /* the loop back-edge */
            } else {
                /* an epsilon body (never for real atoms) loops forever:
                 * reject rather than emit a live-lock */
                if (c->err[0] == '\0') {
                    snprintf(c->err, c->err_cap,
                             "quantified subexpression matches empty");
                }
                return false;
            }
            int entry = f->entry; /* frag_eps wipes it; the mandatory
                                     copies' entry is preserved */
            frag_eps(f);
            f->entry = r->qmin >= 1 ? entry : split;
            plist_add(c, &f->tail, split, true);
            return true;
        }
        for (long k = 0; k < extra; k++) {
            int split = nstate(c, RS_SPLIT);
            if (split < 0) return false;
            frag_thread(c, f, split);
            frag body = {-1, {0}};
            if (!emit_node(c, r->child, &body)) return false;
            c->states[split].a = body.entry >= 0 ? body.entry : -1;
            plist out;
            memset(&out, 0, sizeof(out));
            plist_add(c, &out, split, true);
            for (size_t q = 0; q < body.tail.n; q++) {
                plist_add(c, &out, body.tail.slots[q].state,
                          body.tail.slots[q].is_b);
            }
            f->tail = out;
            if (f->entry < 0 && body.entry >= 0) {
                f->entry = split; /* the optional chain is the entry */
            }
        }
        return true;
    }
    default:
        snprintf(c->err, c->err_cap, "internal: unknown AST kind");
        return false;
    }
}

struct sch_regex *sch_regex_compile(teptris_arena *a, const char *pattern,
                                    size_t len, char *err, size_t err_cap) {
    pctx pc;
    memset(&pc, 0, sizeof(pc));
    pc.a = a;
    pc.err = err;
    pc.err_cap = err_cap;
    if (err != NULL && err_cap > 0) err[0] = '\0';
    utf8_to_cp(&pc, pattern, len);

    /* a leading (?i): ASCII case-fold the rest */
    if (pc.n >= 4 && pc.s[0] == '(' && pc.s[1] == '?' && pc.s[2] == 'i' &&
        pc.s[3] == ')') {
        pc.fold = true;
        pc.pos = 4;
        if (pc.n >= 5 && pc.s[4] == ':') pc.pos = 5;
    }

    ra *ast = ralternation(&pc);
    if (ast == NULL) return NULL;
    if (pc.pos != pc.n) {
        if (err != NULL && err_cap > 0 && err[0] == '\0') {
            snprintf(err, err_cap, "unexpected ')' in pattern");
        }
        return NULL;
    }

    nctx nc;
    memset(&nc, 0, sizeof(nc));
    nc.a = a;
    nc.err = err;
    nc.err_cap = err_cap;
    frag f = {-1, {0}};
    if (!emit_node(&nc, ast, &f)) return NULL;
    int match = nstate(&nc, RS_MATCH);
    if (match < 0) return NULL;
    frag_thread(&nc, &f, match);

    struct sch_regex *re = teptris_arena_alloc(a, sizeof(*re));
    re->states = nc.states;
    re->n_states = nc.n;
    re->match = match;
    re->start = f.entry >= 0 ? f.entry : match; /* an empty pattern */
    return re;
}


static bool cls_match(const rstate *st, uint32_t cp) {
    if (st->any) return true;
    int lo = 0, hi = st->n_ranges - 1;
    bool in = false;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (cp < st->ranges[mid].lo) {
            hi = mid - 1;
        } else if (cp > st->ranges[mid].hi) {
            lo = mid + 1;
        } else {
            in = true;
            break;
        }
    }
    return st->negated ? !in : in;
}

typedef struct {
    const uint32_t *s;
    int len;
} sim_ctx;

/* epsilon closure of the open set at input position pos; set membership
 * uses generation stamps (open == gen, closed == -gen) */
static void closure(sim_ctx *sc, const struct sch_regex *re, int *mark,
                    int gen, int *stack, int pos) {
    int sp = 0;
    for (int i = 0; i < re->n_states; i++) {
        if (mark[i] == gen) stack[sp++] = i;
    }
    while (sp > 0) {
        int s = stack[--sp];
        const rstate *st = &re->states[s];
        int targets[2];
        int nt = 0;
        if (st->kind == RS_SPLIT) {
            targets[nt++] = st->a;
            targets[nt++] = st->b;
        } else if (st->kind == RS_BOL) {
            if (pos == 0) targets[nt++] = st->a;
        } else if (st->kind == RS_EOL) {
            if (pos >= sc->len) targets[nt++] = st->a;
        } else if (st->kind == RS_WORDB) {
            bool before = pos > 0 && is_word_cp(sc->s[pos - 1]);
            bool after = pos < sc->len && is_word_cp(sc->s[pos]);
            if (before != after) targets[nt++] = st->a;
        } else if (st->kind == RS_NWORDB) {
            bool before = pos > 0 && is_word_cp(sc->s[pos - 1]);
            bool after = pos < sc->len && is_word_cp(sc->s[pos]);
            if (before == after) targets[nt++] = st->a;
        }
        for (int k = 0; k < nt; k++) {
            int t = targets[k];
            if (t >= 0 && mark[t] != gen && mark[t] != -gen) {
                mark[t] = gen;
                stack[sp++] = t;
            }
        }
        mark[s] = -gen; /* closed */
    }
}

bool sch_regex_search(const struct sch_regex *re, const uint32_t *s, int len) {
    if (re->n_states == 0) return false;
    bool matched = false;
    int *mark = calloc((size_t)re->n_states, sizeof(int));
    int *stack = calloc((size_t)re->n_states + 1, sizeof(int));
    if (mark == NULL || stack == NULL) {
        free(mark);
        free(stack);
        return false;
    }
    sim_ctx sc = {s, len};
    int gen = 0;
    for (int start = 0; start <= len && !matched; start++) {
        gen++;
        if (re->start >= 0 && mark[re->start] != gen && mark[re->start] != -gen) {
            mark[re->start] = gen;
        } else if (re->start < 0) {
            /* the empty pattern: the match state is the entry */
            mark[re->match] = gen;
        }
        closure(&sc, re, mark, gen, stack, start);
        for (int pos = start; pos <= len; pos++) {
            if (mark[re->match] == gen || mark[re->match] == -gen) {
                matched = true;
                break;
            }
            if (pos >= len) break;
            gen++;
            for (int i = 0; i < re->n_states; i++) {
                if (mark[i] == gen - 1 || mark[i] == -(gen - 1)) {
                    if (re->states[i].kind == RS_CHAR) {
                        if (cls_match(&re->states[i], s[pos])) {
                            int t = re->states[i].a;
                            if (t >= 0 && mark[t] != gen && mark[t] != -gen) {
                                mark[t] = gen;
                            }
                        }
                    }
                }
            }
            closure(&sc, re, mark, gen, stack, pos + 1);
        }
    }
    free(mark);
    free(stack);
    return matched;
}

bool sch_regex_is_match(const struct sch_regex *re, const char *utf8,
                        size_t len) {
    uint32_t stackbuf[64];
    uint32_t *cps = stackbuf;
    if (len > sizeof stackbuf / sizeof stackbuf[0]) {
        cps = malloc(len * sizeof(uint32_t));
        if (cps == NULL) return false;
    }
    size_t n = 0;
    size_t i = 0;
    while (i < len) {
        unsigned char b = (unsigned char)utf8[i];
        uint32_t cp;
        size_t step;
        if (b < 0x80) {
            cp = b;
            step = 1;
        } else if ((b & 0xE0) == 0xC0 && i + 1 < len &&
                   ((unsigned char)utf8[i + 1] & 0xC0) == 0x80) {
            cp = ((uint32_t)(b & 0x1F)) << 6 | ((unsigned char)utf8[i + 1] & 0x3F);
            step = 2;
        } else if ((b & 0xF0) == 0xE0 && i + 2 < len &&
                   ((unsigned char)utf8[i + 1] & 0xC0) == 0x80 &&
                   ((unsigned char)utf8[i + 2] & 0xC0) == 0x80) {
            cp = ((uint32_t)(b & 0x0F)) << 12 |
                 ((uint32_t)((unsigned char)utf8[i + 1] & 0x3F)) << 6 |
                 ((unsigned char)utf8[i + 2] & 0x3F);
            step = 3;
        } else if ((b & 0xF8) == 0xF0 && i + 3 < len &&
                   ((unsigned char)utf8[i + 1] & 0xC0) == 0x80 &&
                   ((unsigned char)utf8[i + 2] & 0xC0) == 0x80 &&
                   ((unsigned char)utf8[i + 3] & 0xC0) == 0x80) {
            cp = ((uint32_t)(b & 0x07)) << 18 |
                 ((uint32_t)((unsigned char)utf8[i + 1] & 0x3F)) << 12 |
                 ((uint32_t)((unsigned char)utf8[i + 2] & 0x3F)) << 6 |
                 ((unsigned char)utf8[i + 3] & 0x3F);
            step = 4;
        } else {
            cp = 0xFFFD;
            step = 1;
        }
        cps[n++] = cp;
        i += step;
    }
    bool m = sch_regex_search(re, cps, (int)n);
    if (cps != stackbuf) free(cps);
    return m;
}
