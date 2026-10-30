/* Writer unit tests: op-stream -> TOML text, then parse back and
 * compare values; plus the error contract (unbalanced, value after
 * sub-table, malformed stream, keyless sections). */
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "teptris/teptris.h"
#include "util.hpp"

namespace {

std::vector<uint8_t> stream(std::vector<uint8_t> prefix)
{
    prefix.push_back(0x00); /* END-OF-CHUNK */
    return prefix;
}

void put_key(std::vector<uint8_t> &s, const char *k)
{
    s.push_back(0x01);
    s.push_back((uint8_t)strlen(k));
    s.insert(s.end(), k, k + strlen(k));
}

void put_int(std::vector<uint8_t> &s, int64_t v)
{
    s.push_back(0x03);
    for (int i = 0; i < 8; i++) {
        s.push_back((uint8_t)(v >> (8 * i)));
    }
}

void put_str(std::vector<uint8_t> &s, const char *v)
{
    s.push_back(0x02);
    size_t n = strlen(v);
    s.push_back((uint8_t)(n & 0xFF));
    s.push_back((uint8_t)((n >> 8) & 0xFF));
    s.push_back((uint8_t)((n >> 16) & 0xFF));
    s.push_back((uint8_t)((n >> 24) & 0xFF));
    s.insert(s.end(), v, v + n);
}

/* parse the writer output; the parse must succeed and round-trip the
 * emitted text */
teptris_document *parse_ok(const char *text)
{
    teptris_document *doc = NULL;
    teptris_status st = teptris_parse(text, strlen(text), NULL, &doc);
    EXPECT_EQ(st, TEPTRIS_OK);
    EXPECT_NE(doc, nullptr);
    return doc;
}

std::string emit_and_parse(std::vector<uint8_t> ops)
{
    teptris_writer *w = teptris_writer_new();
    EXPECT_NE(w, nullptr);
    EXPECT_EQ(teptris_writer_feed(w, ops.data(), ops.size()), TEPTRIS_OK);
    char *buf = NULL;
    size_t len = 0;
    teptris_status st = teptris_writer_finish(w, &buf, &len);
    EXPECT_EQ(st, TEPTRIS_OK);
    EXPECT_NE(buf, nullptr);
    teptris_document *doc = parse_ok(buf);
    char *out = NULL;
    size_t olen = 0;
    EXPECT_EQ(teptris_document_emit(doc, &out, &olen), TEPTRIS_OK);
    std::string text_str(buf, len);
    std::string canon(out, olen);
    /* the writer output must itself re-emit identically (canonical) */
    EXPECT_EQ(text_str, canon);
    free(out);
    teptris_document_free(doc);
    free(buf);
    teptris_writer_free(w);
    return text_str;
}

} /* namespace */

TEST(Writer, ScalarsAtRoot)
{
    std::vector<uint8_t> s;
    put_key(s, "a");
    put_int(s, 42);
    put_key(s, "b");
    put_str(s, "x");
    put_key(s, "c");
    s.push_back(0x05); /* false */
    put_key(s, "d");
    s.push_back(0x0E); /* empty array */
    std::string text = emit_and_parse(stream(s));
    EXPECT_NE(text.find("a = 42"), std::string::npos);
    EXPECT_NE(text.find("b = 'x'"), std::string::npos);
    EXPECT_NE(text.find("c = false"), std::string::npos);
    EXPECT_NE(text.find("d = []"), std::string::npos);
}

TEST(Writer, SectionsAndNesting)
{
    std::vector<uint8_t> s;
    put_key(s, "root_v");
    put_int(s, 1);
    put_key(s, "top");
    put_int(s, 4);
    put_key(s, "a");
    s.push_back(0x08); /* BEGIN-TABLE [a] */
    put_key(s, "x");
    put_int(s, 2);
    put_key(s, "b");
    s.push_back(0x08); /* [a.b] */
    put_key(s, "y");
    put_int(s, 3);
    s.push_back(0x0C); /* CLOSE a.b */
    s.push_back(0x0C); /* CLOSE a */
    std::string text = emit_and_parse(stream(s));
    EXPECT_NE(text.find("[a]"), std::string::npos);
    EXPECT_NE(text.find("[a.b]"), std::string::npos);
    EXPECT_NE(text.find("top = 4"), std::string::npos);
}

TEST(Writer, AOT)
{
    std::vector<uint8_t> s;
    put_key(s, "fruit");
    s.push_back(0x0A); /* BEGIN-AOT */
    s.push_back(0x08); /* element 1: BEGIN-TABLE (keyless) */
    put_key(s, "name");
    put_str(s, "apple");
    s.push_back(0x0C); /* close element */
    s.push_back(0x08); /* element 2 */
    put_key(s, "name");
    put_str(s, "banana");
    s.push_back(0x0C);
    s.push_back(0x0C); /* close AOT */
    std::string text = emit_and_parse(stream(s));
    EXPECT_NE(text.find("[[fruit]]"), std::string::npos);
    EXPECT_NE(text.find("name = 'apple'"), std::string::npos);
    EXPECT_NE(text.find("name = 'banana'"), std::string::npos);
}

TEST(Writer, InlineArraysAndTables)
{
    std::vector<uint8_t> s;
    put_key(s, "arr");
    s.push_back(0x09); /* BEGIN-ARRAY */
    put_int(s, 1);
    put_int(s, 2);
    s.push_back(0x0B); /* inline table element (bare) */
    put_key(s, "k");
    put_str(s, "v");
    s.push_back(0x0C); /* close inline table */
    s.push_back(0x0C); /* close array */
    put_key(s, "e");
    s.push_back(0x0D); /* empty table value */
    std::string text = emit_and_parse(stream(s));
    EXPECT_NE(text.find("arr = [1, 2, {k = 'v'}]"), std::string::npos);
    EXPECT_NE(text.find("e = {}"), std::string::npos);
}

TEST(Writer, InlineTableMultiPair)
{
    /* every pair after the first needs ", " - scalars, nested inline
     * tables, inline arrays, and datetimes all separate the same way */
    std::vector<uint8_t> s;
    put_key(s, "m");
    s.push_back(0x0B); /* BEGIN-INLINE-TABLE */
    put_key(s, "a");
    put_int(s, 1);
    put_key(s, "b");
    put_str(s, "x");
    put_key(s, "c");
    s.push_back(0x06); /* true */
    s.push_back(0x0C);
    std::string text = emit_and_parse(stream(s));
    EXPECT_NE(text.find("m = {a = 1, b = 'x', c = true}"), std::string::npos);
}

TEST(Writer, InlineTableNestedContainers)
{
    std::vector<uint8_t> s;
    put_key(s, "m");
    s.push_back(0x0B);
    put_key(s, "a");
    put_int(s, 1);
    put_key(s, "n");
    s.push_back(0x0B); /* nested inline table */
    put_key(s, "x");
    put_int(s, 1);
    put_key(s, "y");
    put_int(s, 2);
    s.push_back(0x0C);
    put_key(s, "arr");
    s.push_back(0x09); /* inline array as a pair value */
    put_int(s, 1);
    put_int(s, 2);
    s.push_back(0x0C);
    s.push_back(0x0C);
    std::string text = emit_and_parse(stream(s));
    EXPECT_NE(text.find("m = {a = 1, n = {x = 1, y = 2}, arr = [1, 2]}"),
              std::string::npos);
}

TEST(Writer, InlineTableDatetimePairSeparator)
{
    /* the mixed-corpus regression: a datetime pair followed by another
     * pair lost its separator ({created = 2026-01-17weight = 9}) */
    std::vector<uint8_t> s;
    put_key(s, "m");
    s.push_back(0x0B);
    put_key(s, "created");
    s.push_back(0x07); /* DATETIME */
    s.push_back(6);    /* DATE */
    int64_t year = 2026;
    for (int i = 0; i < 8; i++) {
        s.push_back((uint8_t)(year >> (8 * i)));
    }
    s.push_back(1);  /* month */
    s.push_back(17); /* day */
    s.push_back(0);  /* hour */
    s.push_back(0);  /* minute */
    s.push_back(0);  /* second */
    for (int i = 0; i < 4; i++) {
        s.push_back(0); /* nanosecond */
    }
    for (int i = 0; i < 8; i++) {
        s.push_back(0); /* offset */
    }
    put_key(s, "weight");
    put_int(s, 9);
    s.push_back(0x0C);
    std::string text = emit_and_parse(stream(s));
    EXPECT_NE(text.find("m = {created = 2026-01-17, weight = 9}"),
              std::string::npos);
}

TEST(Writer, EmptyKeys)
{
    /* the parse side accepts '' = 1 (quoted empty keys are legal
     * TOML); the writer must emit them, not conflate the empty key
     * with "no pending key" */
    {
        std::vector<uint8_t> s;
        put_key(s, ""); /* KEY op with klen 0 */
        put_int(s, 1);
        std::string text = emit_and_parse(stream(s));
        EXPECT_NE(text.find("'' = 1"), std::string::npos);
    }
    /* empty key inside an inline table: {'' = 1, b = 2} */
    {
        std::vector<uint8_t> s;
        put_key(s, "m");
        s.push_back(0x0B);
        put_key(s, "");
        put_int(s, 1);
        put_key(s, "b");
        put_int(s, 2);
        s.push_back(0x0C);
        std::string text = emit_and_parse(stream(s));
        EXPECT_NE(text.find("{'' = 1, b = 2}"), std::string::npos);
    }
    /* [''] section */
    {
        std::vector<uint8_t> s;
        put_key(s, "");
        s.push_back(0x08);
        put_key(s, "x");
        put_int(s, 1);
        s.push_back(0x0C);
        std::string text = emit_and_parse(stream(s));
        EXPECT_NE(text.find("['']"), std::string::npos);
        EXPECT_NE(text.find("x = 1"), std::string::npos);
    }
}

TEST(Writer, Datetimes)
{
    std::vector<uint8_t> s;
    put_key(s, "od");
    s.push_back(0x07);
    s.push_back(4); /* DATETIME_OFFSET */
    int64_t year = 1979;
    for (int i = 0; i < 8; i++) {
        s.push_back((uint8_t)(year >> (8 * i)));
    }
    s.push_back(5);  /* month */
    s.push_back(27); /* day */
    s.push_back(7);  /* hour */
    s.push_back(32); /* minute */
    s.push_back(0);  /* second */
    uint32_t ns = 999000000;
    for (int i = 0; i < 4; i++) {
        s.push_back((uint8_t)(ns >> (8 * i)));
    }
    int64_t off = -25200;
    for (int i = 0; i < 8; i++) {
        s.push_back((uint8_t)(off >> (8 * i)));
    }
    std::string text = emit_and_parse(stream(s));
    EXPECT_NE(text.find("od = 1979-05-27T07:32:00.999-07:00"),
              std::string::npos);
}

TEST(Writer, ValueAfterSubtableRejected)
{
    std::vector<uint8_t> s;
    put_key(s, "a");
    s.push_back(0x08); /* [a] */
    s.push_back(0x0C); /* close a */
    put_key(s, "late");
    put_int(s, 1);
    s.push_back(0x00);
    teptris_writer *w = teptris_writer_new();
    /* [a] emits the a section; "late = 1" at the ROOT now lands after
     * the sub-table -> ERR_STATE by contract */
    EXPECT_EQ(teptris_writer_feed(w, s.data(), s.size()), TEPTRIS_ERR_STATE);
    char *buf = NULL;
    size_t len = 0;
    EXPECT_EQ(teptris_writer_finish(w, &buf, &len), TEPTRIS_ERR_STATE);
    teptris_writer_free(w);
}

TEST(Writer, MalformedStreams)
{
    /* truncated KEY */
    {
        uint8_t ops[] = {0x01, 0x05, 'a'};
        teptris_writer *w = teptris_writer_new();
        EXPECT_EQ(teptris_writer_feed(w, ops, sizeof ops),
                  TEPTRIS_ERR_ARG);
        teptris_writer_free(w);
    }
    /* truncated STRING length */
    {
        uint8_t ops[] = {0x01, 0x01, 'a', 0x02, 0xFF, 0x00, 0x00, 0x00};
        teptris_writer *w = teptris_writer_new();
        EXPECT_EQ(teptris_writer_feed(w, ops, sizeof ops),
                  TEPTRIS_ERR_ARG);
        teptris_writer_free(w);
    }
    /* unknown opcode */
    {
        uint8_t ops[] = {0x7F, 0x00};
        teptris_writer *w = teptris_writer_new();
        EXPECT_EQ(teptris_writer_feed(w, ops, sizeof ops),
                  TEPTRIS_ERR_ARG);
        teptris_writer_free(w);
    }
    /* unclosed array at finish */
    {
        uint8_t ops[] = {0x01, 0x01, 'a', 0x09, 0x03, 1, 0, 0, 0, 0, 0,
                         0,    0,    0x00};
        teptris_writer *w = teptris_writer_new();
        EXPECT_EQ(teptris_writer_feed(w, ops, sizeof ops), TEPTRIS_OK);
        char *buf = NULL;
        size_t len = 0;
        EXPECT_EQ(teptris_writer_finish(w, &buf, &len), TEPTRIS_ERR_STATE);
        teptris_writer_free(w);
    }
    /* keyless section */
    {
        uint8_t ops[] = {0x08, 0x00};
        teptris_writer *w = teptris_writer_new();
        EXPECT_EQ(teptris_writer_feed(w, ops, sizeof ops),
                  TEPTRIS_ERR_ARG);
        teptris_writer_free(w);
    }
}

TEST(Writer, Empty)
{
    teptris_writer *w = teptris_writer_new();
    uint8_t ops[] = {0x00};
    EXPECT_EQ(teptris_writer_feed(w, ops, 1), TEPTRIS_OK);
    char *buf = NULL;
    size_t len = 0;
    EXPECT_EQ(teptris_writer_finish(w, &buf, &len), TEPTRIS_OK);
    EXPECT_EQ(len, 0u);
    free(buf);
    teptris_writer_free(w);
}
