#include <cstdlib>
#include <string>

#include "teptris/dom/dom.h"
#include "teptris/teptris.h"
#include "util.hpp"

namespace {

struct Guard {
    teptris_builder *b;
    Guard() : b(teptris_builder_new()) {}
    ~Guard() { teptris_builder_free(b); }
};

struct DocGuard {
    teptris_document *doc = nullptr;
    ~DocGuard() { teptris_document_free(doc); }
};

/* build -> emit -> reparse; returns the emitted canonical text and
 * fills the reparsed document */
std::string build_emit(teptris_builder *b, DocGuard &rg)
{
    teptris_document *doc = nullptr;
    EXPECT_EQ(teptris_builder_finish(b, &doc), TEPTRIS_OK);
    char *buf = nullptr;
    size_t len = 0;
    teptris_document *rd = nullptr;
    EXPECT_EQ(teptris_document_emit(doc, &buf, &len), TEPTRIS_OK);
    std::string out(buf, len);
    free(buf);
    EXPECT_EQ(teptris_parse(out.data(), out.size(), nullptr, &rd), TEPTRIS_OK);
    rg.doc = rd;
    teptris_document_free(doc);
    return out;
}

TEST(Builder, RootScalars)
{
    Guard g;
    ASSERT_NE(g.b, nullptr);
    ASSERT_EQ(teptris_builder_put_string(g.b, "s", 1, "v", 1), TEPTRIS_OK);
    ASSERT_EQ(teptris_builder_put_integer(g.b, "i", 1, -42), TEPTRIS_OK);
    ASSERT_EQ(teptris_builder_put_float(g.b, "f", 1, 3.5), TEPTRIS_OK);
    ASSERT_EQ(teptris_builder_put_boolean(g.b, "b", 1, true), TEPTRIS_OK);
    DocGuard rg;
    std::string toml = build_emit(g.b, rg);
    EXPECT_EQ(toml, "s = 'v'\ni = -42\nf = 3.5\nb = true\n");
    ASSERT_NE(rg.doc, nullptr);
    const teptris_node *root = teptris_document_root(rg.doc);
    int64_t i = 0;
    double f = 0;
    ASSERT_EQ(teptris_node_integer(teptris_node_table_get(root, "i", 1), &i),
              TEPTRIS_OK);
    ASSERT_EQ(teptris_node_float(teptris_node_table_get(root, "f", 1), &f),
              TEPTRIS_OK);
    EXPECT_EQ(i, -42);
    EXPECT_DOUBLE_EQ(f, 3.5);
}

TEST(Builder, SectionsAndAot)
{
    Guard g;
    ASSERT_EQ(teptris_builder_put_string(g.b, "root", 4, "x", 1), TEPTRIS_OK);
    ASSERT_EQ(teptris_builder_open_table(g.b, "a", 1), TEPTRIS_OK);
    ASSERT_EQ(teptris_builder_put_integer(g.b, "k", 1, 1), TEPTRIS_OK);
    ASSERT_EQ(teptris_builder_close(g.b), TEPTRIS_OK);
    ASSERT_EQ(teptris_builder_open_array(g.b, "arr", 3), TEPTRIS_OK);
    ASSERT_EQ(teptris_builder_open_table(g.b, nullptr, 0), TEPTRIS_OK);
    ASSERT_EQ(teptris_builder_put_string(g.b, "n", 1, "one", 3), TEPTRIS_OK);
    ASSERT_EQ(teptris_builder_close(g.b), TEPTRIS_OK);
    ASSERT_EQ(teptris_builder_open_table(g.b, nullptr, 0), TEPTRIS_OK);
    ASSERT_EQ(teptris_builder_put_string(g.b, "n", 1, "two", 3), TEPTRIS_OK);
    ASSERT_EQ(teptris_builder_close(g.b), TEPTRIS_OK);
    ASSERT_EQ(teptris_builder_close(g.b), TEPTRIS_OK);
    DocGuard rg;
    std::string toml = build_emit(g.b, rg);
    EXPECT_EQ(toml,
              "root = 'x'\n[a]\nk = 1\n[[arr]]\nn = 'one'\n[[arr]]\nn = 'two'\n");
    const teptris_node *root = teptris_document_root(rg.doc);
    const teptris_node *arr = teptris_node_table_get(root, "arr", 3);
    ASSERT_EQ(teptris_node_array_length(arr), 2u);
}

TEST(Builder, InlineShapes)
{
    Guard g;
    ASSERT_EQ(teptris_builder_open_array(g.b, "nums", 4), TEPTRIS_OK);
    ASSERT_EQ(teptris_builder_put_integer(g.b, nullptr, 0, 1), TEPTRIS_OK);
    ASSERT_EQ(teptris_builder_open_array(g.b, nullptr, 0), TEPTRIS_OK);
    ASSERT_EQ(teptris_builder_put_float(g.b, nullptr, 0, 2.5), TEPTRIS_OK);
    ASSERT_EQ(teptris_builder_close(g.b), TEPTRIS_OK);
    ASSERT_EQ(teptris_builder_close(g.b), TEPTRIS_OK);
    /* table element inside an inline array renders inline */
    ASSERT_EQ(teptris_builder_open_array(g.b, "mix", 3), TEPTRIS_OK);
    ASSERT_EQ(teptris_builder_open_table(g.b, nullptr, 0), TEPTRIS_OK);
    ASSERT_EQ(teptris_builder_put_integer(g.b, "z", 1, 9), TEPTRIS_OK);
    ASSERT_EQ(teptris_builder_close(g.b), TEPTRIS_OK);
    ASSERT_EQ(teptris_builder_close(g.b), TEPTRIS_OK);
    /* forced inline: [{..}, 2] renders as an inline array */
    ASSERT_EQ(teptris_builder_open_inline_array(g.b, "m2", 2), TEPTRIS_OK);
    ASSERT_EQ(teptris_builder_open_table(g.b, nullptr, 0), TEPTRIS_OK);
    ASSERT_EQ(teptris_builder_put_integer(g.b, "q", 1, 1), TEPTRIS_OK);
    ASSERT_EQ(teptris_builder_close(g.b), TEPTRIS_OK);
    ASSERT_EQ(teptris_builder_put_integer(g.b, nullptr, 0, 2), TEPTRIS_OK);
    ASSERT_EQ(teptris_builder_close(g.b), TEPTRIS_OK);
    DocGuard rg;
    std::string toml = build_emit(g.b, rg);
    /* table-element arrays under a key are arrays of tables; an
     * explicit inline array keeps [..] rendering for mixed content */
    EXPECT_EQ(toml, "nums = [1, [2.5]]\nm2 = [{q = 1}, 2]\n[[mix]]\nz = 9\n");
}

TEST(Builder, DatetimeKinds)
{
    Guard g;
    teptris_datetime dt{};
    dt.year = 1979; dt.month = 5; dt.day = 27;
    dt.hour = 7; dt.minute = 32; dt.second = 0; dt.nanosecond = 500000000;
    dt.offset_seconds = 3600;
    ASSERT_EQ(teptris_builder_put_datetime(g.b, "off", 3,
                                           TEPTRIS_DATETIME_OFFSET, &dt),
              TEPTRIS_OK);
    dt.offset_seconds = 0;
    ASSERT_EQ(teptris_builder_put_datetime(g.b, "loc", 3,
                                           TEPTRIS_DATETIME_LOCAL, &dt),
              TEPTRIS_OK);
    dt.hour = dt.minute = dt.second = 0; dt.nanosecond = 0;
    ASSERT_EQ(teptris_builder_put_datetime(g.b, "d", 1,
                                           TEPTRIS_DATE_LOCAL, &dt),
              TEPTRIS_OK);
    dt.year = dt.month = dt.day = 0;
    dt.hour = 7; dt.minute = 32; dt.second = 1;
    ASSERT_EQ(teptris_builder_put_datetime(g.b, "t", 1,
                                           TEPTRIS_TIME_LOCAL, &dt),
              TEPTRIS_OK);
    DocGuard rg;
    std::string toml = build_emit(g.b, rg);
    EXPECT_EQ(toml,
              "off = 1979-05-27T07:32:00.5+01:00\n"
              "loc = 1979-05-27T07:32:00.5\n"
              "d = 1979-05-27\nt = 07:32:01\n");
}

TEST(Builder, Rejects)
{
    Guard g;
    ASSERT_EQ(teptris_builder_put_integer(g.b, "a", 1, 1), TEPTRIS_OK);
    EXPECT_EQ(teptris_builder_put_string(g.b, "a", 1, "dup", 3),
              TEPTRIS_ERR_ARG);
    /* scalar-leading array stays inline; a table element joins it */
    ASSERT_EQ(teptris_builder_open_array(g.b, "arr", 3), TEPTRIS_OK);
    ASSERT_EQ(teptris_builder_put_integer(g.b, nullptr, 0, 1), TEPTRIS_OK);
    ASSERT_EQ(teptris_builder_open_table(g.b, nullptr, 0), TEPTRIS_OK);
    ASSERT_EQ(teptris_builder_put_integer(g.b, "z", 1, 9), TEPTRIS_OK);
    ASSERT_EQ(teptris_builder_close(g.b), TEPTRIS_OK);
    ASSERT_EQ(teptris_builder_close(g.b), TEPTRIS_OK);
    /* element push without an array current */
    EXPECT_EQ(teptris_builder_put_integer(g.b, nullptr, 0, 1), TEPTRIS_ERR_ARG);
    /* scalar into an AOT */
    ASSERT_EQ(teptris_builder_open_array(g.b, "aot", 3), TEPTRIS_OK);
    ASSERT_EQ(teptris_builder_open_table(g.b, nullptr, 0), TEPTRIS_OK);
    ASSERT_EQ(teptris_builder_close(g.b), TEPTRIS_OK);
    EXPECT_EQ(teptris_builder_put_string(g.b, nullptr, 0, "x", 1),
              TEPTRIS_ERR_ARG);
    ASSERT_EQ(teptris_builder_close(g.b), TEPTRIS_OK);
    /* datetime validation */
    teptris_datetime bad{};
    bad.month = 13; bad.day = 1;
    EXPECT_EQ(teptris_builder_put_datetime(g.b, "x", 1,
                                           TEPTRIS_DATE_LOCAL, &bad),
              TEPTRIS_ERR_ARG);
    /* time fields set on a date */
    teptris_datetime mixed{};
    mixed.year = 2000; mixed.month = 1; mixed.day = 1; mixed.hour = 1;
    EXPECT_EQ(teptris_builder_put_datetime(g.b, "x", 1,
                                           TEPTRIS_DATE_LOCAL, &mixed),
              TEPTRIS_ERR_ARG);
}

TEST(Builder, UnclosedFinishAndOwnership)
{
    Guard g;
    ASSERT_EQ(teptris_builder_open_table(g.b, "t", 1), TEPTRIS_OK);
    teptris_document *doc = nullptr;
    EXPECT_EQ(teptris_builder_finish(g.b, &doc), TEPTRIS_ERR_STATE);
    ASSERT_EQ(teptris_builder_close(g.b), TEPTRIS_OK);
    ASSERT_EQ(teptris_builder_finish(g.b, &doc), TEPTRIS_OK);
    ASSERT_NE(doc, nullptr);
    char *buf = nullptr;
    size_t len = 0;
    EXPECT_EQ(teptris_document_emit(doc, &buf, &len), TEPTRIS_OK);
    free(buf);
    teptris_document_free(doc);
    /* finish transferred ownership: a second finish reports state */
    teptris_document *again = nullptr;
    EXPECT_EQ(teptris_builder_finish(g.b, &again), TEPTRIS_ERR_STATE);
}

TEST(Builder, RoundTripsCanonically)
{
    const char *src =
        "s = \"quo\\\"ted\"\n"
        "arr = [1, 2, 3]\n"
        "[t]\nn = -1.25\n[[t.rows]]\nk = 'a'\n[[t.rows]]\nk = 'b'\n";
    teptris_document *p = nullptr;
    ASSERT_EQ(teptris_parse(src, strlen(src), nullptr, &p), TEPTRIS_OK);

    Guard g;
    const teptris_node *root = teptris_document_root(p);
    teptris_view sv;
    ASSERT_EQ(teptris_node_string(teptris_node_table_get(root, "s", 1), &sv),
              TEPTRIS_OK);
    ASSERT_EQ(teptris_builder_put_string(g.b, "s", 1, sv.ptr, sv.len),
              TEPTRIS_OK);
    const teptris_node *arr = teptris_node_table_get(root, "arr", 3);
    ASSERT_EQ(teptris_builder_open_array(g.b, "arr", 3), TEPTRIS_OK);
    for (size_t i = 0; i < teptris_node_array_length(arr); i++) {
        int64_t v = 0;
        ASSERT_EQ(teptris_node_integer(teptris_node_array_at(arr, i), &v),
                  TEPTRIS_OK);
        ASSERT_EQ(teptris_builder_put_integer(g.b, nullptr, 0, v),
                  TEPTRIS_OK);
    }
    ASSERT_EQ(teptris_builder_close(g.b), TEPTRIS_OK);
    const teptris_node *t = teptris_node_table_get(root, "t", 1);
    ASSERT_EQ(teptris_builder_open_table(g.b, "t", 1), TEPTRIS_OK);
    double dn = 0;
    ASSERT_EQ(teptris_node_float(teptris_node_table_get(t, "n", 1), &dn),
              TEPTRIS_OK);
    ASSERT_EQ(teptris_builder_put_float(g.b, "n", 1, dn), TEPTRIS_OK);
    const teptris_node *rows = teptris_node_table_get(t, "rows", 4);
    ASSERT_EQ(teptris_builder_open_array(g.b, "rows", 4), TEPTRIS_OK);
    for (size_t i = 0; i < teptris_node_array_length(rows); i++) {
        const teptris_node *row = teptris_node_array_at(rows, i);
        ASSERT_EQ(teptris_builder_open_table(g.b, nullptr, 0), TEPTRIS_OK);
        teptris_view kv;
        ASSERT_EQ(teptris_node_string(teptris_node_table_get(row, "k", 1),
                                      &kv),
                  TEPTRIS_OK);
        ASSERT_EQ(teptris_builder_put_string(g.b, "k", 1, kv.ptr, kv.len),
                  TEPTRIS_OK);
        ASSERT_EQ(teptris_builder_close(g.b), TEPTRIS_OK);
    }
    ASSERT_EQ(teptris_builder_close(g.b), TEPTRIS_OK);
    ASSERT_EQ(teptris_builder_close(g.b), TEPTRIS_OK);
    teptris_document_free(p);

    DocGuard rg;
    std::string built = build_emit(g.b, rg);
    char *pbuf = nullptr;
    size_t plen = 0;
    teptris_document *pd = nullptr;
    ASSERT_EQ(teptris_parse(src, strlen(src), nullptr, &pd), TEPTRIS_OK);
    ASSERT_EQ(teptris_document_emit(pd, &pbuf, &plen), TEPTRIS_OK);
    std::string parsed(pbuf, plen);
    free(pbuf);
    teptris_document_free(pd);
    EXPECT_EQ(built, parsed); /* built == parse(emit(parse(src))) */
}

} // namespace
