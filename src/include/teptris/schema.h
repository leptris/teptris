#ifndef TEPTRIS_SCHEMA_H
#define TEPTRIS_SCHEMA_H

/* JSON Schema validation for TOML documents (draft 2020-12, practical
 * subset). Compile a schema once, validate many documents. One
 * implementation, every runtime.
 *
 * ------------------------------------------------------------- policies --
 * TOML and JSON differ in exactly four places; each difference has a
 * documented policy:
 *
 * 1. null. TOML has no null. A schema branch requiring null (a bare
 *    `"type": "null"`, or an enum/const entry of null) matches no TOML
 *    value. Optionality stays with `required` + absence, so the common
 *    JSON idiom `"type": ["string", "null"]` behaves as an optional
 *    string: absent passes, a present string passes, any other present
 *    value fails. No special-casing: null-typed branches simply never
 *    match.
 *
 * 2. integer. TOML's integer is a DISTINCT type from float (`1` and
 *    `1.0` differ). `"type": "integer"` matches only TOML integers - a
 *    float never validates as integer, even with zero fractional part.
 *    `"type": "number"` matches both.
 *
 * 3. datetimes. TOML's four datetime shapes validate as `type:
 *    "string"` (their JSON view), and the `format` keywords
 *    "date-time", "date" and "time" check the datetime SHAPE: a TOML
 *    datetime value of the matching shape passes, and so does a string
 *    whose content parses as that shape. Other formats are annotations
 *    (checked by the host, if at all) - draft 2020-12 makes format
 *    annotation-only by default.
 *
 * 4. non-finite floats. `nan`, `inf`, `-inf` are TOML floats: they
 *    match `type: "number"`. nan compares false against everything,
 *    so it fails every range/multipleOf constraint; inf violates any
 *    `maximum`. No special-casing.
 *
 * ---------------------------------------------------------- semantics --
 * - `$ref` resolves local JSON Pointers ("#/...", "#", plain-name
 *   "#anchor" via `$anchor`), including recursive definitions. Remote
 *   references (absolute URIs) are REJECTED at compile: a parser
 *   library never touches the network.
 * - Keywords that would silently change validation results when
 *   ignored (`unevaluatedProperties`, `unevaluatedItems`,
 *   `$dynamicRef`, `$dynamicAnchor`, `$recursiveRef`, `$recursiveAnchor`,
 *   backreferences / lookahead / lookbehind in patterns) are REJECTED
 *   at compile with a diagnostic. Unknown keywords are ignored (they
 *   are extensions per the spec).
 * - Patterns run on a linear-time NFA (Thompson construction): immune
 *   to ReDoS by construction. Practical ECMA subset: literals, '.',
 *   classes, groups, alternation, greedy quantifiers, anchors, \b,
 *   \d \w \s (and negations), \xNN, \uNNNN. Lazy quantifiers are
 *   accepted (boolean match semantics are identical).
 * - Validation collects every error it finds, up to 256, and reports
 *   them as a JSON array of {"path", "schemaPath", "message"}; an
 *   empty array means the document is valid. Paths are dotted TOML
 *   paths with array indices ("items.0.name"); keys containing a dot
 *   are quoted ("\"my.key\"").
 *
 * Memory: every buffer handed out (*buf, *diag_buf) is malloc'd; the
 * caller frees it with free(). */
#include <stddef.h>

#include "teptris/teptris.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct teptris_schema teptris_schema;

/* Compile a schema from JSON text. On failure *out stays unset and
 * *diag_buf (if non-NULL) receives a malloc'd JSON diagnostic
 * {"error": "...", "path": "..."} locating the problem; free it with
 * teptris_diag_free. Either out or diag_buf may be NULL if the caller
 * does not want it. */
/* compile with a base directory: relative $refs and $ids in the
 * schema resolve against files under it (local files only - the
 * library never touches the network). compile() is compile_ex(NULL). */
TEPTRIS_API teptris_status teptris_schema_compile_ex(
    const char *json, size_t len, const char *base_dir, size_t base_len,
    struct teptris_schema **out, char **diag_buf, size_t *diag_len);

TEPTRIS_API teptris_status teptris_schema_compile(const char *json, size_t len,
                                                  teptris_schema **out,
                                                  char **diag_buf,
                                                  size_t *diag_len);

/* Free a compile diagnostic. */
TEPTRIS_API void teptris_diag_free(char *buf);

/* Validate a parsed document against the schema. *buf receives a
 * malloc'd JSON array of error objects (empty array = valid); free it
 * with free(). At most 256 errors are collected. */
TEPTRIS_API teptris_status teptris_schema_validate_doc(
    const teptris_schema *sch, const teptris_document *doc, char **buf,
    size_t *out_len);

/* Parse TOML text and validate it. A malformed document returns
 * TEPTRIS_ERR_SYNTAX (with *out unchanged - parse it separately with
 * teptris_parse to read the position). */
TEPTRIS_API teptris_status teptris_schema_validate(const teptris_schema *sch,
                                                   const char *toml, size_t len,
                                                   char **buf, size_t *out_len);

/* Free a compiled schema. Compiled schemas are independent of any
 * document; reuse across documents is the intended pattern. */
TEPTRIS_API void teptris_schema_free(teptris_schema *sch);

#ifdef __cplusplus
}
#endif

#endif /* TEPTRIS_SCHEMA_H */
