#include "teptris/teptris.h"

#include "util.hpp"

TEST(Datetime, OffsetKinds)
{
    teptris_document *doc = parse_ok(
        "a = 1979-05-27T07:32:00Z\n"
        "b = 1979-05-27T00:32:00-07:00\n"
        "c = 1979-05-27 07:32:00Z\n"
        "d = 1979-05-27t07:32:00z\n");
    ASSERT_NE(doc, nullptr);
    DocGuard g(doc);
    const teptris_node *root = teptris_document_root(doc);

    teptris_datetime dt;
    ASSERT_EQ(teptris_node_datetime(get(root, "a"), &dt), TEPTRIS_OK);
    EXPECT_EQ(dt.year, 1979);
    EXPECT_EQ(dt.month, 5);
    EXPECT_EQ(dt.day, 27);
    EXPECT_EQ(dt.hour, 7);
    EXPECT_EQ(dt.offset_seconds, 0);
    ASSERT_EQ(teptris_node_datetime(get(root, "b"), &dt), TEPTRIS_OK);
    EXPECT_EQ(dt.offset_seconds, -7 * 3600);
    ASSERT_EQ(teptris_node_datetime(get(root, "c"), &dt), TEPTRIS_OK);
    EXPECT_EQ(dt.offset_seconds, 0);
    ASSERT_EQ(teptris_node_datetime(get(root, "d"), &dt), TEPTRIS_OK);
    EXPECT_EQ(dt.offset_seconds, 0);
    EXPECT_EQ(teptris_node_kind(get(root, "a")), TEPTRIS_DATETIME_OFFSET);
}

TEST(Datetime, LocalKinds)
{
    teptris_document *doc = parse_ok(
        "ldt = 1979-05-27T07:32:00\n"
        "ld = 1979-05-27\n"
        "lt = 07:32:00\n"
        "ltf = 00:32:00.999999\n");
    ASSERT_NE(doc, nullptr);
    DocGuard g(doc);
    const teptris_node *root = teptris_document_root(doc);
    EXPECT_EQ(teptris_node_kind(get(root, "ldt")), TEPTRIS_DATETIME_LOCAL);
    EXPECT_EQ(teptris_node_kind(get(root, "ld")), TEPTRIS_DATE_LOCAL);
    EXPECT_EQ(teptris_node_kind(get(root, "lt")), TEPTRIS_TIME_LOCAL);

    teptris_datetime dt;
    ASSERT_EQ(teptris_node_datetime(get(root, "ltf"), &dt), TEPTRIS_OK);
    EXPECT_EQ(dt.nanosecond, 999999000u);
}

TEST(Datetime, FractionAndLeap)
{
    teptris_document *doc = parse_ok("a = 1979-05-27T00:32:00.999999999+07:00\n"
                                     "leap = 2016-12-31T23:59:60Z");
    ASSERT_NE(doc, nullptr);
    DocGuard g(doc);
    const teptris_node *root = teptris_document_root(doc);
    teptris_datetime dt;
    ASSERT_EQ(teptris_node_datetime(get(root, "a"), &dt), TEPTRIS_OK);
    EXPECT_EQ(dt.nanosecond, 999999999u);
    EXPECT_EQ(dt.offset_seconds, 7 * 3600);
    ASSERT_EQ(teptris_node_datetime(get(root, "leap"), &dt), TEPTRIS_OK);
    EXPECT_EQ(dt.second, 60);
}

TEST(Datetime, JsonValue)
{
    struct {
        const char *in;
        const char *json;
    } cases[] = {
        {"a = 1979-05-27T07:32:00Z",
         R"({"a":{"type":"datetime","value":"1979-05-27T07:32:00Z"}})"},
        {"a = 1979-05-27T07:32:00",
         R"({"a":{"type":"datetime-local","value":"1979-05-27T07:32:00"}})"},
        {"a = 1979-05-27", R"({"a":{"type":"date-local","value":"1979-05-27"}})"},
        {"a = 07:32:00", R"({"a":{"type":"time-local","value":"07:32:00"}})"},
        {"a = 1979-05-27T00:32:00.999999-07:00",
         R"({"a":{"type":"datetime","value":"1979-05-27T00:32:00.999999-07:00"}})"},
    };
    for (auto &c : cases) {
        teptris_document *doc = parse_ok(c.in);
        ASSERT_NE(doc, nullptr);
        DocGuard g(doc);
        EXPECT_EQ(emit_json(doc), c.json) << "input: " << c.in;
    }
}

TEST(Datetime, Invalid)
{
    const char *bad[] = {
        "a = 1979-13-27",    "a = 1979-00-27",   "a = 1979-02-30",
        "a = 1979-05-32",    "a = 1979-05-27T24:00:00",
        "a = 1979-05-27T07:60:00", "a = 1979-05-27T07:32:61",
        "a = 1979-05-27T07:32:00+25:00", "a = 1979-05-27T07:32", "a = 25:00:00",
    };
    for (const char *in : bad) {
        teptris_document *doc = nullptr;
        teptris_status st = parse_status(in, &doc);
        EXPECT_EQ(st, TEPTRIS_ERR_SYNTAX) << "input: " << in;
        DocGuard g(doc);
    }
}

TEST(Datetime, IntDateDisambiguation)
{
    /* A plain integer stays an integer; the lookahead needs the full shape. */
    teptris_document *doc = parse_ok("a = 1979\nb = 19790527");
    ASSERT_NE(doc, nullptr);
    DocGuard g(doc);
    EXPECT_EQ(teptris_node_kind(get(teptris_document_root(doc), "a")),
              TEPTRIS_INTEGER);
    EXPECT_EQ(teptris_node_kind(get(teptris_document_root(doc), "b")),
              TEPTRIS_INTEGER);
}
