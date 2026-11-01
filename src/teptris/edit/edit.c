/* Format-preserving scalar edit (v2): rewrite exactly the value bytes
 * of one key = value line; every other byte of the source is
 * preserved. The target is addressed by a dotted key path that may
 * carry a [table] prefix ("t.x" addresses x inside [t]; the LAST
 * component is the key, everything before it is the table path that
 * must match the current [section]). The replacement is the value's
 * TEXT and is validated by parsing "k = <replacement>" - the editor
 * stays value-agnostic.
 *
 * v2 scope: single-line scalars, multiline strings and inline
 * collections all splice; v2 spans are parse-verified before handback
 * so a mis-scanned span rejects instead of corrupting. CRLF line
 * endings are preserved (the line-tail \r is outside the span). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "teptris/teptris.h"

static int edit_is_ws(char c)
{
    return c == ' ' || c == '\t';
}

static const char *edit_skip_ws(const char *p, const char *end)
{
    while (p < end && edit_is_ws(*p)) {
        p++;
    }
    return p;
}

/* match one key component (bare or quoted) against the source at p;
 * returns the pointer past the component, else NULL */
static const char *edit_match_component(const char *p, const char *end,
                                        const char *comp, size_t comp_len)
{
    if (p >= end) {
        return NULL;
    }
    if (*p == '\'' || *p == '"') {
        char q = *p;
        p++;
        size_t ci = 0;
        while (p < end && *p != q) {
            char c = *p;
            if (c == '\n') {
                return NULL;
            }
            if (q == '"' && c == '\\' && p + 1 < end) {
                /* raw-byte comparison: an escaped component character
                 * simply fails to match (v1 keeps key matching
                 * escape-naive) */
                if (ci < comp_len && comp[ci] != c) {
                    return NULL;
                }
                ci++;
                p += 2;
                continue;
            }
            if (ci >= comp_len || comp[ci] != c) {
                return NULL;
            }
            ci++;
            p++;
        }
        if (p >= end || ci != comp_len) {
            return NULL;
        }
        return p + 1;
    }
    if (comp_len == 0) {
        return NULL;
    }
    if ((size_t)(end - p) < comp_len || memcmp(p, comp, comp_len) != 0) {
        return NULL;
    }
    p += comp_len;
    if (p == end || p[0] == '.' || edit_is_ws(p[0]) || p[0] == '=' ||
        p[0] == ']') {
        return p;
    }
    return NULL;
}

/* match a full dotted path at p; returns the pointer past the path,
 * else NULL. Dots inside quotes do not split. */
static const char *edit_match_path(const char *p, const char *end,
                                   const char *path, size_t path_len)
{
    const char *pc = path;
    const char *pend = path + path_len;
    for (;;) {
        while (pc < pend && edit_is_ws(*pc)) {
            pc++;
        }
        const char *cstart = pc;
        int quoted = 0;
        while (pc < pend) {
            if (*pc == '\'' || *pc == '"') {
                quoted = !quoted;
            } else if (*pc == '.' && !quoted) {
                break;
            }
            pc++;
        }
        if (quoted) {
            return NULL;
        }
        const char *cend = pc;
        while (cend > cstart && edit_is_ws(cend[-1])) {
            cend--;
        }
        while (cstart < cend && edit_is_ws(*cstart)) {
            cstart++;
        }
        if (cstart == cend) {
            return NULL;
        }
        p = edit_match_component(p, end, cstart, (size_t)(cend - cstart));
        if (p == NULL) {
            return NULL;
        }
        p = edit_skip_ws(p, end);
        if (pc < pend && *pc == '.') {
            pc++;
            if (p < end && *p == '.') {
                p++; /* consume the source's separator too */
                p = edit_skip_ws(p, end);
            }
            continue;
        }
        return p;
    }
}

/* does the line carry a [section] or [[aot]] header? fills the path
 * span (between the brackets, trimmed) */
static int edit_section_header(const char *p, const char *end,
                               const char **path, size_t *path_len)
{
    p = edit_skip_ws(p, end);
    if (p >= end || *p != '[') {
        return 0;
    }
    p++;
    if (p < end && *p == '[') {
        p++;
    }
    p = edit_skip_ws(p, end);
    const char *start = p;
    while (p < end && *p != ']') {
        if (*p == '\n') {
            return 0;
        }
        p++;
    }
    if (p >= end) {
        return 0;
    }
    const char *stop = p;
    while (stop > start && edit_is_ws(stop[-1])) {
        stop--;
    }
    *path = start;
    *path_len = (size_t)(stop - start);
    return 1;
}

/* Skip one string spanning from its opening quote at p; mirrors the
 * engine's scanners: quote-run close (run >= 3 for multiline), escape
 * tracking in basic strings. Returns the pointer past the string, or
 * NULL when unterminated. */
static const char *edit_skip_string(const char *p, const char *end)
{
    char q = *p;
    int ml = (p + 2 < end && p[1] == q && p[2] == q);
    p += ml ? 3 : 1;
    while (p < end) {
        char c = *p;
        if (q == '"' && c == '\\') {
            if (p + 1 >= end) {
                return NULL; /* unterminated escape */
            }
            p += 2;
            continue;
        }
        if (c == q) {
            int run = 0;
            while (p + run < end && p[run] == q) {
                run++;
            }
            if (!ml) {
                return p + 1; /* single-line closes at the first quote */
            }
            if (run >= 3) {
                return p + run; /* multiline closes at the run */
            }
            p += run; /* 1-2 quotes inside multiline: content */
            continue;
        }
        p++;
    }
    return NULL;
}

/* the value span of a keyval line: p sits past '='. Absolute [start,
 * end) of the value bytes. Returns 0 with *verified unset for the
 * single-line scalar fast path (v1 contract: no parse required), or 1
 * for string/inline spans whose splice must be parse-verified, or -1
 * for a malformed span. */
static int edit_value_span(const char *p, const char *end,
                           const char **vstart, const char **vend,
                           int *verified)
{
    p = edit_skip_ws(p, end);
    if (p >= end) {
        return -1;
    }
    const char *vs = p;
    const char *ve = NULL;

    if (*p == '\'' || *p == '"') {
        p = edit_skip_string(p, end);
        if (p == NULL) {
            return -1;
        }
        ve = p;
        *verified = 1;
    } else if (*p == '{' || *p == '[') {
        int depth = 0;
        while (p < end) {
            char c = *p;
            if (c == '\'' || c == '"') {
                p = edit_skip_string(p, end);
                if (p == NULL) {
                    return -1;
                }
                continue;
            }
            if (c == '{' || c == '[') {
                depth++;
            } else if (c == '}' || c == ']') {
                depth--;
                p++;
                if (depth == 0) {
                    ve = p;
                    break;
                }
                continue;
            }
            p++;
        }
        if (ve == NULL) {
            return -1; /* unbalanced inline collection */
        }
        *verified = 1;
    } else {
        while (p < end && *p != '\n' && *p != '#') {
            p++;
        }
        while (p > vs && (edit_is_ws(p[-1]) || p[-1] == '\r')) {
            p--; /* CRLF: the line-tail \r stays outside the span */
        }
        ve = p;
        *verified = 0;
    }
    *vstart = vs;
    *vend = ve;
    return 0;
}

/* Split the target keypath at its LAST unquoted '.': the prefix is
 * the table path (must textually match the open [section]), the last
 * component is the key written on the line. A dotted key LINE
 * ("a.b = 1") additionally matches when its full written path equals
 * the target - handled by trying the full-path match first. */
static void edit_split_target(const char *keypath, size_t klen,
                              const char **prefix, size_t *prefix_len,
                              const char **last, size_t *last_len)
{
    const char *kp_end = keypath + klen;
    const char *dot = NULL;
    int quoted = 0;
    for (const char *q = keypath; q < kp_end; q++) {
        if (*q == '\'' || *q == '"') {
            quoted = !quoted;
        } else if (*q == '.' && !quoted) {
            dot = q;
        }
    }
    if (dot == NULL) {
        *prefix = NULL;
        *prefix_len = 0;
        *last = keypath;
        *last_len = klen;
        return;
    }
    *prefix = keypath;
    *prefix_len = (size_t)(dot - keypath);
    *last = dot + 1;
    *last_len = (size_t)(kp_end - (dot + 1));
}

/* raw textual comparison of the open section against the target's
 * table prefix (v1: quoting/spacing variants of the same path do not
 * match; the canonical form does) */
static int edit_section_matches(const char *prefix, size_t prefix_len,
                                const char *sec, size_t sec_len)
{
    const char *ps = prefix;
    while (prefix_len > 0 && edit_is_ws(ps[0])) {
        ps++;
        prefix_len--;
    }
    while (prefix_len > 0 && edit_is_ws(ps[prefix_len - 1])) {
        prefix_len--;
    }
    if (prefix_len == 0) {
        return sec_len == 0; /* bare key: root only */
    }
    if (sec_len != prefix_len) {
        return 0;
    }
    return memcmp(sec, ps, prefix_len) == 0;
}

/* Raw-textual check: does the target keypath, after its trimmed
 * section prefix, CONTINUE the open section? [items] + target
 * "items.a.b" -> the line may write "a.b" or "b"; this yields "a.b".
 * Canonical-textual only (quoting variants do not match), consistent
 * with edit_section_matches. */
static int edit_strip_section_prefix(const char *keypath, size_t klen,
                                     const char *sec, size_t sec_len,
                                     const char **rest, size_t *rest_len)
{
    while (klen > 0 && edit_is_ws(keypath[0])) {
        keypath++;
        klen--;
    }
    while (klen > 0 && edit_is_ws(keypath[klen - 1])) {
        klen--;
    }
    if (sec_len == 0 || klen <= sec_len) {
        return 0;
    }
    if (keypath[sec_len] != '.' ||
        memcmp(keypath, sec, sec_len) != 0) {
        return 0;
    }
    *rest = keypath + sec_len + 1;
    *rest_len = klen - sec_len - 1;
    return *rest_len > 0;
}

teptris_status teptris_edit_scalar(const char *src, size_t len,
                                   const char *keypath,
                                   const char *replacement,
                                   char **buf, size_t *out_len)
{
    if (src == NULL || keypath == NULL || replacement == NULL ||
        buf == NULL || out_len == NULL) {
        return TEPTRIS_ERR_ARG;
    }
    const char *end = src + len;

    /* validate the replacement by parsing it as a keyval; multiline
     * replacements (multiline-string values) are fine - the probe
     * parse is the only gate */
    {
        size_t rlen = strlen(replacement);
        if (rlen == 0 || rlen > 16384) {
            return TEPTRIS_ERR_ARG;
        }
        char *probe = malloc(rlen + 16);
        if (probe == NULL) {
            return TEPTRIS_ERR_ALLOC;
        }
        int n = snprintf(probe, rlen + 16, "k = %s\n", replacement);
        teptris_document *probe_doc = NULL;
        teptris_status st = teptris_parse(probe, (size_t)n, NULL,
                                          &probe_doc);
        free(probe);
        if (st != TEPTRIS_OK) {
            if (probe_doc != NULL) {
                teptris_document_free(probe_doc);
            }
            return TEPTRIS_ERR_ARG;
        }
        teptris_document_free(probe_doc);
    }

    size_t klen = strlen(keypath);
    const char *sec = NULL; /* current [section] path span */
    size_t sec_len = 0;

    const char *p = src;
    while (p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        const char *le = nl ? nl : end;
        const char *t = edit_skip_ws(p, le);
        if (t < le && *t != '#') {
            const char *hp;
            size_t hlen;
            if (edit_section_header(t, le, &hp, &hlen)) {
                sec = hp;
                sec_len = hlen;
            } else {
                const char *after = NULL;
                if (sec_len == 0) {
                    /* root: a dotted key LINE may carry the full path */
                    after = edit_match_path(t, le, keypath, klen);
                }
                if (after == NULL) {
                    /* the line carries just the key: the last target
                     * component, under a matching open section */
                    const char *pfx;
                    size_t pfx_len;
                    const char *lastc;
                    size_t last_len;
                    edit_split_target(keypath, klen, &pfx, &pfx_len,
                                      &lastc, &last_len);
                    if (last_len > 0 &&
                        edit_section_matches(pfx, pfx_len, sec, sec_len)) {
                        after = edit_match_path(t, le, lastc, last_len);
                    }
                    if (after == NULL && sec_len > 0) {
                        /* the line writes a dotted path that CONTINUES
                         * the open section: "a.b = 1" under [items]
                         * with target "items.a.b" */
                        const char *rest;
                        size_t rest_len;
                        if (edit_strip_section_prefix(keypath, klen, sec,
                                                      sec_len, &rest,
                                                      &rest_len)) {
                            after = edit_match_path(t, le, rest, rest_len);
                        }
                    }
                }
                if (after != NULL) {
                    const char *eq = edit_skip_ws(after, le);
                    if (eq < le && *eq == '=') {
                        const char *vs;
                        const char *ve;
                        int verified = 0;
                        /* the span scan bounds at the whole source:
                         * multiline and inline values cross lines */
                        if (edit_value_span(eq + 1, end, &vs, &ve,
                                            &verified) != 0) {
                            return TEPTRIS_ERR_ARG;
                        }
                        size_t rlen = strlen(replacement);
                        size_t pre = (size_t)(vs - src);
                        size_t post = (size_t)(end - ve);
                        char *out = malloc(pre + rlen + post);
                        if (out == NULL) {
                            return TEPTRIS_ERR_ALLOC;
                        }
                        memcpy(out, src, pre);
                        memcpy(out + pre, replacement, rlen);
                        memcpy(out + pre + rlen, ve, post);
                        if (verified) {
                            /* v2 spans are parse-verified: a
                             * mis-scanned span rejects instead of
                             * corrupting */
                            teptris_document *vd = NULL;
                            teptris_status vst = teptris_parse(out,
                                pre + rlen + post, NULL, &vd);
                            if (vst != TEPTRIS_OK) {
                                if (vd != NULL) {
                                    teptris_document_free(vd);
                                }
                                free(out);
                                return TEPTRIS_ERR_ARG;
                            }
                            teptris_document_free(vd);
                        }
                        *buf = out;
                        *out_len = pre + rlen + post;
                        return TEPTRIS_OK;
                    }
                }
            }
        }
        if (nl == NULL) {
            break;
        }
        p = nl + 1;
    }
    return TEPTRIS_ERR_STATE; /* key not found */
}
