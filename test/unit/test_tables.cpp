#include "teptris/teptris.h"

#include "util.hpp"

TEST(Tables, HeadersNestAndKeepOrder)
{
    teptris_document *doc = parse_ok(
        "[a]\nx = 1\n[a.b]\ny = 2\n[a.b.c]\nz = 3\n[t]\nq = 9\n");
    ASSERT_NE(doc, nullptr);
    DocGuard g(doc);
    const teptris_node *root = teptris_document_root(doc);
    ASSERT_EQ(teptris_node_table_length(root), 2u);
    teptris_view k;
    EXPECT_EQ(teptris_node_table_at(root, 0, &k), get(root, "a"));
    EXPECT_EQ(str(k), "a");
    EXPECT_EQ(teptris_node_table_at(root, 1, &k), get(root, "t"));
    EXPECT_EQ(str(k), "t");

    const teptris_node *a = get(root, "a");
    int64_t x = 0;
    teptris_node_integer(get(a, "x"), &x);
    EXPECT_EQ(x, 1);
    EXPECT_NE(get(a, "b"), nullptr);
    EXPECT_NE(get(get(a, "b"), "c"), nullptr);
    int64_t z = 0;
    teptris_node_integer(get(get(get(a, "b"), "c"), "z"), &z);
    EXPECT_EQ(z, 3);
}

TEST(Tables, ImplicitThenExplicit)
{
    teptris_document *doc = parse_ok("[a.b]\nx = 1\n[a]\ny = 2\n");
    ASSERT_NE(doc, nullptr);
    DocGuard g(doc);
    const teptris_node *root = teptris_document_root(doc);
    EXPECT_NE(get(get(root, "a"), "b"), nullptr);
    int64_t y = 0;
    teptris_node_integer(get(get(root, "a"), "y"), &y);
    EXPECT_EQ(y, 2);
}

TEST(Tables, DottedKeys)
{
    teptris_document *doc = parse_ok("a.b.c = 1\na.b.d = 2\n");
    ASSERT_NE(doc, nullptr);
    DocGuard g(doc);
    const teptris_node *root = teptris_document_root(doc);
    const teptris_node *ab = get(get(root, "a"), "b");
    ASSERT_NE(ab, nullptr);
    int64_t v = 0;
    teptris_node_integer(get(ab, "c"), &v);
    EXPECT_EQ(v, 1);
    teptris_node_integer(get(ab, "d"), &v);
    EXPECT_EQ(v, 2);
}

TEST(Tables, ArrayOfTables)
{
    teptris_document *doc = parse_ok(
        "[[fruit]]\nname = \"apple\"\n[fruit.physical]\ncolor = \"red\"\n"
        "[[fruit]]\nname = \"banana\"\n[fruit.physical]\ncolor = \"yellow\"\n");
    ASSERT_NE(doc, nullptr);
    DocGuard g(doc);
    const teptris_node *root = teptris_document_root(doc);
    const teptris_node *fruit = get(root, "fruit");
    ASSERT_EQ(teptris_node_kind(fruit), TEPTRIS_ARRAY);
    ASSERT_EQ(teptris_node_array_length(fruit), 2u);

    teptris_view name;
    const teptris_node *a0 = teptris_node_array_at(fruit, 0);
    teptris_node_string(get(a0, "name"), &name);
    EXPECT_EQ(str(name), "apple");
    /* [fruit.physical] attaches to the LAST [[fruit]] member */
    const teptris_node *a1 = teptris_node_array_at(fruit, 1);
    teptris_view color;
    teptris_node_string(get(get(a1, "physical"), "color"), &color);
    EXPECT_EQ(str(color), "yellow");
    /* each [fruit.physical] attached to the then-current last member */
    teptris_view color0;
    teptris_node_string(get(get(a0, "physical"), "color"), &color0);
    EXPECT_EQ(str(color0), "red");
}

TEST(Tables, InlineTables)
{
    teptris_document *doc = parse_ok(
        "point = {x = 1, y = 2}\nnested = {a = {b = 3}}\nd = {e.f = 4}\nempty = {}\n");
    ASSERT_NE(doc, nullptr);
    DocGuard g(doc);
    const teptris_node *root = teptris_document_root(doc);
    int64_t v = 0;
    teptris_node_integer(get(get(root, "point"), "x"), &v);
    EXPECT_EQ(v, 1);
    teptris_node_integer(get(get(get(root, "nested"), "a"), "b"), &v);
    EXPECT_EQ(v, 3);
    teptris_node_integer(get(get(get(root, "d"), "e"), "f"), &v);
    EXPECT_EQ(v, 4);
    EXPECT_EQ(teptris_node_table_length(get(root, "empty")), 0u);
}

TEST(Tables, ArraysOfValues)
{
    teptris_document *doc = parse_ok(
        "a = [1, 2, 3]\n"
        "b = [\n  \"red\",\n  \"yellow\", # comment\n]\n"
        "mixed = [0.1, 1979-05-27, [1], {x = 1}]\n");
    ASSERT_NE(doc, nullptr);
    DocGuard g(doc);
    const teptris_node *root = teptris_document_root(doc);
    EXPECT_EQ(teptris_node_array_length(get(root, "a")), 3u);
    EXPECT_EQ(teptris_node_array_length(get(root, "b")), 2u);
    const teptris_node *mixed = get(root, "mixed");
    EXPECT_EQ(teptris_node_kind(teptris_node_array_at(mixed, 0)), TEPTRIS_FLOAT);
    EXPECT_EQ(teptris_node_kind(teptris_node_array_at(mixed, 1)),
              TEPTRIS_DATE_LOCAL);
    EXPECT_EQ(teptris_node_kind(teptris_node_array_at(mixed, 2)), TEPTRIS_ARRAY);
    EXPECT_EQ(teptris_node_kind(teptris_node_array_at(mixed, 3)), TEPTRIS_TABLE);
}

TEST(Tables, QuotedAndEmptyKeys)
{
    teptris_document *doc = parse_ok("\"127.0.0.1\" = 1\n'' = 2\n'lit key' = 3\n");
    ASSERT_NE(doc, nullptr);
    DocGuard g(doc);
    const teptris_node *root = teptris_document_root(doc);
    int64_t v = 0;
    teptris_node_integer(teptris_node_table_get(root, "127.0.0.1", 9), &v);
    EXPECT_EQ(v, 1);
    teptris_node_integer(teptris_node_table_get(root, "", 0), &v);
    EXPECT_EQ(v, 2);
    teptris_node_integer(teptris_node_table_get(root, "lit key", 7), &v);
    EXPECT_EQ(v, 3);
}

TEST(Tables, DefineSemanticsErrors)
{
    const char *bad[] = {
        "[a]\nx = 1\n[a]\ny = 2",              /* table redefined */
        "a = 1\na = 2",                        /* duplicate key */
        "[a]\nb = 1\n[a.b]",                   /* dotted leaf redefined as table */
        "a.b = 1\n[a]",                        /* dotted table redefined by header */
        "[a]\nb.c = 1\n[a.b]",                 /* dotted subtable redefined */
        "[a.b]\nx = 1\n[a]\nb.y = 2",          /* dotted key extends header table */
        "a = {x = 1}\n[a.y]",                  /* extend inline table */
        "a = {x = 1}\na.y = 2",                /* dotted into inline table */
        "a = [1, 2]\n[[a]]",                   /* plain array as aot */
        "[[a]]\nx = 1\n[a]",                   /* table over aot */
        "a = 1\n[a]",                          /* value redefined as table */
        "a = 1\n[[a]]",                        /* value redefined as aot */
        "a = {x = 1, x = 2}",                  /* dup key in inline */
        "a = {x = 1\ny = 2}",                  /* newline in inline */
        "a = [1, 2",                           /* unterminated array */
    };
    for (const char *in : bad) {
        teptris_document *doc = nullptr;
        teptris_status st = parse_status(in, &doc);
        EXPECT_TRUE(st == TEPTRIS_ERR_SYNTAX || st == TEPTRIS_ERR_SEMANTIC)
            << "input: " << in << " (got " << teptris_status_string(st) << ")";
        DocGuard g(doc);
    }
}

TEST(Tables, ValidDefineSemantics)
{
    const char *ok[] = {
        "[a]\nb.c = 1\nb.d = 2",               /* dotted siblings */
        "a.b = 1\na.c = 2",                    /* dotted siblings at root */
        "[a.b.c]\nx = 1\n[a.b]\ny = 2\n[a]\nz = 3", /* deep implicit then explicit */
        "[[a]]\nx = 1\n[[a]]\nx = 2\n[[a.b]]\ny = 3", /* aot nesting */
    };
    for (const char *in : ok) {
        teptris_document *doc = nullptr;
        teptris_status st = parse_status(in, &doc);
        EXPECT_EQ(st, TEPTRIS_OK) << "input: " << in;
        DocGuard g(doc);
    }
}
