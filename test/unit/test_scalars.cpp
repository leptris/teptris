#include <cmath>
#include <cstdint>

#include "teptris/teptris.h"

#include "util.hpp"

namespace {

int64_t int_of(const std::string &toml, const char *key)
{
    teptris_document *doc = parse_ok(toml);
    if (doc == nullptr) {
        return 0;
    }
    DocGuard g(doc);
    int64_t v = 0;
    const teptris_node *n = get(teptris_document_root(doc), key);
    EXPECT_NE(n, nullptr);
    teptris_node_integer(n, &v);
    return v;
}

double float_of(const std::string &toml, const char *key)
{
    teptris_document *doc = parse_ok(toml);
    if (doc == nullptr) {
        return 0.0;
    }
    DocGuard g(doc);
    double v = 0;
    teptris_node_float(get(teptris_document_root(doc), key), &v);
    return v;
}

} // namespace

TEST(Integers, Forms)
{
    EXPECT_EQ(int_of("a = 1", "a"), 1);
    EXPECT_EQ(int_of("a = +99", "a"), 99);
    EXPECT_EQ(int_of("a = -17", "a"), -17);
    EXPECT_EQ(int_of("a = 0", "a"), 0);
    EXPECT_EQ(int_of("a = 1_000_000", "a"), 1000000);
    EXPECT_EQ(int_of("a = 0xDEAD_beef", "a"), 0xDEADBEEF);
    EXPECT_EQ(int_of("a = 0o755", "a"), 0755);
    EXPECT_EQ(int_of("a = 0b1101", "a"), 13);
    EXPECT_EQ(int_of("a = 9223372036854775807", "a"), INT64_MAX);
    EXPECT_EQ(int_of("a = -9223372036854775808", "a"), INT64_MIN);
}

TEST(Integers, Invalid)
{
    const char *bad[] = {
        "a = 01",           "a = 0x",        "a = +0x1",      "a = -0b1",
        "a = 1__0",         "a = _1",        "a = 1_",        "a = 0x_1",
        "a = 9223372036854775808",           "a = 0x1FFFFFFFFFFFFFFFF",
    };
    for (const char *in : bad) {
        teptris_document *doc = nullptr;
        teptris_status st = parse_status(in, &doc);
        EXPECT_EQ(st, TEPTRIS_ERR_SYNTAX) << "input: " << in;
        DocGuard g(doc);
    }
}

TEST(Floats, Forms)
{
    EXPECT_DOUBLE_EQ(float_of("a = 3.14", "a"), 3.14);
    EXPECT_DOUBLE_EQ(float_of("a = 1e6", "a"), 1e6);
    EXPECT_DOUBLE_EQ(float_of("a = -2.5E-3", "a"), -2.5e-3);
    EXPECT_DOUBLE_EQ(float_of("a = 6.626e-34", "a"), 6.626e-34);
    EXPECT_DOUBLE_EQ(float_of("a = 224_617.445_991_228", "a"), 224617.445991228);
    EXPECT_TRUE(std::isinf(float_of("a = inf", "a")));
    EXPECT_TRUE(std::isinf(float_of("a = -inf", "a")));
    EXPECT_TRUE(std::isnan(float_of("a = nan", "a")));
    EXPECT_DOUBLE_EQ(float_of("a = 5e+22", "a"), 5e22);
    /* >17 significant digits: ryu s2d rejects, strtod fallback engages */
    EXPECT_DOUBLE_EQ(float_of("a = 1.2345678901234567890123456789", "a"),
                     1.2345678901234567890123456789);
    EXPECT_DOUBLE_EQ(float_of("a = 5e-324", "a"), 5e-324);
}

TEST(Floats, JsonValue)
{
    struct {
        const char *in;
        const char *json;
    } cases[] = {
        {"a = 3.14", R"({"a":{"type":"float","value":"3.14"}})"},
        {"a = 1.0", R"({"a":{"type":"float","value":"1.0"}})"},
        {"a = inf", R"({"a":{"type":"float","value":"inf"}})"},
        {"a = -inf", R"({"a":{"type":"float","value":"-inf"}})"},
        {"a = nan", R"({"a":{"type":"float","value":"nan"}})"},
        {"a = 0.5", R"({"a":{"type":"float","value":"0.5"}})"},
    };
    for (auto &c : cases) {
        teptris_document *doc = parse_ok(c.in);
        ASSERT_NE(doc, nullptr);
        DocGuard g(doc);
        EXPECT_EQ(emit_json(doc), c.json) << "input: " << c.in;
    }
}

TEST(Floats, Invalid)
{
    const char *bad[] = {
        "a = .7", "a = 7.", "a = 3.e+20", "a = 01.5", "a = 1e", "a = 1e_2",
    };
    for (const char *in : bad) {
        teptris_document *doc = nullptr;
        teptris_status st = parse_status(in, &doc);
        EXPECT_EQ(st, TEPTRIS_ERR_SYNTAX) << "input: " << in;
        DocGuard g(doc);
    }
}

TEST(Strings, Escapes)
{
    teptris_document *doc = parse_ok("a = \"tab\\there\\nline \\\"q\\\" \\\\ \\u00e9\\U0001F600\"");
    ASSERT_NE(doc, nullptr);
    DocGuard g(doc);
    teptris_view v;
    ASSERT_EQ(teptris_node_string(get(teptris_document_root(doc), "a"), &v),
              TEPTRIS_OK);
    EXPECT_EQ(str(v), "tab\there\nline \"q\" \\ \xC3\xA9\xF0\x9F\x98\x80");
}

TEST(Strings, Literal)
{
    teptris_document *doc = parse_ok("a = 'C:\\path\\no\\escapes'");
    ASSERT_NE(doc, nullptr);
    DocGuard g(doc);
    teptris_view v;
    teptris_node_string(get(teptris_document_root(doc), "a"), &v);
    EXPECT_EQ(str(v), "C:\\path\\no\\escapes");
}

TEST(Strings, MultilineBasic)
{
    teptris_document *doc = parse_ok("a = \"\"\"\nline1\nline2 \\\n   continued\"\"\"");
    ASSERT_NE(doc, nullptr);
    DocGuard g(doc);
    teptris_view v;
    teptris_node_string(get(teptris_document_root(doc), "a"), &v);
    EXPECT_EQ(str(v), "line1\nline2 continued");
}

TEST(Strings, MultilineLiteralAndQuoteRuns)
{
    teptris_document *doc = parse_ok("a = '''\nit's \"fine\" 'ok' '''");
    ASSERT_NE(doc, nullptr);
    DocGuard g(doc);
    teptris_view v;
    teptris_node_string(get(teptris_document_root(doc), "a"), &v);
    EXPECT_EQ(str(v), "it's \"fine\" 'ok' ");
}

TEST(Strings, EmptyAndControl)
{
    teptris_document *doc = parse_ok("e = ''\nb = \"\"");
    ASSERT_NE(doc, nullptr);
    DocGuard g(doc);
    teptris_view v;
    teptris_node_string(get(teptris_document_root(doc), "e"), &v);
    EXPECT_EQ(v.len, 0u);
    teptris_node_string(get(teptris_document_root(doc), "b"), &v);
    EXPECT_EQ(v.len, 0u);

    teptris_document *bad = nullptr;
    EXPECT_EQ(parse_status(std::string("a = \"raw\tnow\x01ctrl\""), &bad),
              TEPTRIS_ERR_SYNTAX);
    DocGuard g2(bad);
}

TEST(Booleans, Exact)
{
    teptris_document *doc = parse_ok("t = true\nf = false");
    ASSERT_NE(doc, nullptr);
    DocGuard g(doc);
    bool b = false;
    teptris_node_boolean(get(teptris_document_root(doc), "t"), &b);
    EXPECT_TRUE(b);
    teptris_node_boolean(get(teptris_document_root(doc), "f"), &b);
    EXPECT_FALSE(b);

    teptris_document *bad = nullptr;
    EXPECT_EQ(parse_status("a = tru", &bad), TEPTRIS_ERR_SYNTAX);
    DocGuard g2(bad);
}

TEST(Booleans, JsonValue)
{
    teptris_document *doc = parse_ok("t = true");
    ASSERT_NE(doc, nullptr);
    DocGuard g(doc);
    EXPECT_EQ(emit_json(doc), R"({"t":{"type":"bool","value":"true"}})");
}
