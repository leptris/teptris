#include <cstdint>

#include "teptris/memory/arena.h"
#include "teptris/teptris.h"

#include "util.hpp"

TEST(Arena, AllocAlignsAndDistincts)
{
    teptris_arena a;
    teptris_arena_init(&a);
    void *p1 = teptris_arena_alloc(&a, 1);
    void *p2 = teptris_arena_alloc(&a, 24);
    ASSERT_NE(p1, nullptr);
    ASSERT_NE(p2, nullptr);
    EXPECT_NE(p1, p2);
    EXPECT_EQ((uintptr_t)p1 % 16, 0u);
    EXPECT_EQ((uintptr_t)p2 % 16, 0u);
    teptris_arena_destroy(&a);
}

TEST(Arena, TryGrowTailOnly)
{
    teptris_arena a;
    teptris_arena_init(&a);
    char *p1 = (char *)teptris_arena_alloc(&a, 8);
    char *p2 = (char *)teptris_arena_alloc(&a, 8);
    /* p1 is not the tail: growth must fail */
    EXPECT_EQ(teptris_arena_try_grow(&a, p1, 8, 16), nullptr);
    /* p2 is the tail: growth in place */
    EXPECT_EQ(teptris_arena_try_grow(&a, p2, 8, 16), (void *)p2);
    /* non-growth shrink is rejected */
    EXPECT_EQ(teptris_arena_try_grow(&a, p2, 16, 8), nullptr);
    teptris_arena_destroy(&a);
}

TEST(Arena, LargeAllocationGetsOwnBlock)
{
    teptris_arena a;
    teptris_arena_init(&a);
    void *big = teptris_arena_alloc(&a, 10u * 1024u * 1024u);
    ASSERT_NE(big, nullptr);
    memset(big, 0xAB, 10u * 1024u * 1024u);
    void *small = teptris_arena_alloc(&a, 4);
    EXPECT_NE(small, nullptr);
    teptris_arena_destroy(&a);
}

TEST(Api, VersionAndStatusStrings)
{
    EXPECT_STREQ(teptris_version_string(), "0.1.0");
    EXPECT_STREQ(teptris_status_string(TEPTRIS_OK), "ok");
    EXPECT_STREQ(teptris_status_string(TEPTRIS_ERR_SYNTAX), "syntax error");
}

TEST(Api, EmptyDocumentIsEmptyTable)
{
    teptris_document *doc = parse_ok("");
    ASSERT_NE(doc, nullptr);
    DocGuard g(doc);
    const teptris_node *root = teptris_document_root(doc);
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(teptris_node_kind(root), TEPTRIS_TABLE);
    EXPECT_EQ(teptris_node_table_length(root), 0u);
    EXPECT_EQ(emit_json(doc), "{}");
}

TEST(Api, NullGuards)
{
    teptris_document *doc = nullptr;
    EXPECT_EQ(teptris_parse("a=1", 3, nullptr, &doc), TEPTRIS_OK);
    DocGuard g(doc);
    EXPECT_EQ(teptris_node_table_get(nullptr, "a", 1), nullptr);
    teptris_view v;
    const teptris_node *root = teptris_document_root(doc);
    const teptris_node *a = get(root, "a");
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(teptris_node_integer(get(root, "nope"), nullptr), TEPTRIS_ERR_ARG);
    EXPECT_EQ(teptris_node_string(a, &v), TEPTRIS_ERR_ARG); /* int is not string */
    teptris_document_free(nullptr); /* no crash */
}
