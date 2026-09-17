/* Plan-walk coverage (teptris#46): build a plan, walk a document,
 * verify skip/missing/nested/collection/raw semantics by row index. */
#include <cstring>

#include "teptris/dom/dom.h"
#include "teptris/plan.h"
#include "util.hpp"

namespace {

struct PlanGuard {
    teptris_plan *p = nullptr;
    ~PlanGuard() { teptris_plan_free(p); }
};
struct ResGuard {
    teptris_plan_result *r = nullptr;
    ~ResGuard() { teptris_plan_result_free(r); }
};
struct DocGuard {
    teptris_document *doc = nullptr;
    ~DocGuard() { teptris_document_free(doc); }
};

TEST(Plan, WalkSkipsUnplannedAndReportsMissing)
{
    const char *src =
        "name = \"svc\"\n"
        "junk = [1, 2, 3]\n"
        "[server]\n"
        "port = 8080\n"
        "hosts = [\"a\", \"b\"]\n"
        "[[items]]\n"
        "id = 1\n"
        "extra = true\n"
        "[[items]]\n"
        "id = 2\n";
    DocGuard g;
    ASSERT_EQ(teptris_parse(src, strlen(src), nullptr, &g.doc), TEPTRIS_OK);

    /* plan 0 (root): name=scalar, server=nested(1), items=nested(2)
     * plan 1 (server): port=scalar, hosts=collection
     * plan 2 (items): id=scalar */
    teptris_plan_row rows[] = {
        {"name", TEPTRIS_PLAN_SCALAR, 0},
        {"server", TEPTRIS_PLAN_NESTED, 1},
        {"items", TEPTRIS_PLAN_NESTED, 2},
        {"port", TEPTRIS_PLAN_SCALAR, 0},
        {"hosts", TEPTRIS_PLAN_COLLECTION, 0},
        {"id", TEPTRIS_PLAN_SCALAR, 0},
    };
    uint32_t first[] = {0, 3, 5, 6};
    teptris_plan_spec spec{TEPTRIS_PLAN_ABI_VERSION, 3, rows, first};

    ASSERT_EQ(teptris_plan_abi_version(), (uint32_t)TEPTRIS_PLAN_ABI_VERSION);
    PlanGuard pg;
    teptris_status st;
    pg.p = teptris_plan_build(&spec, &st);
    ASSERT_EQ(st, TEPTRIS_OK);
    ASSERT_NE(pg.p, nullptr);
    ASSERT_EQ(teptris_plan_row_count(pg.p, 0), 3u);
    ASSERT_EQ(teptris_plan_row_count(pg.p, 2), 1u);

    ResGuard rg;
    rg.r = teptris_plan_walk(pg.p, teptris_document_root(g.doc), &st);
    ASSERT_EQ(st, TEPTRIS_OK);
    ASSERT_NE(rg.r, nullptr);

    /* row 0: name scalar (junk skipped entirely — no row for it) */
    ASSERT_EQ(teptris_plan_result_kind_at(rg.r, 0), (uint8_t)TEPTRIS_PLAN_SCALAR_RESULT);
    teptris_view v;
    ASSERT_EQ(teptris_plan_result_string_at(rg.r, 0, &v), TEPTRIS_OK);
    EXPECT_EQ(std::string(v.ptr, v.len), "svc");

    /* row 1: server nested table */
    /* MISSING for absent keys / type mismatches */
    ASSERT_EQ(teptris_plan_result_kind_at(rg.r, 0 + 3 + 10), (uint8_t)TEPTRIS_PLAN_MISSING);

    /* row 2: items = array of two nested tables; ids 1 and 2 */
    ASSERT_EQ(teptris_plan_result_kind_at(rg.r, 2), (uint8_t)TEPTRIS_PLAN_ARRAY);
    ASSERT_EQ(teptris_plan_result_array_len_at(rg.r, 2), 2u);
    /* items are tables walked by sub-plan 2: navigate via borrowed
     * entry views, then read row 0 (id) inside each */
    int64_t id0 = -1, id1 = -1;
    teptris_plan_result *e0 = teptris_plan_result_array_entry_at(rg.r, 2, 0);
    teptris_plan_result *e1 = teptris_plan_result_array_entry_at(rg.r, 2, 1);
    ASSERT_NE(e0, nullptr);
    ASSERT_NE(e1, nullptr);
    ASSERT_EQ(teptris_plan_result_kind_at(e0, 0), (uint8_t)TEPTRIS_PLAN_SCALAR_RESULT);
    ASSERT_EQ(teptris_plan_result_integer_at(e0, 0, &id0), TEPTRIS_OK);
    ASSERT_EQ(teptris_plan_result_integer_at(e1, 0, &id1), TEPTRIS_OK);
    EXPECT_EQ(id0, 1);
    EXPECT_EQ(id1, 2);
    teptris_plan_result_view_free(e0);
    teptris_plan_result_view_free(e1);

    /* NULL/arg discipline */
    ASSERT_EQ(teptris_plan_walk(nullptr, teptris_document_root(g.doc), &st),
              nullptr);
}

TEST(Plan, BuildRejectsBadSpecs)
{
    teptris_status st;
    teptris_plan_row rows[] = {{"a", TEPTRIS_PLAN_NESTED, 7}}; /* sub OOR */
    uint32_t first[] = {0, 1};
    teptris_plan_spec spec{TEPTRIS_PLAN_ABI_VERSION, 1, rows, first};
    ASSERT_EQ(teptris_plan_build(&spec, &st), nullptr);
    teptris_plan_spec bad{9, 1, rows, first}; /* wrong ABI */
    ASSERT_EQ(teptris_plan_build(&bad, &st), nullptr);
    teptris_plan_free(nullptr);
}

TEST(Plan, RawNestedCollectionCombo)
{
    /* mirrors the binding regression spec: scalar + AoT-nested + RAW
     * in one root plan (musl segfaulted on the ruby side) */
    const char *src =
        "name = \"svc\"\n[[items]]\nid = 1\n[meta]\nx = \"r\"\n";
    DocGuard g;
    ASSERT_EQ(teptris_parse(src, strlen(src), nullptr, &g.doc), TEPTRIS_OK);
    teptris_plan_row rows[] = {
        {"name", TEPTRIS_PLAN_SCALAR, 0},
        {"items", TEPTRIS_PLAN_NESTED, 1},
        {"meta", TEPTRIS_PLAN_RAW, 0},
        {"id", TEPTRIS_PLAN_SCALAR, 0},
    };
    uint32_t first[] = {0, 3, 4};
    teptris_plan_spec spec{TEPTRIS_PLAN_ABI_VERSION, 2, rows, first};
    PlanGuard pg;
    teptris_status st;
    pg.p = teptris_plan_build(&spec, &st);
    ASSERT_EQ(st, TEPTRIS_OK);
    ResGuard rg;
    rg.r = teptris_plan_walk(pg.p, teptris_document_root(g.doc), &st);
    ASSERT_EQ(st, TEPTRIS_OK);

    ASSERT_EQ(teptris_plan_result_kind_at(rg.r, 2), (uint8_t)TEPTRIS_PLAN_RAW_RESULT);
    const teptris_node *raw = teptris_plan_result_raw_at(rg.r, 2);
    ASSERT_NE(raw, nullptr);
    teptris_view rv;
    ASSERT_EQ(teptris_node_table_get(raw, "x", 1) != nullptr, true);

    teptris_plan_result *e = teptris_plan_result_array_entry_at(rg.r, 1, 0);
    ASSERT_NE(e, nullptr);
    int64_t id = -1;
    ASSERT_EQ(teptris_plan_result_integer_at(e, 0, &id), TEPTRIS_OK);
    EXPECT_EQ(id, 1);
    teptris_plan_result_view_free(e);
}

} // namespace
