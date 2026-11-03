/* The resource model: base URIs, relative-reference joining, and
 * file-backed resource loading. Internal to the schema subsystem. */
#ifndef TEPTRIS_SCHEMA_REFS_H
#define TEPTRIS_SCHEMA_REFS_H

#include <stdbool.h>
#include <stddef.h>

#define TEPTRIS_URI_MAX 1024
#define TEPTRIS_RESOURCE_MAX_BYTES (1u << 20)

/* join a relative reference against a base URI; the caller owns the
 * rejection policy for http(s) and unbacked bases */
bool teptris_uri_join(const char *base, const char *ref, char *out,
                      size_t cap);

/* split "doc#fragment" (fragment may come back empty) */
void teptris_uri_split(const char *uri, char *doc, size_t doc_cap,
                       char *frag, size_t frag_cap);

/* file:// URIs to plain paths (plain paths pass through) */
bool teptris_file_uri_to_path(const char *uri, char *path, size_t cap);

#endif
