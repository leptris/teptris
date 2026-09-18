#ifndef TEPTRIS_DOM_H
#define TEPTRIS_DOM_H

#include <stdint.h>
#include <string.h>

#include "teptris/memory/arena.h"
#include "teptris/teptris.h"

/* Table define-semantics flags (parser-only state, invisible to readers). */
#define TBL_EXPLICIT 0x01u /* named by a [table] header (or root) */
#define TBL_IMPLICIT 0x02u /* intermediate of a header path */
#define TBL_DOTTED 0x04u   /* intermediate of a dotted key */
#define TBL_INLINE 0x08u   /* inline table value: immutable */
#define ARR_AOT 0x10u      /* array created by [[...]] headers */

typedef struct teptris_entry {
    teptris_view key;    /* view into the parse input */
    teptris_node *value;
    uint64_t hash;       /* cached: index rebuilds never re-hash keys */
} teptris_entry;

struct teptris_node {
    uint8_t kind;
    uint8_t flags;
    union {
        teptris_view str; /* NUL-terminated arena copy */
        int64_t i;
        double f;
        bool b;
        teptris_datetime dt;
        struct {
            teptris_entry *entries; /* insertion order */
            size_t len, cap;
            uint32_t *index; /* entry_idx+1; 0 = empty; linear probe */
            size_t idx_cap;
        } table;
        struct {
            struct teptris_node **items;
            size_t len, cap;
        } array;
    } as;
};

/* _Static_assert is C11; g++ (unlike clang++) has no such extension */
#if defined(__cplusplus)
static_assert(sizeof(teptris_node) <= 64, "teptris_node must stay compact");
#else
_Static_assert(sizeof(teptris_node) <= 64, "teptris_node must stay compact");
#endif

struct teptris_document {
    teptris_arena arena;
    teptris_node *root;
    teptris_error err;
    char err_msg[192];
    uint32_t max_depth;
};

/* Constructors (arena-owned, zeroed). Return NULL on allocation failure. */
teptris_node *teptris_dom_new_node(teptris_document *doc, teptris_kind kind);
teptris_node *teptris_dom_new_table(teptris_document *doc, uint8_t flags);
teptris_node *teptris_dom_new_array(teptris_document *doc, uint8_t flags);

/* Find by key (hash index); NULL when absent. */
teptris_entry *teptris_dom_table_find(const teptris_node *table, const char *key,
                                      size_t key_len);

/* Single-hash variants: find also reports the key hash so a following
 * insert does not rehash (the common insert-dotted/resolve flow). */
uint64_t teptris_dom_key_hash(const char *key, size_t key_len);
teptris_entry *teptris_dom_table_find_h(const teptris_node *table,
                                        const char *key, size_t key_len,
                                        uint64_t *hash_out);

/* Probe with a PRECOMPUTED hash that also reports the empty index
 * slot the walk stopped at, for reuse by insert_slot — one walk for
 * the whole miss->insert sequence (header and keyval paths).
 * *slot_out is SIZE_MAX when the slot cannot be reused (empty table,
 * non-table). Must be header-inline: cross-TU, the call cost ate the
 * measured gain last time. */
static inline teptris_entry *
teptris_dom_table_probe_slot(const teptris_node *t, uint64_t hash,
                             const char *key, size_t key_len, size_t *slot_out)
{
    *slot_out = SIZE_MAX;
    if (t == NULL || t->kind != TEPTRIS_TABLE || t->as.table.idx_cap == 0) {
        return NULL;
    }
    size_t mask = t->as.table.idx_cap - 1;
    size_t slot = (size_t)hash & mask;
    while (t->as.table.index[slot] != 0) {
        uint32_t ei = t->as.table.index[slot] - 1;
        const teptris_entry *e = &t->as.table.entries[ei];
        if (e->hash == hash && e->key.len == key_len &&
            memcmp(e->key.ptr, key, key_len) == 0) {
            return (teptris_entry *)e;
        }
        slot = (slot + 1) & mask;
    }
    *slot_out = slot;
    return NULL;
}

/* Insert; the key must not already exist (caller checks). */
teptris_status teptris_dom_table_insert(teptris_document *doc, teptris_node *table,
                                        teptris_view key, teptris_node *value);
teptris_status teptris_dom_table_insert_h(teptris_document *doc,
                                          teptris_node *table, teptris_view key,
                                          uint64_t hash, teptris_node *value);
/* insert_h with the empty index slot pre-resolved by probe_slot
 * (SIZE_MAX = unresolved). A load-factor rebuild invalidates the
 * slot internally and falls back to a fresh probe. */
teptris_status teptris_dom_table_insert_slot(teptris_document *doc,
                                             teptris_node *table,
                                             teptris_view key, uint64_t hash,
                                             teptris_node *value, size_t slot);

teptris_status teptris_dom_array_push(teptris_document *doc, teptris_node *array,
                                      teptris_node *value);

#endif /* TEPTRIS_DOM_H */
