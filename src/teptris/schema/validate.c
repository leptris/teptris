/* The validation walk: a compiled schema against a converted
 * document. One tagged-value conversion per validate call (validation
 * is a separate API; the parse hot path is untouched), then a single
 * recursive walk collecting every error, up to 256, as a JSON array. */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "schema_internal.h"
#include "teptris/teptris.h"

#define MAX_ERRORS 256
#define MAX_DEPTH 512

typedef struct {
    teptris_arena *scratch;
    char *buf;
    size_t n, cap;
    int n_errors;
    int mute;  /* trial validations: walk, but record nothing */
    int pings; /* every verr fire, muted or not - the trial signal */
    int ref_depth; /* $ref hops in flight (cycle guard) */
    /* TOML path stack */
    char *segs[128];
    int depth;
    /* schema-path stack */
    const char *ssegs[64];
    char sseg_buf[64][64];
    int sdepth;
    int truncated;
} vctx;

/* ---------------------------------------------------------- output -- */

static void out_put(vctx *vc, const char *s, size_t len) {
    if (vc->n + len + 1 > vc->cap) {
        vc->cap = vc->cap == 0 ? 256 : vc->cap * 2;
        while (vc->n + len + 1 > vc->cap) vc->cap *= 2;
        char *nb = realloc(vc->buf, vc->cap);
        if (nb == NULL) return;
        vc->buf = nb;
    }
    memcpy(vc->buf + vc->n, s, len);
    vc->n += len;
    vc->buf[vc->n] = '\0';
}

static void out_json_escaped(vctx *vc, const char *s) {
    for (const char *p = s; *p != '\0'; p++) {
        if (*p == '"' || *p == '\\') {
            out_put(vc, "\\", 1);
            out_put(vc, p, 1);
        } else if ((unsigned char)*p < 0x20) {
            char esc[8];
            snprintf(esc, sizeof esc, "\\u%04x", (unsigned char)*p);
            out_put(vc, esc, 6);
        } else {
            out_put(vc, p, 1);
        }
    }
}

/* ------------------------------------------------------------- path -- */

static bool plain_key(const char *k) {
    if (k[0] == '\0') return false;
    for (const char *p = k; *p != '\0'; p++) {
        char ch = *p;
        if (!(ch >= 'a' && ch <= 'z') && !(ch >= 'A' && ch <= 'Z') &&
            !(ch >= '0' && ch <= '9') && ch != '_' && ch != '-') {
            return false;
        }
    }
    return true;
}

static void path_push(vctx *vc, const char *seg) {
    if (vc->depth >= 128) return;
    size_t len = strlen(seg);
    char *copy = teptris_arena_alloc(vc->scratch, len + 3);
    if (plain_key(seg)) {
        memcpy(copy, seg, len + 1);
    } else {
        copy[0] = '"';
        memcpy(copy + 1, seg, len);
        copy[len + 1] = '"';
        copy[len + 2] = '\0';
    }
    vc->segs[vc->depth++] = copy;
}

static void path_push_idx(vctx *vc, long idx) {
    if (vc->depth >= 128) return;
    char *copy = teptris_arena_alloc(vc->scratch, 24);
    snprintf(copy, 24, "%ld", idx);
    vc->segs[vc->depth++] = copy;
}

static void path_pop(vctx *vc) {
    if (vc->depth > 0) vc->depth--;
}

/* the materialized TOML path as one JSON string: "a.b.\"c.d\".0"
 * (segments are pre-formatted: bare when plain, quoted otherwise) */
static void out_path(vctx *vc) {
    out_put(vc, "\"", 1);
    for (int i = 0; i < vc->depth; i++) {
        if (i > 0) out_put(vc, ".", 1);
        out_json_escaped(vc, vc->segs[i]);
    }
    out_put(vc, "\"", 1);
}

static void spush(vctx *vc, const char *seg) {
    if (vc->sdepth >= 64) return;
    snprintf(vc->sseg_buf[vc->sdepth], sizeof vc->sseg_buf[vc->sdepth], "%s",
             seg);
    vc->ssegs[vc->sdepth] = vc->sseg_buf[vc->sdepth];
    vc->sdepth++;
}

static void spush_named(vctx *vc, const char *kind, const char *name) {
    if (vc->sdepth >= 64) return;
    snprintf(vc->sseg_buf[vc->sdepth], sizeof vc->sseg_buf[vc->sdepth],
             "%s.%s", kind, name);
    vc->ssegs[vc->sdepth] = vc->sseg_buf[vc->sdepth];
    vc->sdepth++;
}

static void spop(vctx *vc) {
    if (vc->sdepth > 0) vc->sdepth--;
}

static void out_schema_path(vctx *vc) {
    out_put(vc, "\"#", 2);
    for (int i = 0; i < vc->sdepth; i++) {
        out_put(vc, "/", 1);
        out_json_escaped(vc, vc->ssegs[i]);
    }
    out_put(vc, "\"", 1);
}

static void verr(vctx *vc, const char *fmt, ...) {
    vc->pings++;
    if (vc->mute > 0) return;
    if (vc->n_errors >= MAX_ERRORS) {
        vc->truncated = 1;
        return;
    }
    if (vc->n_errors > 0) out_put(vc, ",", 1);
    out_put(vc, "{\"path\":", 8);
    out_path(vc);
    out_put(vc, ",\"schemaPath\":", 14);
    out_schema_path(vc);
    out_put(vc, ",\"message\":\"", 12);
    char msg[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    for (const char *p = msg; *p != '\0'; p++) {
        if (*p == '"' || *p == '\\') out_put(vc, "\\", 1);
        out_put(vc, p, 1);
    }
    out_put(vc, "\"}", 2);
    vc->n_errors++;
}

/* --------------------------------------------------- value equality -- */

static bool sv_equal(const sch_svalue *a, const sch_svalue *b) {
    if (a == NULL || b == NULL) return a == b;
    if (a->kind != b->kind) {
        /* policy 3: a TOML datetime compares with a schema-side string
         * by canonical text (JSON cannot carry a datetime literal, so
         * enum/const entries for datetimes are strings); a date never
         * equals a datetime text */
        int ad = a->kind >= SV_DATETIME;
        int bd = b->kind >= SV_DATETIME;
        if ((ad || bd) && (a->kind == SV_STRING || b->kind == SV_STRING)) {
            const sch_svalue *dt = ad ? a : b;
            const sch_svalue *st = ad ? b : a;
            if (dt->str_len == st->str_len &&
                memcmp(dt->str, st->str, dt->str_len) == 0) {
                return true;
            }
            /* taplo re-formats offset datetimes with the time crate,
             * which spells UTC as "+00:00" where the canonical TOML
             * form is "Z"; both spellings name the same instant, so
             * const/enum entries match either way */
            if (dt->kind == SV_DATETIME && dt->str_len > 0 &&
                dt->str[dt->str_len - 1] == 'Z' && st->str_len > 6 &&
                memcmp(st->str + st->str_len - 6, "+00:00", 6) == 0 &&
                dt->str_len - 1 == st->str_len - 6 &&
                memcmp(dt->str, st->str, st->str_len - 6) == 0) {
                return true;
            }
            return false;
        }
        return false; /* type-strict (policy 2) */
    }
    switch (a->kind) {
    case SV_STRING:
        return a->str_len == b->str_len && memcmp(a->str, b->str, a->str_len) == 0;
    case SV_INT: return a->i == b->i;
    case SV_FLOAT: return a->f == b->f;
    case SV_BOOL: return a->b == b->b;
    case SV_NULL: return true;
    case SV_DATETIME: case SV_DATE: case SV_TIME:
        return a->str_len == b->str_len && memcmp(a->str, b->str, a->str_len) == 0;
    case SV_ARRAY:
        if (a->n_items != b->n_items) return false;
        for (size_t i = 0; i < a->n_items; i++) {
            if (!sv_equal(a->items[i], b->items[i])) return false;
        }
        return true;
    case SV_TABLE:
        if (a->n_members != b->n_members) return false;
        for (size_t i = 0; i < a->n_members; i++) {
            sch_svalue *other = NULL;
            for (size_t k = 0; k < b->n_members; k++) {
                if (strcmp(b->members[k].key, a->members[i].key) == 0) {
                    other = b->members[k].val;
                    break;
                }
            }
            if (other == NULL) return false;
            if (!sv_equal(a->members[i].val, other)) return false;
        }
        return true;
    }
    return false;
}

/* --------------------------------------------------------- the walk -- */

static sch_svalue *jv_to_sv(vctx *vc, jv *v);
static void val(vctx *vc, sch_node *n, const sch_svalue *v);

static bool passes(vctx *vc, sch_node *n, const sch_svalue *v) {
    int save_pings = vc->pings;
    size_t save_n = vc->n;
    int save_depth = vc->sdepth;
    vc->mute++;
    val(vc, n, v);
    vc->mute--;
    if (vc->pings == save_pings) {
        return true;
    }
    /* discard the trial's errors: rewind the buffer to save_n */
    vc->n = save_n;
    if (vc->buf != NULL) vc->buf[vc->n] = '\0';
    vc->sdepth = save_depth;
    return false;
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

/* RFC 3339 shapes for the TOML datetime kinds live in formats.c; the
 * walk only decides whether a kind participates in a format (policy
 * 3) and hands plain strings to the format battery */
static bool format_ok(vctx *vc, int format, const sch_svalue *v) {
    if (format == FMT_NONE) return true;
    if (v->kind == SV_DATE) return format == FMT_DATE;
    if (v->kind == SV_TIME) return format == FMT_TIME;
    if (v->kind == SV_DATETIME) return format == FMT_DATETIME;
    if (v->kind != SV_STRING) return format == FMT_NONE;
    return sch_format_string_check(format, v->str, v->str_len, vc->scratch);
}

/* --------------------------------------------------------- content -- */

static bool str_is_base64(const char *s, size_t len) {
    if (len == 0) return true; /* "" decodes to empty (the crate's take) */
    if (len % 4 != 0) return false;
    size_t pad = 0;
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        bool alnum = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                     (c >= '0' && c <= '9');
        if (!(alnum || c == '+' || c == '/' || c == '=')) return false;
        if (c == '=') {
            pad++;
        } else if (pad > 0) {
            return false; /* padding only at the end */
        }
    }
    return pad <= 2;
}

static int b64_val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

/* contentEncoding/contentMediaType, the taplo parity pair: base64 is
 * the one encoding checked (and decoded for the media step),
 * application/json the one media type parsed; unknown names on either
 * side are no-ops, exactly like the crate's map misses */
static void check_content(vctx *vc, const sch_node *n, const sch_svalue *v) {
    if (v->kind != SV_STRING) return;
    const char *text = v->str;
    size_t tlen = v->str_len;
    bool b64 = n->content_encoding != NULL &&
               strcmp(n->content_encoding, "base64") == 0;
    if (b64 && !str_is_base64(text, tlen)) {
        verr(vc, "string is not valid base64 (contentEncoding)");
        return;
    }
    if (n->content_media_type == NULL ||
        strcmp(n->content_media_type, "application/json") != 0) {
        return; /* unknown media names: annotation */
    }
    const char *payload = text;
    size_t plen = tlen;
    if (b64) {
        size_t dn = tlen / 4 * 3;
        if (tlen > 0 && text[tlen - 1] == '=') dn--;
        if (tlen > 1 && text[tlen - 2] == '=') dn--;
        char *out = teptris_arena_alloc(vc->scratch, dn + 1);
        size_t o = 0;
        int acc = 0, bits = 0;
        for (size_t i = 0; i < tlen && o < dn; i++) {
            if (text[i] == '=') break;
            int d = b64_val(text[i]);
            if (d < 0) break;
            acc = (acc << 6) | d;
            bits += 6;
            if (bits >= 8) {
                bits -= 8;
                out[o++] = (char)((acc >> bits) & 0xFF);
            }
        }
        /* the media check runs on the decoded text only when it is
         * UTF-8 (the crate skips the check otherwise) */
        bool utf8 = true;
        for (size_t i = 0; i < o && utf8; i++) {
            unsigned char b = (unsigned char)out[i];
            if (b < 0x80) continue;
            int need = b < 0xE0 ? 1 : b < 0xF0 ? 2 : 3;
            if (i + need >= o || (b & 0xC0) != 0xC0) utf8 = false;
            for (int k = 1; k <= need && utf8; k++) {
                if (((unsigned char)out[i + k] & 0xC0) != 0x80) utf8 = false;
            }
            i += need;
        }
        if (!utf8) return;
        out[o] = '\0';
        payload = out;
        plen = o;
    }
    char jerr[64] = {0};
    if (json_read(vc->scratch, payload, plen, jerr, sizeof jerr) == NULL) {
        verr(vc, "string is not valid JSON (contentMediaType)");
    }
}

/* ------------------------------------------------------ multipleOf -- */

/* value / multipleOf decided exactly on the decimal representations:
 * value = vm * 10^ve, mult = mm * 10^me, and the ratio is an integer
 * iff the mantissas divide once the 2s and 5s are moved across —
 * plain int64 arithmetic, so it runs identically where __int128 does
 * not exist (MSVC) and needs no floating division at all */
static bool sch_mult_ok(const sch_node *n, const sch_svalue *v) {
    if (v->kind == SV_INT && n->mult_is_int) {
        return n->imult != 0 && v->i % n->imult == 0;
    }
    double fv = v->kind == SV_INT ? (double)v->i : v->f;
    if (!(fv == fv) || fv == INFINITY || fv == -INFINITY) {
        return false; /* nan/inf are never multiples */
    }
    int64_t vm, mm;
    int ve, me;
    if (v->kind == SV_INT) {
        vm = v->i;
        ve = 0;
    } else if (!sch_decimal_of_double(v->f, &vm, &ve)) {
        return false;
    }
    if (n->mult_is_int) {
        mm = n->imult;
        me = 0;
    } else if (n->mult_exact) {
        mm = n->mult_mant;
        me = n->mult_exp;
    } else {
        return false;
    }
    if (mm == 0) return false;

    /* strip the 2s and 5s out of the multiplier; the remainder is
     * coprime to 10, so it must divide the mantissa itself */
    int b = 0, c = 0;
    while (mm % 2 == 0) {
        mm /= 2;
        b++;
    }
    while (mm % 5 == 0) {
        mm /= 5;
        c++;
    }

    /* the numerator vm * 10^k (k = ve - me) carries 2^(k+v2) * 5^(k+v5) */
    int k = ve - me;
    uint64_t mag = vm < 0 ? (uint64_t)(-(vm + 1)) + 1 : (uint64_t)vm;
    int u2 = 0, u5 = 0;
    uint64_t m = mag;
    while (m % 2 == 0 && u2 < 64) {
        m /= 2;
        u2++;
    }
    m = mag;
    while (m % 5 == 0 && u5 < 64) {
        m /= 5;
        u5++;
    }
    if (k + u2 < b || k + u5 < c) return false;
    return mag % (uint64_t)mm == 0;
}

static int sv_to_st(const sch_svalue *v) {
    switch (v->kind) {
    case SV_STRING: return ST_STRING;
    case SV_INT: return ST_INTEGER | ST_NUMBER;
    case SV_FLOAT: return ST_NUMBER;
    case SV_BOOL: return ST_BOOLEAN;
    case SV_TABLE: return ST_OBJECT;
    case SV_ARRAY: return ST_ARRAY;
    case SV_DATETIME: case SV_DATE: case SV_TIME: return ST_STRING;
    default: return 0;
    }
}

static void val(vctx *vc, sch_node *n, const sch_svalue *v) {
    if (n == NULL || v == NULL) return;
    if (vc->depth >= MAX_DEPTH) {
        verr(vc, "schema depth exceeded");
        return;
    }

    if (n->ref != NULL) {
        /* a $ref cycle that consumes no document structure (a schema
         * that is its own ref target) would loop forever; legal
         * cycles descend the document on every lap and stay shallow */
        if (vc->ref_depth >= MAX_DEPTH) {
            verr(vc, "cyclic $ref recursion");
        } else {
            vc->ref_depth++;
            spush(vc, "$ref");
            val(vc, n->ref, v);
            spop(vc);
            vc->ref_depth--;
        }
    }

    if (n->is_false) {
        verr(vc, "no value is allowed here (the false schema)");
        return;
    }

    /* type */
    if (n->types != ST_ANY) {
        int bits = sv_to_st(v);
        if ((n->types & (unsigned)bits) == 0) {
            const char *found = v->kind == SV_TABLE     ? "table"
                                : v->kind == SV_ARRAY   ? "array"
                                : v->kind == SV_STRING  ? "string"
                                : v->kind == SV_INT     ? "integer"
                                : v->kind == SV_FLOAT   ? "float"
                                : v->kind == SV_BOOL    ? "boolean"
                                                        : "value";
            verr(vc, "expected a matching type, found %s", found);
        }
    }

    /* enum / const */
    if (n->has_const && n->const_val != NULL) {
        sch_svalue *cv = NULL;
        /* convert lazily into the scratch arena */
                cv = jv_to_sv(vc, n->const_val);
        if (!sv_equal(cv, v)) {
            verr(vc, "value does not equal the constant");
        }
    }
    if (n->n_enum > 0) {
        bool hit = false;
        for (size_t i = 0; i < n->n_enum && !hit; i++) {
            sch_svalue *ev = jv_to_sv(vc, n->enum_vals[i]);
            if (sv_equal(ev, v)) hit = true;
        }
        if (!hit) verr(vc, "value is not one of the %zu enum entries",
                       n->n_enum);
    }

    /* numeric */
    bool is_num = v->kind == SV_INT || v->kind == SV_FLOAT;
    if (is_num) {
        double fv = v->kind == SV_INT ? (double)v->i : v->f;
        if (n->has_min) {
            bool ok = v->kind == SV_INT && n->fmin == (double)(int64_t)n->fmin
                          ? v->i >= n->imin
                          : fv >= n->fmin;
            if (!ok) verr(vc, "value is below the minimum");
        }
        if (n->has_max) {
            bool ok = v->kind == SV_INT && n->fmax == (double)(int64_t)n->fmax
                          ? v->i <= n->imax
                          : fv <= n->fmax;
            if (!ok) verr(vc, "value is above the maximum");
        }
        if (n->has_ex_min) {
            bool ok = v->kind == SV_INT && n->fex_min == (double)(int64_t)n->fex_min
                          ? v->i > n->iex_min
                          : fv > n->fex_min;
            if (!ok) verr(vc, "value is not above the exclusive minimum");
        }
        if (n->has_ex_max) {
            bool ok = v->kind == SV_INT && n->fex_max == (double)(int64_t)n->fex_max
                          ? v->i < n->iex_max
                          : fv < n->fex_max;
            if (!ok) verr(vc, "value is not below the exclusive maximum");
        }
        if (n->has_mult) {
            bool ok = sch_mult_ok(n, v);
            if (!ok) verr(vc, "value is not a multiple of multipleOf");
        }
    }

    /* string */
    if (v->kind == SV_STRING || v->kind == SV_DATE || v->kind == SV_TIME ||
        v->kind == SV_DATETIME) {
        if (n->has_min_length &&
            (long)cp_count(v->str, v->str_len) < n->min_length) {
            verr(vc, "string is shorter than minLength");
        }
        if (n->has_max_length &&
            (long)cp_count(v->str, v->str_len) > n->max_length) {
            verr(vc, "string is longer than maxLength");
        }
        if (n->pattern != NULL &&
            !sch_regex_is_match(n->pattern, v->str, v->str_len)) {
            verr(vc, "string does not match the pattern");
        }
        if (n->format != FMT_NONE && !format_ok(vc, n->format, v)) {
            verr(vc, "string does not match the format");
        }
        check_content(vc, n, v);
    }

    /* object */
    if (v->kind == SV_TABLE) {
        if (n->has_min_props && (long)v->n_members < n->min_props) {
            verr(vc, "fewer properties than minProperties");
        }
        if (n->has_max_props && (long)v->n_members > n->max_props) {
            verr(vc, "more properties than maxProperties");
        }
        for (size_t r = 0; r < n->n_req; r++) {
            bool found = false;
            for (size_t m = 0; m < v->n_members && !found; m++) {
                if (strcmp(v->members[m].key, n->required[r]) == 0) found = true;
            }
            if (!found) {
                verr(vc, "missing required property '%s'", n->required[r]);
            }
        }
        for (size_t m = 0; m < v->n_members; m++) {
            const char *key = v->members[m].key;
            sch_svalue *mv = v->members[m].val;
            bool matched_known = false;
            if (n->n_props > 0) {
                for (size_t p = 0; p < n->n_props; p++) {
                    if (strcmp(n->props[p].name, key) == 0) {
                        matched_known = true;
                        spush_named(vc, "properties", key);
                        path_push(vc, key);
                        val(vc, n->props[p].schema, mv);
                        path_pop(vc);
                        spop(vc);
                        break;
                    }
                }
            }
            for (size_t p = 0; p < n->n_pprops; p++) {
                if (sch_regex_is_match(n->pprops[p].re, key, strlen(key))) {
                    matched_known = true;
                    spush(vc, "patternProperties");
                    path_push(vc, key);
                    val(vc, n->pprops[p].schema, mv);
                    path_pop(vc);
                    spop(vc);
                }
            }
            if (!matched_known && n->additional != NULL) {
                spush(vc, "additionalProperties");
                path_push(vc, key);
                if (n->additional->is_false) {
                    verr(vc, "unknown property '%s' (additionalProperties is false)",
                         key);
                } else {
                    val(vc, n->additional, mv);
                }
                path_pop(vc);
                spop(vc);
            }
        }
        if (n->prop_names != NULL) {
            for (size_t m = 0; m < v->n_members; m++) {
                sch_svalue key_sv;
                memset(&key_sv, 0, sizeof key_sv);
                key_sv.kind = SV_STRING;
                key_sv.str = v->members[m].key;
                key_sv.str_len = strlen(v->members[m].key);
                spush(vc, "propertyNames");
                if (!passes(vc, n->prop_names, &key_sv)) {
                    verr(vc, "property name '%s' fails propertyNames",
                         v->members[m].key);
                }
                spop(vc);
            }
        }
        for (size_t d = 0; d < n->n_dep_req; d++) {
            bool present = false;
            for (size_t m = 0; m < v->n_members && !present; m++) {
                if (strcmp(v->members[m].key, n->dep_req[d].key) == 0) present = true;
            }
            if (!present) continue;
            sch_node *fake = n->dep_req[d].schema;
            for (size_t r = 0; r < fake->n_req; r++) {
                bool found = false;
                for (size_t m = 0; m < v->n_members && !found; m++) {
                    if (strcmp(v->members[m].key, fake->required[r]) == 0) found = true;
                }
                if (!found) {
                    verr(vc, "'%s' requires '%s' (dependentRequired)",
                         n->dep_req[d].key, fake->required[r]);
                }
            }
        }
        for (size_t d = 0; d < n->n_dep_schemas; d++) {
            bool present = false;
            for (size_t m = 0; m < v->n_members && !present; m++) {
                if (strcmp(v->members[m].key, n->dep_schemas[d].key) == 0) {
                    present = true;
                }
            }
            if (!present) continue;
            if (!passes(vc, n->dep_schemas[d].schema, v)) {
                verr(vc, "the '%s' dependency schema does not hold",
                     n->dep_schemas[d].key);
            }
        }
    }

    /* array */
    if (v->kind == SV_ARRAY) {
        if (n->has_min_items && (long)v->n_items < n->min_items) {
            verr(vc, "fewer items than minItems");
        }
        if (n->has_max_items && (long)v->n_items > n->max_items) {
            verr(vc, "more items than maxItems");
        }
        if (n->unique_items && n->has_unique) {
            for (size_t i = 0; i < v->n_items; i++) {
                for (size_t k = i + 1; k < v->n_items; k++) {
                    if (sv_equal(v->items[i], v->items[k])) {
                        verr(vc, "array items are not unique (items %zu and %zu)",
                             i, k);
                        i = v->n_items; /* one report is enough */
                        break;
                    }
                }
            }
        }
        if (n->n_prefix > 0 || (n->items_set && n->items != NULL)) {
            for (size_t i = 0; i < v->n_items; i++) {
                if (i < n->n_prefix) {
                    spush(vc, "prefixItems");
                    path_push_idx(vc, (long)i);
                    val(vc, n->prefix[i], v->items[i]);
                    path_pop(vc);
                    spop(vc);
                } else if (n->items != NULL) {
                    spush(vc, "items");
                    path_push_idx(vc, (long)i);
                    val(vc, n->items, v->items[i]);
                    path_pop(vc);
                    spop(vc);
                } else if (n->add_items != NULL) {
                    spush(vc, "additionalItems");
                    path_push_idx(vc, (long)i);
                    if (n->add_items->is_false) {
                        verr(vc, "array has items beyond the tuple "
                                 "(additionalItems is false)");
                    } else {
                        val(vc, n->add_items, v->items[i]);
                    }
                    path_pop(vc);
                    spop(vc);
                }
            }
        } else if (n->add_items != NULL) {
            /* additionalItems with no tuple: every position */
            for (size_t i = 0; i < v->n_items; i++) {
                spush(vc, "additionalItems");
                path_push_idx(vc, (long)i);
                if (n->add_items->is_false) {
                    verr(vc, "array has items beyond the tuple "
                             "(additionalItems is false)");
                } else {
                    val(vc, n->add_items, v->items[i]);
                }
                path_pop(vc);
                spop(vc);
            }
        }
        if (n->has_contains && n->contains != NULL) {
            long hits = 0;
            for (size_t i = 0; i < v->n_items; i++) {
                if (passes(vc, n->contains, v->items[i])) hits++;
            }
            long min = n->has_min_contains ? n->min_contains : 1;
            long max = n->has_max_contains ? n->max_contains : (long)v->n_items;
            if (hits < min) verr(vc, "fewer matching items than minContains");
            if (hits > max) verr(vc, "more matching items than maxContains");
        }
    }

    /* applicators */
    if (n->n_all > 0) {
        for (size_t i = 0; i < n->n_all; i++) {
            spush(vc, "allOf");
            val(vc, n->all_of[i], v);
            spop(vc);
        }
    }
    if (n->n_any > 0) {
        bool ok = false;
        for (size_t i = 0; i < n->n_any && !ok; i++) {
            if (passes(vc, n->any_of[i], v)) ok = true;
        }
        if (!ok) verr(vc, "does not match any of the %zu anyOf schemas",
                      n->n_any);
    }
    if (n->n_one > 0) {
        int hits = 0;
        for (size_t i = 0; i < n->n_one; i++) {
            if (passes(vc, n->one_of[i], v)) hits++;
        }
        if (hits != 1) {
            verr(vc, "matches %d of the %zu oneOf schemas (exactly one required)",
                 hits, n->n_one);
        }
    }
    if (n->_not != NULL) {
        if (passes(vc, n->_not, v)) {
            verr(vc, "must not match the not-schema");
        }
    }
    if (n->if_s != NULL) {
        bool cond = passes(vc, n->if_s, v);
        if (cond && n->then_s != NULL) {
            spush(vc, "then");
            val(vc, n->then_s, v);
            spop(vc);
        } else if (!cond && n->else_s != NULL) {
            spush(vc, "else");
            val(vc, n->else_s, v);
            spop(vc);
        }
    }
}

/* -------------------------------------------------- DOM conversion -- */

static sch_svalue *node_to_sv(teptris_arena *scratch, const teptris_node *node);

static sch_svalue *sv_new(teptris_arena *a, int kind) {
    sch_svalue *v = teptris_arena_alloc(a, sizeof(sch_svalue));
    *v = (sch_svalue){ .kind = kind };
    return v;
}

static sch_svalue *node_to_sv(teptris_arena *scratch, const teptris_node *node) {
    int kind = teptris_node_kind(node);
    switch (kind) {
    case TEPTRIS_STRING: {
        sch_svalue *v = sv_new(scratch, SV_STRING);
        teptris_view view;
        if (teptris_node_string(node, &view) == TEPTRIS_OK) {
            v->str = view.ptr;
            v->str_len = view.len;
        }
        return v;
    }
    case TEPTRIS_INTEGER: {
        sch_svalue *v = sv_new(scratch, SV_INT);
        teptris_node_integer(node, &v->i);
        return v;
    }
    case TEPTRIS_FLOAT: {
        sch_svalue *v = sv_new(scratch, SV_FLOAT);
        teptris_node_float(node, &v->f);
        return v;
    }
    case TEPTRIS_BOOLEAN: {
        sch_svalue *v = sv_new(scratch, SV_BOOL);
        teptris_node_boolean(node, &v->b);
        return v;
    }
    case TEPTRIS_DATETIME_OFFSET:
    case TEPTRIS_DATETIME_LOCAL: {
        sch_svalue *v = sv_new(scratch, SV_DATETIME);
        teptris_datetime dt;
        teptris_node_datetime(node, &dt);
        /* the RFC 3339 text: equality + format checks run on the
         * canonical text the emitter would produce */
        char *buf = teptris_arena_alloc(scratch, 64);
        int n = snprintf(buf, 64, "%04d-%02d-%02dT%02d:%02d:%02d", dt.year,
                         dt.month, dt.day, dt.hour, dt.minute, dt.second);
        if (dt.nanosecond > 0) {
            n += snprintf(buf + n, 20, ".%09u", dt.nanosecond);
        }
        if (kind == TEPTRIS_DATETIME_OFFSET) {
            if (dt.offset_seconds == 0) {
                n += snprintf(buf + n, 8, "Z");
            } else {
                int sign = dt.offset_seconds < 0 ? -1 : 1;
                int off = dt.offset_seconds * sign;
                snprintf(buf + n, 12, "%c%02d:%02d", sign < 0 ? '-' : '+',
                         off / 3600, (off % 3600) / 60);
            }
        }
        v->str = buf;
        v->str_len = (size_t)n;
        return v;
    }
    case TEPTRIS_DATE_LOCAL: {
        sch_svalue *v = sv_new(scratch, SV_DATE);
        teptris_datetime dt;
        teptris_node_datetime(node, &dt);
        char *buf = teptris_arena_alloc(scratch, 16);
        snprintf(buf, 16, "%04d-%02d-%02d", dt.year, dt.month, dt.day);
        v->str = buf;
        v->str_len = 10;
        return v;
    }
    case TEPTRIS_TIME_LOCAL: {
        sch_svalue *v = sv_new(scratch, SV_TIME);
        teptris_datetime dt;
        teptris_node_datetime(node, &dt);
        char *buf = teptris_arena_alloc(scratch, 24);
        int n = snprintf(buf, 16, "%02d:%02d:%02d", dt.hour, dt.minute, dt.second);
        if (dt.nanosecond > 0) {
            n += snprintf(buf + n, 20, ".%09u", dt.nanosecond);
        }
        v->str = buf;
        v->str_len = (size_t)n;
        return v;
    }
    case TEPTRIS_ARRAY: {
        sch_svalue *v = sv_new(scratch, SV_ARRAY);
        size_t len = teptris_node_array_length(node);
        v->items = teptris_arena_alloc(scratch, (len ? len : 1) * sizeof(sch_svalue *));
        for (size_t i = 0; i < len; i++) {
            v->items[i] = node_to_sv(scratch, teptris_node_array_at(node, i));
        }
        v->n_items = len;
        return v;
    }
    case TEPTRIS_TABLE: {
        sch_svalue *v = sv_new(scratch, SV_TABLE);
        size_t len = teptris_node_table_length(node);
        v->members = teptris_arena_alloc(scratch, (len ? len : 1) * sizeof(sv_member));
        for (size_t i = 0; i < len; i++) {
            teptris_view key;
            const teptris_node *child =
                teptris_node_table_at(node, i, &key);
            char *kbuf = teptris_arena_alloc(scratch, key.len + 1);
            memcpy(kbuf, key.ptr, key.len);
            kbuf[key.len] = '\0';
            v->members[i].key = kbuf;
            v->members[i].val = node_to_sv(scratch, child);
        }
        v->n_members = len;
        return v;
    }
    default: {
        sch_svalue *v = sv_new(scratch, SV_NULL);
        return v;
    }
    }
}

/* jv -> sv for schema-side enum/const values */
static sch_svalue *jv_to_sv(vctx *vc, jv *v) {
    switch (jv_kind_of(v)) {
    case JV_NULL: return sv_new(vc->scratch, SV_NULL);
    case JV_BOOL: {
        sch_svalue *sv = sv_new(vc->scratch, SV_BOOL);
        sv->b = jv_bool(v);
        return sv;
    }
    case JV_INT: {
        sch_svalue *sv = sv_new(vc->scratch, SV_INT);
        sv->i = jv_int(v);
        return sv;
    }
    case JV_NUM: {
        sch_svalue *sv = sv_new(vc->scratch, SV_FLOAT);
        sv->f = jv_num(v);
        return sv;
    }
    case JV_STRING: {
        sch_svalue *sv = sv_new(vc->scratch, SV_STRING);
        size_t len = jv_str_len(v);
        char *buf = teptris_arena_alloc(vc->scratch, len + 1);
        memcpy(buf, jv_str(v), len);
        buf[len] = '\0';
        sv->str = buf;
        sv->str_len = len;
        return sv;
    }
    case JV_ARRAY: {
        sch_svalue *sv = sv_new(vc->scratch, SV_ARRAY);
        size_t len = jv_item_count(v);
        sv->items = teptris_arena_alloc(vc->scratch, (len ? len : 1) * sizeof(sch_svalue *));
        for (size_t i = 0; i < len; i++) {
            sv->items[i] = jv_to_sv(vc, jv_item(v, i));
        }
        sv->n_items = len;
        return sv;
    }
    case JV_OBJECT: {
        sch_svalue *sv = sv_new(vc->scratch, SV_TABLE);
        size_t len = jv_member_count(v);
        sv->members = teptris_arena_alloc(vc->scratch, (len ? len : 1) * sizeof(sv_member));
        for (size_t i = 0; i < len; i++) {
            size_t klen = strlen(jv_member_key(v, i));
            char *kbuf = teptris_arena_alloc(vc->scratch, klen + 1);
            memcpy(kbuf, jv_member_key(v, i), klen + 1);
            sv->members[i].key = kbuf;
            sv->members[i].val = jv_to_sv(vc, jv_member_val(v, i));
        }
        sv->n_members = len;
        return sv;
    }
    }
    return sv_new(vc->scratch, SV_NULL);
}

/* ---------------------------------------------------------- public -- */

TEPTRIS_API teptris_status teptris_schema_validate_doc(
    const struct teptris_schema *sch, const teptris_document *doc, char **buf,
    size_t *out_len) {
    if (sch == NULL || doc == NULL || buf == NULL || out_len == NULL) {
        return TEPTRIS_ERR_ARG;
    }
    *buf = NULL;
    *out_len = 0;

    teptris_arena scratch;
    teptris_arena_init(&scratch);
    const teptris_node *root = teptris_document_root(doc);
    sch_svalue *rv = node_to_sv(&scratch, root);

    vctx vc;
    memset(&vc, 0, sizeof(vc));
    vc.scratch = &scratch;

    val(&vc, sch->root, rv);

    teptris_status st = TEPTRIS_OK;
    /* the envelope is always one JSON array: wrap the comma-joined
     * error objects (or emit the empty array) */
    size_t body_n = vc.n;
    size_t out_n = body_n > 0 ? body_n + 2 : 2;
    char *out = malloc(out_n + 1);
    if (out == NULL) {
        st = TEPTRIS_ERR_ALLOC;
    } else {
        if (body_n > 0) {
            out[0] = '[';
            memcpy(out + 1, vc.buf, body_n);
            out[body_n + 1] = ']';
            out[out_n] = '\0';
        } else {
            memcpy(out, "[]", 3);
        }
        *buf = out;
        *out_len = out_n;
    }
    if (vc.buf != NULL) free(vc.buf);
    teptris_arena_destroy(&scratch);
    return st;
}

TEPTRIS_API teptris_status teptris_schema_validate(const struct teptris_schema *sch,
                                                   const char *toml, size_t len,
                                                   char **buf, size_t *out_len) {
    if (sch == NULL || toml == NULL || buf == NULL || out_len == NULL) {
        return TEPTRIS_ERR_ARG;
    }
    teptris_document *doc = NULL;
    teptris_status st = teptris_parse(toml, len, NULL, &doc);
    if (st != TEPTRIS_OK) {
        if (doc != NULL) teptris_document_free(doc);
        return st;
    }
    st = teptris_schema_validate_doc(sch, doc, buf, out_len);
    teptris_document_free(doc);
    return st;
}
