/* Format-preserving scalar edit (v1): rewrite exactly the value bytes
 * of one key = value line; every other byte of the source is
 * preserved. The target is addressed by a dotted key path that may
 * carry a [table] prefix ("t.x" addresses x inside [t]; the LAST
 * component is the key, everything before it is the table path that
 * must match the current [section]). The replacement is the value's
 * TEXT and is validated by parsing "k = <replacement>" - the editor
 * stays value-agnostic.
 *
 * v1 scope: single-line scalar values (numbers, booleans, dates,
 * single-line quoted strings). Multiline strings and inline
 * collections are rejected with TEPTRIS_ERR_ARG rather than
 * half-understood. */
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

/* the value span on a keyval line: p sits past '='. Absolute [start,
 * end) of the value bytes. Single-line scalars only: returns 0, or
 * -1 for multiline/inline targets. */
static int edit_value_span(const char *p, const char *end,
                           const char **vstart, const char **vend)
{
    p = edit_skip_ws(p, end);
    if (p >= end) {
        return -1;
    }
    const char *vs = p;
    if (*p == '\'') {
        p++;
        while (p < end && *p != '\'') {
            if (*p == '\n') {
                return -1;
            }
            p++;
        }
        if (p >= end) {
            return -1;
        }
        p++;
    } else if (*p == '"') {
        if (p + 2 < end && p[1] == '"' && p[2] == '"') {
            return -1; /* multiline basic */
        }
        p++;
        while (p < end && *p != '"') {
            if (*p == '\n') {
                return -1;
            }
            if (*p == '\\' && p + 1 < end) {
                p += 2;
                continue;
            }
            p++;
        }
        if (p >= end) {
            return -1;
        }
        p++;
    } else if (*p == '{' || *p == '[') {
        return -1; /* inline collections: v1 out of scope */
    } else {
        while (p < end && *p != '\n' && *p != '#') {
            p++;
        }
        while (p > vs && edit_is_ws(p[-1])) {
            p--;
        }
    }
    *vstart = vs;
    *vend = p;
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

    /* validate the replacement by parsing it as a keyval */
    {
        size_t rlen = strlen(replacement);
        if (rlen == 0 || rlen > 4096) {
            return TEPTRIS_ERR_ARG;
        }
        if (strchr(replacement, '\n') != NULL) {
            return TEPTRIS_ERR_ARG; /* v1: single-line replacements */
        }
        char probe[4200];
        int n = snprintf(probe, sizeof probe, "k = %s\n", replacement);
        if (n <= 0 || (size_t)n >= sizeof probe) {
            return TEPTRIS_ERR_ARG;
        }
        teptris_document *probe_doc = NULL;
        teptris_status st = teptris_parse(probe, (size_t)n, NULL, &probe_doc);
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
                }
                if (after != NULL) {
                    const char *eq = edit_skip_ws(after, le);
                    if (eq < le && *eq == '=') {
                        const char *vs;
                        const char *ve;
                        if (edit_value_span(eq + 1, le, &vs, &ve) != 0) {
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
