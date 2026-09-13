#include "teptris/teptris.h"

#include "util.hpp"

TEST(Emit, SimpleGolden)
{
    teptris_document *doc = parse_ok("a = 1\nb = 'x'\n[t]\nc = 2\n");
    ASSERT_NE(doc, nullptr);
    DocGuard g(doc);
    EXPECT_EQ(emit_toml(doc), "a = 1\nb = 'x'\n[t]\nc = 2\n");
}

TEST(Emit, RootKeysBeforeHeaders)
{
    teptris_document *doc = parse_ok("[t]\nc = 2\n");
    ASSERT_NE(doc, nullptr);
    DocGuard g(doc);
    /* a root key added after a header would be invalid input; adding one
     * before is the only order, so re-emission is stable */
    teptris_document *doc2 = parse_ok("root = true\n[t]\nc = 2\n");
    ASSERT_NE(doc2, nullptr);
    DocGuard g2(doc2);
    EXPECT_EQ(emit_toml(doc2), "root = true\n[t]\nc = 2\n");
}

TEST(Emit, ArrayOfTablesGolden)
{
    teptris_document *doc = parse_ok("[[a]]\nx = 1\n[[a]]\nx = 2\n");
    ASSERT_NE(doc, nullptr);
    DocGuard g(doc);
    EXPECT_EQ(emit_toml(doc), "[[a]]\nx = 1\n[[a]]\nx = 2\n");
}

TEST(Emit, NestedSubtableOfAotGolden)
{
    teptris_document *doc = parse_ok("[[a]]\nx = 1\n[a.b]\ny = 2\n");
    ASSERT_NE(doc, nullptr);
    DocGuard g(doc);
    EXPECT_EQ(emit_toml(doc), "[[a]]\nx = 1\n[a.b]\ny = 2\n");
}

TEST(Emit, InlineAndArraysGolden)
{
    teptris_document *doc = parse_ok("a = [1, 2]\np = {x = 1}\n");
    ASSERT_NE(doc, nullptr);
    DocGuard g(doc);
    EXPECT_EQ(emit_toml(doc), "a = [1, 2]\np = {x = 1}\n");
}

TEST(Emit, KeyQuoting)
{
    teptris_document *doc = parse_ok("'a.b' = 1\n\"it's\" = 2\n\"a\\tb\" = 3\n");
    ASSERT_NE(doc, nullptr);
    DocGuard g(doc);
    EXPECT_EQ(emit_toml(doc), "'a.b' = 1\n\"it's\" = 2\n'a\tb' = 3\n");
}

TEST(Emit, LiteralWithSingleQuoteNeedsBasic)
{
    teptris_document *doc = parse_ok("s = \"it's\"\n");
    ASSERT_NE(doc, nullptr);
    DocGuard g(doc);
    /* literal form cannot hold a single quote → basic */
    EXPECT_EQ(emit_toml(doc), "s = \"it's\"\n");
}

TEST(Emit, StringsNeedingBasicForm)
{
    teptris_document *doc = parse_ok("s = \"line1\\nline2 'quoted'\"\n");
    ASSERT_NE(doc, nullptr);
    DocGuard g(doc);
    /* contains ' → basic-escaped */
    EXPECT_EQ(emit_toml(doc), "s = \"line1\\nline2 'quoted'\"\n");
}

TEST(Emit, DateTimeCanonicalization)
{
    teptris_document *doc = parse_ok("a = 1979-05-27T07:32:00+00:00\n");
    ASSERT_NE(doc, nullptr);
    DocGuard g(doc);
    EXPECT_EQ(emit_toml(doc), "a = 1979-05-27T07:32:00Z\n");

    teptris_document *doc2 = parse_ok("a = 1979-05-27T07:32:00+07:30\n");
    ASSERT_NE(doc2, nullptr);
    DocGuard g2(doc2);
    EXPECT_EQ(emit_toml(doc2), "a = 1979-05-27T07:32:00+07:30\n");
}

TEST(Emit, IntegerRadixNormalization)
{
    teptris_document *doc = parse_ok("h = 0xFF\no = 0o10\nb = 0b101\n");
    ASSERT_NE(doc, nullptr);
    DocGuard g(doc);
    EXPECT_EQ(emit_toml(doc), "h = 255\no = 8\nb = 5\n");
}

TEST(Emit, FloatNotation)
{
    struct {
        const char *in;
        const char *out;
    } cases[] = {
        {"a = 1.0", "a = 1.0\n"},
        {"a = 0.5", "a = 0.5\n"},
        {"a = 3.14", "a = 3.14\n"},
        {"a = 100000.0", "a = 100000.0\n"},
        {"a = 0.0025", "a = 0.0025\n"},
        {"a = 1e20", "a = 1e20\n"},
        {"a = 1e-20", "a = 1e-20\n"},
        {"a = 5e-324", "a = 5e-324\n"},
        {"a = -2.5e-3", "a = -0.0025\n"},
        {"a = 1e308", "a = 1e308\n"},
        {"a = 224617.445991228", "a = 224617.445991228\n"},
    };
    for (auto &c : cases) {
        teptris_document *doc = parse_ok(c.in);
        ASSERT_NE(doc, nullptr);
        DocGuard g(doc);
        EXPECT_EQ(emit_toml(doc), c.out) << "input: " << c.in;
        /* every emitted float must roundtrip bit-exactly */
        std::string re = emit_toml(doc);
        teptris_document *d2 = nullptr;
        ASSERT_EQ(parse_status(re, &d2), TEPTRIS_OK);
        DocGuard g2(d2);
        double v1 = 0, v2 = 0;
        teptris_node_float(get(teptris_document_root(doc), "a"), &v1);
        teptris_node_float(get(teptris_document_root(d2), "a"), &v2);
        EXPECT_EQ(memcmp(&v1, &v2, sizeof(double)), 0) << "input: " << c.in;
    }
}

TEST(Emit, RoundtripProperty)
{
    const char *corpus[] = {
        "a = 1",
        "a = [1, 2, 3]\nb = {x = 1, y = [true, false]}",
        "s = \"esc \\\" \\\\ \\u00e9 \\n\"\nl = 'literal'",
        "[a.b.c]\nx = 0.5\n[a]\ny = inf",
        "[[t]]\nx = 1\n[t.s]\ny = 2\n[[t]]\nx = 3",
        "d = 1979-05-27T07:32:00.5Z\ndt = 07:32:00\ndd = 1979-05-27",
        "'weird key' = 1\n\"k\\ty\" = 2",
        "neg = -9223372036854775808\npos = 9223372036854775807\nf = -0.0",
        "nested = [{a = 1}, {b = [{c = 2}]}]",
        "empty_t = {}\nempty_a = []",
    };
    for (const char *in : corpus) {
        teptris_document *d1 = parse_ok(in);
        ASSERT_NE(d1, nullptr);
        DocGuard g1(d1);
        std::string json1 = emit_json(d1);
        std::string toml1 = emit_toml(d1);

        teptris_document *d2 = nullptr;
        ASSERT_EQ(parse_status(toml1, &d2), TEPTRIS_OK) << "reparse of: " << toml1;
        DocGuard g2(d2);
        EXPECT_TRUE(teptris_test::json_semantically_equal(emit_json(d2), json1))
            << "roundtrip json for: " << in << "\n  got: " << emit_json(d2)
            << "\n  want: " << json1;

        std::string toml2 = emit_toml(d2);
        EXPECT_EQ(toml2, toml1) << "byte stability for: " << in;
    }
}

TEST(Emit, EmptyDocumentEmitsEmpty)
{
    teptris_document *doc = parse_ok("");
    ASSERT_NE(doc, nullptr);
    DocGuard g(doc);
    EXPECT_EQ(emit_toml(doc), "");
    EXPECT_EQ(emit_json(doc), "{}");
}

TEST(Emit, EmittedTomlAlwaysReparses)
{
    /* Every string the parser accepts must re-parse after emission;
     * exercised over the corpus above plus tricky scalar forms. */
    const char *corpus[] = {
        "u = \"\\U0001F600 \\u0000\"",
        "ml = '''\nmulti\nline'''",
        "mbs = \"\"\"\nfold \\\n  here\"\"\"",
        "x = 1e308\ny = 5e-324\nz = -1.7976931348623157e308",
        "t = 1979-05-27T07:32:00.999999999-00:00",
    };
    for (const char *in : corpus) {
        teptris_document *d1 = parse_ok(in);
        ASSERT_NE(d1, nullptr);
        DocGuard g1(d1);
        std::string toml1 = emit_toml(d1);
        teptris_document *d2 = nullptr;
        ASSERT_EQ(parse_status(toml1, &d2), TEPTRIS_OK) << "reparse of: " << toml1;
        DocGuard g2(d2);
        EXPECT_TRUE(teptris_test::json_semantically_equal(emit_json(d2),
                                                          emit_json(d1)))
            << in;
    }
}
