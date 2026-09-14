#ifndef TEPTRIS_TEPTRIS_H
#define TEPTRIS_TEPTRIS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "teptris/version.h"

/* Export annotation: the shared library builds with TEPTRIS_BUILDING_DLL
 * (dllexport); consumers may define TEPTRIS_USE_DLL for dllimport, and
 * static builds link plainly. Non-Windows platforms export by default. */
#if defined(_WIN32)
#  if defined(TEPTRIS_BUILDING_DLL)
#    define TEPTRIS_API __declspec(dllexport)
#  elif defined(TEPTRIS_USE_DLL)
#    define TEPTRIS_API __declspec(dllimport)
#  else
#    define TEPTRIS_API
#  endif
#else
#  define TEPTRIS_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handles. Every node handed out by the read API is owned by its
 * document and valid until teptris_document_free. */
typedef struct teptris_document teptris_document;
typedef struct teptris_node teptris_node;

typedef enum teptris_status {
    TEPTRIS_OK = 0,
    TEPTRIS_ERR_ALLOC,
    TEPTRIS_ERR_SYNTAX,
    TEPTRIS_ERR_SEMANTIC,
    TEPTRIS_ERR_ENCODING,
    TEPTRIS_ERR_DEPTH,
    TEPTRIS_ERR_ARG,
    TEPTRIS_ERR_STATE
} teptris_status;

typedef enum teptris_kind {
    TEPTRIS_STRING = 0,
    TEPTRIS_INTEGER,
    TEPTRIS_FLOAT,
    TEPTRIS_BOOLEAN,
    TEPTRIS_DATETIME_OFFSET,
    TEPTRIS_DATETIME_LOCAL,
    TEPTRIS_DATE_LOCAL,
    TEPTRIS_TIME_LOCAL,
    TEPTRIS_ARRAY,
    TEPTRIS_TABLE
} teptris_kind;

/* Views are (ptr, len); string values are additionally NUL-terminated.
 * Table keys are views into the parse input and may not be. */
typedef struct teptris_view {
    const char *ptr;
    size_t len;
} teptris_view;

typedef struct teptris_datetime {
    int32_t year;                /* valid for DATE/DATETIME kinds */
    uint8_t month, day;          /* 1-based */
    uint8_t hour, minute, second; /* second may be 60 (leap second) */
    uint32_t nanosecond;
    int32_t offset_seconds;      /* valid for DATETIME_OFFSET only */
} teptris_datetime;

typedef struct teptris_error {
    teptris_status status;
    size_t line;   /* 1-based */
    size_t column; /* 1-based */
    const char *message; /* NUL-terminated, document-owned */
} teptris_error;

typedef struct teptris_options {
    uint32_t max_depth; /* 0 = default (512) */
    uint32_t _reserved[3];
} teptris_options;

/* Parse a TOML 1.0 document. On failure the status is returned AND *out
 * is set (non-NULL) so the error can be read; free it either way.
 * Table-key views point into `data`: the caller must keep the input
 * buffer alive for the lifetime of the document.
 * Memory: *out must be freed with teptris_document_free. */
TEPTRIS_API teptris_status teptris_parse(const char *data, size_t len,
                             const teptris_options *opts,
                             teptris_document **out);

/* Memory: frees every allocation reachable from the document. */
TEPTRIS_API void teptris_document_free(teptris_document *doc);

TEPTRIS_API const teptris_error * teptris_document_error(const teptris_document *doc);
TEPTRIS_API const teptris_node * teptris_document_root(const teptris_document *doc);

TEPTRIS_API teptris_kind teptris_node_kind(const teptris_node *node);

/* Type-checked scalar accessors: return TEPTRIS_ERR_ARG on kind mismatch
 * (or TEPTRIS_ERR_STATE with a NULL node). */
TEPTRIS_API teptris_status teptris_node_string(const teptris_node *node, teptris_view *out);
TEPTRIS_API teptris_status teptris_node_integer(const teptris_node *node, int64_t *out);
TEPTRIS_API teptris_status teptris_node_float(const teptris_node *node, double *out);
TEPTRIS_API teptris_status teptris_node_boolean(const teptris_node *node, bool *out);
TEPTRIS_API teptris_status teptris_node_datetime(const teptris_node *node,
                                     teptris_datetime *out);

TEPTRIS_API size_t teptris_node_array_length(const teptris_node *node);
TEPTRIS_API const teptris_node * teptris_node_array_at(const teptris_node *node, size_t index);

/* Iteration is in insertion order. */
TEPTRIS_API size_t teptris_node_table_length(const teptris_node *node);
TEPTRIS_API const teptris_node * teptris_node_table_at(const teptris_node *node, size_t index,
                                          teptris_view *key_out);
TEPTRIS_API const teptris_node * teptris_node_table_get(const teptris_node *node,
                                           const char *key, size_t key_len);

/* Emit canonical TOML (deterministic; parse(emit(d)) == d).
 * Memory: *buf is malloc'd; caller frees with free(). */
TEPTRIS_API teptris_status teptris_document_emit(const teptris_document *doc,
                                     char **buf, size_t *len);

/* Emit the toml-test wire shape: scalars as {"type":..,"value":..},
 * tables as objects, arrays as lists, insertion order.
 * Memory: *buf is malloc'd; caller frees with free(). */
TEPTRIS_API teptris_status teptris_document_emit_json(const teptris_document *doc,
                                          char **buf, size_t *len);

/* Bulk drain for FFI/ctypes bindings: the whole tree in ONE crossing as
 * a self-describing flat buffer (explicit little-endian):
 *   table  0x01 u32 n *(u32 klen key node)
 *   array  0x02 u32 n *(node)
 *   string 0x03 u32 len bytes
 *   int    0x04 i64
 *   float  0x05 f64
 *   bool   0x06 / 0x07
 *   dt     0x08..0x0B (offset/local-dt/date/time) i64 y u8 mo d h mi s u32 ns i64 off (25 B)
 * Memory: *buf is malloc'd; free with teptris_flatten_free. */
TEPTRIS_API teptris_status teptris_document_flatten(const teptris_document *doc,
                                        uint8_t **buf, size_t *len);
TEPTRIS_API void teptris_flatten_free(void *buf);

TEPTRIS_API const char * teptris_status_string(teptris_status status);
TEPTRIS_API const char * teptris_version_string(void);

/* ------------------------------------------------------------- builder --
 * Construct a document from materialized values (the dump side of
 * parse): every emission flows through teptris_document_emit, so the
 * emitter stays the single source of formatting truth.
 *
 * Stack discipline: open_* pushes a container, teptris_builder_close
 * pops it; scalars attach to the current container. A NULL key means
 * "element of the current array"; a keyed call requires a table
 * current. Array nature is derived: the first table element makes the
 * array an array-of-tables ([[..]] sections); scalars make it inline;
 * mixing them is TEPTRIS_ERR_ARG. Tables reachable only as array
 * elements or from inline tables render inline ({..}); tables under a
 * section render as [..] sections.
 *
 * Keys and strings are copied into the document (no parse input is
 * kept alive). Duplicate keys are TEPTRIS_ERR_ARG.
 * Memory: the builder owns its document until teptris_builder_finish
 * transfers it; otherwise teptris_builder_free destroys it. */
typedef struct teptris_builder teptris_builder;

TEPTRIS_API teptris_builder *teptris_builder_new(void);
TEPTRIS_API void teptris_builder_free(teptris_builder *b);

TEPTRIS_API teptris_status teptris_builder_put_string(teptris_builder *b,
                                        const char *key, size_t key_len,
                                        const char *val, size_t val_len);
TEPTRIS_API teptris_status teptris_builder_put_integer(teptris_builder *b,
                                        const char *key, size_t key_len,
                                        int64_t v);
TEPTRIS_API teptris_status teptris_builder_put_float(teptris_builder *b,
                                        const char *key, size_t key_len,
                                        double v);
TEPTRIS_API teptris_status teptris_builder_put_boolean(teptris_builder *b,
                                        const char *key, size_t key_len,
                                        bool v);
/* kind is one of TEPTRIS_DATETIME_OFFSET, TEPTRIS_DATETIME_LOCAL,
 * TEPTRIS_DATE_LOCAL, TEPTRIS_TIME_LOCAL; fields must be zero where
 * the kind does not use them (offset only for OFFSET). */
TEPTRIS_API teptris_status teptris_builder_put_datetime(teptris_builder *b,
                                        const char *key, size_t key_len,
                                        teptris_kind kind,
                                        const teptris_datetime *dt);

TEPTRIS_API teptris_status teptris_builder_open_table(teptris_builder *b,
                                        const char *key, size_t key_len);
TEPTRIS_API teptris_status teptris_builder_open_array(teptris_builder *b,
                                        const char *key, size_t key_len);
/* like open_array but pinned inline ([..]) — for callers whose
 * lookahead knows the array is not a homogeneous table array */
TEPTRIS_API teptris_status teptris_builder_open_inline_array(teptris_builder *b,
                                        const char *key, size_t key_len);
TEPTRIS_API teptris_status teptris_builder_close(teptris_builder *b);
TEPTRIS_API teptris_status teptris_builder_finish(teptris_builder *b,
                                        teptris_document **out);

#ifdef __cplusplus
}
#endif

#endif /* TEPTRIS_TEPTRIS_H */
