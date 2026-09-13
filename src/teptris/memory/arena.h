#ifndef TEPTRIS_ARENA_H
#define TEPTRIS_ARENA_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Per-document bump arena: every allocation reachable from a document is
 * carved from here and released in one pass at document free. */
typedef struct teptris_arena_block {
    struct teptris_arena_block *next;
    size_t used;
    size_t cap;
    /* data follows, 16-byte aligned */
} teptris_arena_block;

typedef struct teptris_arena {
    teptris_arena_block *head; /* most recently allocated block */
} teptris_arena;

void teptris_arena_init(teptris_arena *arena);
void *teptris_arena_alloc(teptris_arena *arena, size_t size);
/* Grow the most recent arena allocation in place when possible.
 * Returns the (possibly moved) pointer, or NULL if `last` is not the
 * tail — caller then allocates+copies itself. */
void *teptris_arena_try_grow(teptris_arena *arena, void *last, size_t old_size,
                             size_t new_size);
void teptris_arena_destroy(teptris_arena *arena);

#ifdef __cplusplus
}
#endif

#endif /* TEPTRIS_ARENA_H */
