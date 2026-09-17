/* Flatten wire-format coverage: parse a document holding every kind,
 * flatten it, and decode the self-describing buffer back to values —
 * proving the public bulk-drain ABI (teptris_document_flatten) agrees
 * with the tree. Little-endian is explicit in the format; this test
 * decodes with memcpy so it holds on either endianness. */
#include <cstring>
#include <string>

#include "teptris/teptris.h"
#include "util.hpp"

namespace {

struct BufGuard {
    uint8_t *p = nullptr;
    size_t len = 0;
    ~BufGuard() { teptris_flatten_free(p); }
};

struct DocGuard {
    teptris_document *doc = nullptr;
    ~DocGuard() { teptris_document_free(doc); }
};

struct Cursor {
    const uint8_t *p;
    size_t len, off = 0;
    void need(size_t n) const { ASSERT_LE(off + n, len); }
    uint8_t byte() { need(1); return p[off++]; }
    uint32_t u32() { need(4); uint32_t v; memcpy(&v, p + off, 4); off += 4; return v; }
    int64_t i64() { need(8); int64_t v; memcpy(&v, p + off, 8); off += 8; return v; }
    double f64() { need(8); double v; memcpy(&v, p + off, 8); off += 8; return v; }
    std::string str(size_t n) { need(n); std::string s((const char *)p + off, n); off += n; return s; }
};

/* minimal datetime decode: y(i64) mo d h mi s(u8x5) ns(u32) off(i64) */
struct Dt {
    int64_t y, off;
    uint32_t ns;
    uint8_t mo, d, h, mi, s;
};

Dt dt(Cursor &c)
{
    Dt r;
    r.y = c.i64();
    r.mo = c.byte(); r.d = c.byte(); r.h = c.byte(); r.mi = c.byte(); r.s = c.byte();
    r.ns = c.u32();
    r.off = c.i64();
    return r;
}

TEST(Flatten, EveryKindRoundTrip)
{
    const char *src =
        "s = \"hello\"\n"
        "i = -42\n"
        "f = 1.5\n"
        "t = true\n"
        "f2 = false\n"
        "dto = 1979-05-27T07:32:00Z\n"
        "dtl = 1979-05-27T07:32:00\n"
        "d = 1979-05-27\n"
        "tm = 07:32:00\n"
        "arr = [1, 2, 3]\n"
        "[tbl.k]\n"
        "nested = \"x\"\n";
    DocGuard g;
    ASSERT_EQ(teptris_parse(src, strlen(src), nullptr, &g.doc), TEPTRIS_OK);
    BufGuard fb;
    ASSERT_EQ(teptris_document_flatten(g.doc, &fb.p, &fb.len), TEPTRIS_OK);
    ASSERT_NE(fb.p, nullptr);
    ASSERT_GT(fb.len, 0u);

    Cursor c{fb.p, fb.len};
    /* root table: 11 top-level keys in insertion order */
    ASSERT_EQ(c.byte(), 0x01);
    ASSERT_EQ(c.u32(), 11u);

    auto key = [&](const char *want) {
        uint32_t n = c.u32();
        EXPECT_EQ(c.str(n), want);
    };

    key("s");
    ASSERT_EQ(c.byte(), 0x03);
    ASSERT_EQ(c.str(c.u32()), "hello");

    key("i");
    ASSERT_EQ(c.byte(), 0x04);
    ASSERT_EQ(c.i64(), -42);

    key("f");
    ASSERT_EQ(c.byte(), 0x05);
    ASSERT_DOUBLE_EQ(c.f64(), 1.5);

    key("t");
    ASSERT_EQ(c.byte(), 0x07);

    key("f2");
    ASSERT_EQ(c.byte(), 0x06);

    key("dto");
    ASSERT_EQ(c.byte(), 0x08);
    {
        Dt v = dt(c);
        EXPECT_EQ(v.y, 1979); EXPECT_EQ(v.mo, 5); EXPECT_EQ(v.d, 27);
        EXPECT_EQ(v.h, 7); EXPECT_EQ(v.mi, 32); EXPECT_EQ(v.s, 0);
        EXPECT_EQ(v.ns, 0u); EXPECT_EQ(v.off, 0);
    }

    key("dtl");
    ASSERT_EQ(c.byte(), 0x09);
    {
        Dt v = dt(c);
        EXPECT_EQ(v.y, 1979); EXPECT_EQ(v.h, 7); EXPECT_EQ(v.off, 0);
    }

    key("d");
    ASSERT_EQ(c.byte(), 0x0A);
    {
        Dt v = dt(c);
        EXPECT_EQ(v.mo, 5); EXPECT_EQ(v.d, 27);
    }

    key("tm");
    ASSERT_EQ(c.byte(), 0x0B);
    {
        Dt v = dt(c);
        EXPECT_EQ(v.h, 7); EXPECT_EQ(v.mi, 32); EXPECT_EQ(v.s, 0);
    }

    key("arr");
    ASSERT_EQ(c.byte(), 0x02);
    ASSERT_EQ(c.u32(), 3u);
    for (int64_t want = 1; want <= 3; want++) {
        ASSERT_EQ(c.byte(), 0x04);
        ASSERT_EQ(c.i64(), want);
    }

    key("tbl");
    ASSERT_EQ(c.byte(), 0x01);
    ASSERT_EQ(c.u32(), 1u);
    key("k");
    ASSERT_EQ(c.byte(), 0x01);
    ASSERT_EQ(c.u32(), 1u);
    key("nested");
    ASSERT_EQ(c.byte(), 0x03);
    ASSERT_EQ(c.str(c.u32()), "x");

    EXPECT_EQ(c.off, c.len); /* fully consumed, no trailing bytes */
}

TEST(Flatten, EmptyDocument)
{
    DocGuard g;
    ASSERT_EQ(teptris_parse("", 0, nullptr, &g.doc), TEPTRIS_OK);
    BufGuard fb;
    ASSERT_EQ(teptris_document_flatten(g.doc, &fb.p, &fb.len), TEPTRIS_OK);
    Cursor c{fb.p, fb.len};
    ASSERT_EQ(c.byte(), 0x01);
    ASSERT_EQ(c.u32(), 0u);
    EXPECT_EQ(c.off, c.len);
}

TEST(Flatten, RejectsNullArguments)
{
    teptris_document *doc = nullptr;
    ASSERT_EQ(teptris_parse("a = 1\n", 6, nullptr, &doc), TEPTRIS_OK);
    teptris_document_free(doc);
    uint8_t *buf = nullptr;
    size_t len = 0;
    /* NULL doc/buf/len must fail cleanly, not crash */
    ASSERT_EQ(teptris_document_flatten(nullptr, &buf, &len), TEPTRIS_ERR_ARG);
    teptris_flatten_free(nullptr);
}

} // namespace
