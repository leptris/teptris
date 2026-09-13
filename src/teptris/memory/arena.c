#include "teptris/memory/arena.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define TEPTRIS_ARENA_BLOCK_SIZE (64u * 1024u)
#define TEPTRIS_ARENA_ALIGN 16u

static size_t round_up(size_t n, size_t align)
{
    return (n + align - 1) & ~(align - 1);
}

/* Block header size rounded so the data area starts 16-byte aligned. */
static size_t block_data_offset(void)
{
    return round_up(sizeof(teptris_arena_block), TEPTRIS_ARENA_ALIGN);
}

void teptris_arena_init(teptris_arena *arena)
{
    arena->head = NULL;
}

static teptris_arena_block *block_new(size_t cap)
{
    size_t data_off = block_data_offset();
    teptris_arena_block *b = malloc(data_off + cap);
    if (b == NULL) {
        return NULL;
    }
    b->next = NULL;
    b->used = 0;
    b->cap = cap;
    return b;
}

static char *block_data(teptris_arena_block *b)
{
    return (char *)b + block_data_offset();
}

void *teptris_arena_alloc(teptris_arena *arena, size_t size)
{
    size_t need = round_up(size, TEPTRIS_ARENA_ALIGN);
    teptris_arena_block *b = arena->head;

    if (b != NULL && b->used + need <= b->cap) {
        void *p = block_data(b) + b->used;
        b->used += need;
        return p;
    }

    size_t cap = TEPTRIS_ARENA_BLOCK_SIZE;
    if (need > cap) {
        cap = need;
    }
    /* Geometric growth: fewer blocks means fewer malloc/free (and free's
     * madvise) calls on large documents. */
    if (arena->head != NULL && arena->head->cap * 2 > cap) {
        cap = arena->head->cap * 2;
    }
    teptris_arena_block *nb = block_new(cap);
    if (nb == NULL) {
        return NULL;
    }
    nb->next = arena->head;
    arena->head = nb;
    void *p = block_data(nb) + nb->used;
    nb->used += need;
    return p;
}

void *teptris_arena_try_grow(teptris_arena *arena, void *last, size_t old_size,
                             size_t new_size)
{
    teptris_arena_block *b = arena->head;
    if (b == NULL || last == NULL) {
        return NULL;
    }
    size_t old_rounded = round_up(old_size, TEPTRIS_ARENA_ALIGN);
    size_t new_rounded = round_up(new_size, TEPTRIS_ARENA_ALIGN);
    if (new_size < old_size) {
        return NULL;
    }
    if ((char *)last + old_rounded == block_data(b) + b->used &&
        b->used + (new_rounded - old_rounded) <= b->cap) {
        b->used += new_rounded - old_rounded;
        return last;
    }
    return NULL;
}

void teptris_arena_destroy(teptris_arena *arena)
{
    teptris_arena_block *b = arena->head;
    while (b != NULL) {
        teptris_arena_block *next = b->next;
        free(b);
        b = next;
    }
    arena->head = NULL;
}
