/* Plan-walk coverage (teptris#46): build a plan, walk a document,
 * verify skip/missing/nested/collection/raw semantics by row index. */
#include <cstdio>
#include <cstring>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

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

/* A plan deeper than TEPTRIS_PLAN_MAX_DEPTH is rejected at build —
 * walk_table recurses per NESTED level, so an unbounded hostile plan
 * would crash the host's stack. */
TEST(PlanDepth, DeepPlanIsRejected)
{
    /* a valid chain: plan i's row -> plan i+1; depth = 600 > 512 */
    const uint32_t count = 601;
    std::vector<teptris_plan_row> rows;
    std::vector<uint32_t> first;
    first.push_back(0);
    for (uint32_t i = 0; i < count - 1; i++) {
        rows.push_back({"k", TEPTRIS_PLAN_NESTED, i + 1});
        first.push_back((uint32_t)rows.size());
    }
    rows.push_back({"leaf", TEPTRIS_PLAN_SCALAR, 0});
    first.push_back((uint32_t)rows.size());

    teptris_plan_spec spec = {TEPTRIS_PLAN_ABI_VERSION, count, rows.data(),
                              first.data()};
    teptris_status st = TEPTRIS_OK;
    teptris_plan *p = teptris_plan_build(&spec, &st);
    EXPECT_EQ(p, nullptr);
    EXPECT_EQ(st, TEPTRIS_ERR_ARG);
}

TEST(PlanDepth, ShallowPlanStillBuilds)
{
    const uint32_t count = 9; /* depth 8, well under the cap */
    std::vector<teptris_plan_row> rows;
    std::vector<uint32_t> first;
    first.push_back(0);
    for (uint32_t i = 0; i < count - 1; i++) {
        rows.push_back({"k", TEPTRIS_PLAN_NESTED, i + 1});
        first.push_back((uint32_t)rows.size());
    }
    rows.push_back({"leaf", TEPTRIS_PLAN_SCALAR, 0});
    first.push_back((uint32_t)rows.size());

    teptris_plan_spec spec = {TEPTRIS_PLAN_ABI_VERSION, count, rows.data(),
                              first.data()};
    teptris_status st = TEPTRIS_OK;
    teptris_plan *p = teptris_plan_build(&spec, &st);
    EXPECT_NE(p, nullptr);
    EXPECT_EQ(st, TEPTRIS_OK);
    teptris_plan_free(p);
}

TEST(PlanDepth, BuildWritesNothingToStderr)
{
    /* a library never writes to stderr on its normal paths — a debug
     * fprintf shipped inside the depth gate (2026-10-18) and spammed
     * every binding host per plan row */
    const uint32_t count = 9;
    std::vector<teptris_plan_row> rows;
    std::vector<uint32_t> first;
    first.push_back(0);
    for (uint32_t i = 0; i < count - 1; i++) {
        rows.push_back({"k", TEPTRIS_PLAN_NESTED, i + 1});
        first.push_back((uint32_t)rows.size());
    }
    rows.push_back({"leaf", TEPTRIS_PLAN_SCALAR, 0});
    first.push_back((uint32_t)rows.size());
    teptris_plan_spec spec = {TEPTRIS_PLAN_ABI_VERSION, count, rows.data(),
                              first.data()};

    fflush(stderr);
    int saved = dup(fileno(stderr));
    ASSERT_NE(saved, -1);
    FILE *cap = tmpfile();
    ASSERT_NE(cap, nullptr);
    dup2(fileno(cap), fileno(stderr));

    teptris_status st = TEPTRIS_OK;
    teptris_plan *p = teptris_plan_build(&spec, &st);
    fflush(stderr);

    /* the reject path (601-plan chain) must be silent too */
    const uint32_t deep = 601;
    std::vector<teptris_plan_row> drows;
    std::vector<uint32_t> dfirst;
    dfirst.push_back(0);
    for (uint32_t i = 0; i < deep - 1; i++) {
        drows.push_back({"k", TEPTRIS_PLAN_NESTED, i + 1});
        dfirst.push_back((uint32_t)drows.size());
    }
    drows.push_back({"leaf", TEPTRIS_PLAN_SCALAR, 0});
    dfirst.push_back((uint32_t)drows.size());
    teptris_plan_spec dspec = {TEPTRIS_PLAN_ABI_VERSION, deep, drows.data(),
                               dfirst.data()};
    teptris_status dst = TEPTRIS_OK;
    teptris_plan *dp = teptris_plan_build(&dspec, &dst);
    fflush(stderr);

    dup2(saved, fileno(stderr));
    close(saved);

    EXPECT_NE(p, nullptr);
    EXPECT_EQ(st, TEPTRIS_OK);
    EXPECT_EQ(dp, nullptr);
    EXPECT_EQ(dst, TEPTRIS_ERR_ARG);

    fseek(cap, 0, SEEK_END);
    EXPECT_EQ(ftell(cap), 0L);
    fclose(cap);
    teptris_plan_free(p);
}

TEST(PlanSurface, EveryAccessorNamed)
{
    /* E3: the introspection surface named as a unit - the typed
     * accessors' mismatch paths return non-OK, never crash; the row
     * introspection reads a built plan's spec rows. */
    const uint32_t count = 3;
    std::vector<teptris_plan_row> rows = {
        {"a", TEPTRIS_PLAN_SCALAR, 0},
        {"t", TEPTRIS_PLAN_NESTED, 2},
        {"x", TEPTRIS_PLAN_SCALAR, 0},
    };
    std::vector<uint32_t> first = {0, 1, 2, 3};
    teptris_plan_spec spec = {TEPTRIS_PLAN_ABI_VERSION, count, rows.data(),
                              first.data()};
    teptris_status st = TEPTRIS_OK;
    teptris_plan *p = teptris_plan_build(&spec, &st);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(st, TEPTRIS_OK);

    /* plan 0 holds only 'a'; 't' (nested) is plan 1's row; 'x' is
     * plan 2's row (the nested sub the t row points at) */
    EXPECT_EQ(teptris_plan_row_count(p, 0), 1u);
    EXPECT_EQ(teptris_plan_row_kind_at(p, 0, 0), TEPTRIS_PLAN_SCALAR);
    EXPECT_EQ(teptris_plan_row_kind_at(p, 1, 0), TEPTRIS_PLAN_NESTED);
    EXPECT_EQ(teptris_plan_row_sub_at(p, 1, 0), 2u);
    size_t namelen = 99;
    const char *nm = teptris_plan_row_name_at(p, 0, 0);
    EXPECT_NE(nm, nullptr);
    namelen = strlen(nm);
    EXPECT_EQ(namelen, 1u);

    /* a walked result: every typed accessor + mismatch paths */
    teptris_document *doc = nullptr;
    const char *srcdoc = "a = 7\n[t]\nx = \"s\"\n";
    st = teptris_parse(srcdoc, strlen(srcdoc), nullptr, &doc);
    teptris_plan_result *res =
        teptris_plan_walk(p, teptris_document_root(doc), &st);
    ASSERT_NE(res, nullptr);

    EXPECT_EQ(teptris_plan_result_kind_at(res, 0), TEPTRIS_PLAN_SCALAR);
    EXPECT_EQ(teptris_plan_result_value_kind_at(res, 0), TEPTRIS_INTEGER);
    teptris_view v;
    EXPECT_EQ(teptris_plan_result_string_at(res, 0, &v), TEPTRIS_ERR_ARG);
    int64_t iv = 0;
    EXPECT_EQ(teptris_plan_result_integer_at(res, 0, &iv), TEPTRIS_OK);
    EXPECT_EQ(iv, 7);
    double fv = 0;
    EXPECT_EQ(teptris_plan_result_float_at(res, 0, &fv), TEPTRIS_ERR_ARG);
    bool bv = true;
    EXPECT_EQ(teptris_plan_result_boolean_at(res, 0, &bv), TEPTRIS_ERR_ARG);
    teptris_datetime dt;
    EXPECT_EQ(teptris_plan_result_datetime_at(res, 0, &dt),
              TEPTRIS_ERR_ARG);
    (void)teptris_plan_result_raw_at(res, 0);
    EXPECT_EQ(teptris_plan_result_array_len_at(res, 0), 0u);
    (void)teptris_plan_result_array_value_kind_at(res, 0, 0);
    (void)teptris_plan_result_array_kind_at(res, 0, 0);
    EXPECT_EQ(teptris_plan_result_array_string_at(res, 0, 0, &v),
              TEPTRIS_ERR_ARG);
    EXPECT_EQ(teptris_plan_result_array_integer_at(res, 0, 0, &iv),
              TEPTRIS_ERR_ARG);
    EXPECT_EQ(teptris_plan_result_array_float_at(res, 0, 0, &fv),
              TEPTRIS_ERR_ARG);
    EXPECT_EQ(teptris_plan_result_array_boolean_at(res, 0, 0, &bv),
              TEPTRIS_ERR_ARG);
    EXPECT_EQ(teptris_plan_result_array_datetime_at(res, 0, 0, &dt),
              TEPTRIS_ERR_ARG);
    /* the borrowed sub-result view exists only for collection rows;
     * a scalar row has nothing to view */
    teptris_plan_result *view = teptris_plan_result_row_view(res, 0);
    EXPECT_EQ(view, nullptr);
    if (view != nullptr) {
        teptris_plan_result_free(view);
    }
    teptris_plan_result_free(res);
    teptris_document_free(doc);
    teptris_plan_free(p);
}
