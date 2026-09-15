#include "teptris/dom/dom.h"

#include <string.h>

#include "teptris/memory/arena.h"

uint64_t teptris_dom_key_hash(const char *key, size_t key_len)
{
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < key_len; i++) {
        h ^= (unsigned char)key[i];
        h *= 1099511628211ULL;
    }
    return h;
}

teptris_node *teptris_dom_new_node(teptris_document *doc, teptris_kind kind)
{
    teptris_node *n = teptris_arena_fast_alloc(&doc->arena,
                                               sizeof(teptris_node));
    if (n == NULL) {
        return NULL;
    }
    memset(n, 0, sizeof(*n));
    n->kind = (uint8_t)kind;
    return n;
}

teptris_node *teptris_dom_new_table(teptris_document *doc, uint8_t flags)
{
    teptris_node *n = teptris_dom_new_node(doc, TEPTRIS_TABLE);
    if (n != NULL) {
        n->flags = flags;
    }
    return n;
}

teptris_node *teptris_dom_new_array(teptris_document *doc, uint8_t flags)
{
    teptris_node *n = teptris_dom_new_node(doc, TEPTRIS_ARRAY);
    if (n != NULL) {
        n->flags = flags;
    }
    return n;
}

static void index_insert(teptris_node *t, uint32_t entry_idx, uint64_t h)
{
    size_t mask = t->as.table.idx_cap - 1;
    size_t slot = (size_t)h & mask;
    while (t->as.table.index[slot] != 0) {
        slot = (slot + 1) & mask;
    }
    t->as.table.index[slot] = entry_idx + 1;
}

static teptris_status index_rebuild(teptris_document *doc, teptris_node *t,
                                    size_t new_cap)
{
    uint32_t *idx = teptris_arena_alloc(&doc->arena, new_cap * sizeof(uint32_t));
    if (idx == NULL) {
        return TEPTRIS_ERR_ALLOC;
    }
    memset(idx, 0, new_cap * sizeof(uint32_t));
    t->as.table.index = idx;
    t->as.table.idx_cap = new_cap;
    for (size_t i = 0; i < t->as.table.len; i++) {
        index_insert(t, (uint32_t)i, t->as.table.entries[i].hash);
    }
    return TEPTRIS_OK;
}

teptris_entry *teptris_dom_table_find_h(const teptris_node *t, const char *key,
                                        size_t key_len, uint64_t *hash_out)
{
    if (t == NULL || t->kind != TEPTRIS_TABLE || t->as.table.idx_cap == 0) {
        *hash_out = teptris_dom_key_hash(key, key_len);
        return NULL;
    }
    uint64_t h = teptris_dom_key_hash(key, key_len);
    *hash_out = h;
    size_t mask = t->as.table.idx_cap - 1;
    size_t slot = (size_t)h & mask;
    for (;;) {
        uint32_t e = t->as.table.index[slot];
        if (e == 0) {
            return NULL;
        }
        teptris_entry *entry = &t->as.table.entries[e - 1];
        if (entry->key.len == key_len &&
            memcmp(entry->key.ptr, key, key_len) == 0) {
            return entry;
        }
        slot = (slot + 1) & mask;
    }
}

teptris_entry *teptris_dom_table_find(const teptris_node *t, const char *key,
                                      size_t key_len)
{
    uint64_t h;
    return teptris_dom_table_find_h(t, key, key_len, &h);
}

teptris_status teptris_dom_table_insert(teptris_document *doc, teptris_node *t,
                                        teptris_view key, teptris_node *value)
{
    return teptris_dom_table_insert_h(doc, t, key,
                                      teptris_dom_key_hash(key.ptr, key.len),
                                      value);
}

teptris_status teptris_dom_table_insert_h(teptris_document *doc, teptris_node *t,
                                         teptris_view key, uint64_t hash,
                                         teptris_node *value)
{
    teptris_arena *a = &doc->arena;

    if (t->as.table.len == t->as.table.cap) {
        size_t ncap = t->as.table.cap ? t->as.table.cap * 2 : 8;
        teptris_entry *ne =
            teptris_arena_try_grow(a, t->as.table.entries,
                                   t->as.table.cap * sizeof(teptris_entry),
                                   ncap * sizeof(teptris_entry));
        if (ne == NULL) {
            ne = teptris_arena_alloc(a, ncap * sizeof(teptris_entry));
            if (ne == NULL) {
                return TEPTRIS_ERR_ALLOC;
            }
            memcpy(ne, t->as.table.entries,
                   t->as.table.len * sizeof(teptris_entry));
            t->as.table.entries = ne;
        }
        t->as.table.cap = ncap;
    }

    if ((t->as.table.len + 1) * 10 >= t->as.table.idx_cap * 7) {
        size_t ncap = t->as.table.idx_cap ? t->as.table.idx_cap * 2 : 16;
        teptris_status st = index_rebuild(doc, t, ncap);
        if (st != TEPTRIS_OK) {
            return st;
        }
    }

    t->as.table.entries[t->as.table.len].key = key;
    t->as.table.entries[t->as.table.len].value = value;
    t->as.table.entries[t->as.table.len].hash = hash;
    index_insert(t, (uint32_t)t->as.table.len, hash);
    t->as.table.len++;
    return TEPTRIS_OK;
}

teptris_entry *teptris_dom_table_find_probe(const teptris_node *t,
                                            uint64_t hash, const char *key,
                                            size_t key_len)
{
    if (t == NULL || t->kind != TEPTRIS_TABLE || t->as.table.idx_cap == 0) {
        return NULL; /* empty: no duplicate possible */
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
    return NULL;
}

teptris_status teptris_dom_array_push(teptris_document *doc, teptris_node *a,
                                      teptris_node *value)
{
    teptris_arena *ar = &doc->arena;

    if (a->as.array.len == a->as.array.cap) {
        size_t ncap = a->as.array.cap ? a->as.array.cap * 2 : 8;
        teptris_node **ni =
            teptris_arena_try_grow(ar, a->as.array.items,
                                   a->as.array.cap * sizeof(teptris_node *),
                                   ncap * sizeof(teptris_node *));
        if (ni == NULL) {
            ni = teptris_arena_alloc(ar, ncap * sizeof(teptris_node *));
            if (ni == NULL) {
                return TEPTRIS_ERR_ALLOC;
            }
            memcpy(ni, a->as.array.items,
                   a->as.array.len * sizeof(teptris_node *));
            a->as.array.items = ni;
        }
        a->as.array.cap = ncap;
    }
    a->as.array.items[a->as.array.len++] = value;
    return TEPTRIS_OK;
}
