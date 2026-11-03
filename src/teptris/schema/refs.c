/* The resource model: JSON Schema resources (a base URI + the jv
 * subtree it names), relative-reference joining, and file-backed
 * resource loading. A parser library never touches the network:
 * http(s) targets are rejected by the caller; file-backed resources
 * are size-capped and memoized per canonical path. */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json_read.h"
#include "refs.h"
#include "schema_internal.h"
#include "teptris/memory/arena.h"

/* load (or memoize) the file-backed resource named by a base URI */
jv *sch_load_resource(sctx *c, const char *abs_uri) {
    char path[TEPTRIS_URI_MAX];
    if (!teptris_file_uri_to_path(abs_uri, path, sizeof path)) return NULL;
    for (size_t i = 0; i < c->n_resources; i++) {
        if (strcmp(c->resources[i].abs, abs_uri) == 0) {
            return c->resources[i].root;
        }
    }
    if (c->n_resources >= 64) return NULL; /* the resource fan-out cap */
    size_t len = 0;
    char *text = malloc(TEPTRIS_RESOURCE_MAX_BYTES);
    if (text == NULL) return NULL;
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        free(text);
        return NULL;
    }
    len = fread(text, 1, TEPTRIS_RESOURCE_MAX_BYTES, f);
    fclose(f);
    if (len == 0) {
        free(text);
        return NULL;
    }
    char jerr[160] = {0};
    jv *root = json_read(&c->a, text, len, jerr, sizeof jerr);
    free(text);
    if (root == NULL) return NULL;

    if (c->n_resources == c->cap_resources) {
        c->cap_resources = c->cap_resources == 0 ? 8 : c->cap_resources * 2;
        void *nr = teptris_arena_alloc(c->a, c->cap_resources * sizeof(*c->resources));
        if (c->n_resources > 0) {
            memcpy(nr, c->resources, c->n_resources * sizeof(*c->resources));
        }
        c->resources = nr;
    }
    teptris_resource *res = &c->resources[c->n_resources++];
    res->abs = teptris_arena_alloc(c->a, strlen(abs_uri) + 1);
    strcpy((char *)res->abs, abs_uri);
    res->root = root;
    res->node = NULL;
    res->recursive_anchor = false;
    return root;
}

/* ---------------------------------------------------- uri joining -- */

/* the directory part of a base URI path ("file:///a/b/c.json" ->
 * "file:///a/b/"); the base itself when there is no slash */
static void uri_dir(const char *base, char *out, size_t cap) {
    if (base == NULL) {
        out[0] = '\0';
        return;
    }
    const char *slash = strrchr(base, '/');
    if (slash == NULL) {
        snprintf(out, cap, "%s", base);
        return;
    }
    size_t n = (size_t)(slash - base) + 1;
    if (n >= cap) n = cap - 1;
    memcpy(out, base, n);
    out[n] = '\0';
}

static void remove_dot_segments(char *path) {
    /* RFC 3986 5.2.4 over the whole string; paths here are simple
     * enough that the loop form suffices */
    char *w = path;
    char *r = path;
    while (*r != '\0') {
        if (r[0] == '.' && r[1] == '/' ) {
            r += 2;
        } else if (r[0] == '.' && r[1] == '.' && r[2] == '/') {
            r += 3;
            /* pop the last segment of w */
            char *slash = NULL;
            for (char *p = w; *p != '\0'; p++) {
                if (*p == '/') slash = p;
            }
            if (slash != NULL && slash > path) {
                *slash = '\0';
            } else if (slash == path) {
                slash[1] = '\0';
            } else {
                w[0] = '\0';
            }
        } else {
            while (*r != '\0' && *r != '/') *w++ = *r++;
            if (*r == '/') *w++ = *r++;
        }
    }
    *w = '\0';
}

/* join a relative reference against a base; scheme:// targets and
 * absolute paths stand alone. NULL when the reference cannot be
 * resolved (caller decides whether that is a rejection) */
bool teptris_uri_join(const char *base, const char *ref, char *out,
                      size_t cap) {
    out[0] = '\0';
    if (ref == NULL) return false;
    if (strncmp(ref, "http://", 7) == 0 || strncmp(ref, "https://", 8) == 0) {
        /* network targets never resolve here */
        return false;
    }
    {
        /* any other scheme ("urn:uuid:...", "mailto:...") is an
         * absolute URI: it stands alone and may name an in-memory
         * resource (see the resource table) */
        const char *p = ref;
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z')) {
            while ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                   (*p >= '0' && *p <= '9') || *p == '+' || *p == '-' ||
                   *p == '.') {
                p++;
            }
            if (*p == ':' && p != ref) {
                snprintf(out, cap, "%s", ref);
                return true;
            }
        }
    }
    if (strncmp(ref, "file://", 7) == 0) {
        snprintf(out, cap, "%s", ref);
        return true;
    }
    if (ref[0] == '/') {
        /* a network-path reference: no base directory applies, but an
         * http(s) base still lends its scheme and authority */
        if (strncmp(base, "http://", 7) == 0 ||
            strncmp(base, "https://", 8) == 0) {
            const char *auth = strstr(base, "://") + 3;
            const char *end = strchr(auth, '/');
            size_t n = end != NULL ? (size_t)(end - base) : strlen(base);
            if (n >= cap || strlen(ref) + n + 1 >= cap) return false;
            memcpy(out, base, n);
            strcpy(out + n, ref);
            return true;
        }
        /* absolute path: no base directory applies */
        char tmp[TEPTRIS_URI_MAX];
        snprintf(tmp, sizeof tmp, "%s", ref);
        remove_dot_segments(tmp);
        snprintf(out, cap, "file://%s", tmp);
        return true;
    }
    if (base == NULL || base[0] == '\0') return false;
    if (strncmp(base, "http://", 7) == 0 || strncmp(base, "https://", 8) == 0) {
        /* merge in URI space (RFC 3986 5.3): suite documents name
         * in-memory resources with http-space $ids; resolution merges
         * against that space and only ever hits the resource table —
         * nothing here is ever fetched from the network */
        const char *auth = strstr(base, "://");
        if (auth == NULL) return false;
        /* merge uses the base's directory: up to the LAST slash */
        const char *last_slash = strrchr(auth + 3, '/');
        if (last_slash == NULL) return false;
        size_t n = (size_t)(last_slash - base) + 1;
        if (n >= cap) return false;
        memcpy(out, base, n);
        out[n] = '\0';
        char path_part[TEPTRIS_URI_MAX];
        const char *hash = strchr(ref, '#');
        size_t plen = hash != NULL ? (size_t)(hash - ref) : strlen(ref);
        if (plen >= sizeof path_part) return false;
        memcpy(path_part, ref, plen);
        path_part[plen] = '\0';
        if (strlen(out) + plen + 1 > cap) return false;
        strcat(out, path_part);
        remove_dot_segments(out + n);
        return true;
    }
    if (strncmp(base, "file://", 7) != 0) {
        /* a non-file base (an http base that was rewritten away, a
         * bare id) cannot host relative references without fs backing
         * unless the base itself is a plain path */
        snprintf(out, cap, "%s", ref);
        return true; /* resolved against the CWD by the caller */
    }
    char dir[TEPTRIS_URI_MAX];
    uri_dir(base, dir, sizeof dir);
    size_t n = strlen(dir);
    if (n >= cap) return false;
    memcpy(out, dir, n + 1);
    /* the reference may carry a fragment; split it now */
    char path_part[TEPTRIS_URI_MAX];
    const char *hash = strchr(ref, '#');
    size_t plen = hash != NULL ? (size_t)(hash - ref) : strlen(ref);
    if (plen >= sizeof path_part) return false;
    memcpy(path_part, ref, plen);
    path_part[plen] = '\0';
    if (strlen(out) + strlen(path_part) + 1 > cap) return false;
    strcat(out, path_part);
    /* normalize the path part only (after file://) */
    char *path_start = out + 7;
    if (strncmp(out, "file://", 7) == 0) {
        remove_dot_segments(path_start);
    }
    return true;
}

/* split "scheme://path#fragment" into the document part and the
 * fragment part (fragment may be NULL/empty) */
void teptris_uri_split(const char *uri, char *doc, size_t doc_cap,
                       char *frag, size_t frag_cap) {
    doc[0] = frag[0] = '\0';
    const char *hash = strchr(uri, '#');
    size_t dlen = hash != NULL ? (size_t)(hash - uri) : strlen(uri);
    if (dlen >= doc_cap) dlen = doc_cap - 1;
    memcpy(doc, uri, dlen);
    doc[dlen] = '\0';
    if (hash != NULL && hash[1] != '\0') {
        snprintf(frag, frag_cap, "%s", hash + 1);
    }
}

/* -------------------------------------------------- file resources -- */

static char *read_file_ascii(const char *path, size_t cap, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) return NULL;
    char *buf = malloc(cap + 1);
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }
    size_t n = fread(buf, 1, cap, f);
    fclose(f);
    if (n == 0) {
        free(buf);
        return NULL;
    }
    buf[n] = '\0';
    *out_len = n;
    return buf;
}

bool teptris_file_uri_to_path(const char *uri, char *path, size_t cap) {
    if (strncmp(uri, "file://", 7) == 0) {
        snprintf(path, cap, "%s", uri + 7);
        return true;
    }
    snprintf(path, cap, "%s", uri);
    return true;
}
