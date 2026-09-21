/* Batch parse coverage (teptris-ruby#108 ask 3): the C entry point
 * `teptris_parse_batch` parses N inputs in one call, reports per-doc
 * status, and never leaves a partial batch on the alloc-fail path. */
#include <cstring>

#include "teptris/teptris.h"
#include "util.hpp"

namespace {

struct DocGuard {
    teptris_document *doc = nullptr;
    ~DocGuard() { teptris_document_free(doc); }
};

TEST(ParseBatch, AllOk)
{
    const char *a = "a = 1\n";
    const char *b = "x = \"y\"\nz = [1, 2]\n";
    const char *c = "[t]\nk = 1\n[[t.more]]\nn = 1\n";
    const char *const data[] = {a, b, c};
    const size_t lens[] = {strlen(a), strlen(b), strlen(c)};
    DocGuard docs[3];
    teptris_status statuses[3];
    ASSERT_EQ(teptris_parse_batch(data, lens, 3, nullptr, &docs[0].doc,
                                  statuses),
              TEPTRIS_OK);
    for (int i = 0; i < 3; i++) {
        EXPECT_EQ(statuses[i], TEPTRIS_OK);
        EXPECT_NE(docs[i].doc, nullptr);
        ASSERT_NE(teptris_document_root(docs[i].doc), nullptr);
    }
}

TEST(ParseBatch, PerDocErrorReportsStatusAndSetsDoc)
{
    const char *good = "a = 1\n";
    const char *bad = "a = [1, 2,\n"; /* unterminated */
    const char *const data[] = {good, bad, good};
    const size_t lens[] = {strlen(good), strlen(bad), strlen(good)};
    DocGuard docs[3];
    teptris_status statuses[3];
    ASSERT_EQ(teptris_parse_batch(data, lens, 3, nullptr, &docs[0].doc,
                                  statuses),
              TEPTRIS_OK);
    EXPECT_EQ(statuses[0], TEPTRIS_OK);
    EXPECT_NE(statuses[1], TEPTRIS_OK);
    EXPECT_EQ(statuses[2], TEPTRIS_OK);
    /* failing slot still yields a doc so the caller can read the error */
    EXPECT_NE(docs[1].doc, nullptr);
    const teptris_error *e = teptris_document_error(docs[1].doc);
    EXPECT_NE(e->message, nullptr);
}

TEST(ParseBatch, EmptyIsOk)
{
    teptris_status statuses[1];
    DocGuard docs[1];
    EXPECT_EQ(teptris_parse_batch(nullptr, nullptr, 0, nullptr,
                                  &docs[0].doc, statuses),
              TEPTRIS_OK);
}

TEST(ParseBatch, NullArraysWithNIsArg)
{
    DocGuard docs[1];
    teptris_status statuses[1];
    EXPECT_EQ(teptris_parse_batch(nullptr, nullptr, 1, nullptr,
                                  &docs[0].doc, statuses),
              TEPTRIS_ERR_ARG);
}

}  // namespace
