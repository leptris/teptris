#include "bench_common.hpp"
#include "bench_paths.h"

#include <cstdlib>
#include <cstring>

#include "teptris/teptris.h"

#ifdef HAVE_TOMLC99
bool run_tomlc99(const BenchCase &c, BenchIter &it, size_t &top_keys,
                 std::string &err);
#endif
#ifdef HAVE_TOML11
bool run_toml11(const BenchCase &c, BenchIter &it, size_t &top_keys,
                std::string &err);
#endif
#ifdef HAVE_TOMLPLUS
bool run_tomlplus(const BenchCase &c, BenchIter &it, size_t &top_keys,
                  std::string &err);
#endif
#ifdef HAVE_TOMLC17
bool run_tomlc17(const BenchCase &c, BenchIter &it, size_t &top_keys,
                 std::string &err);
#endif
#ifdef HAVE_CPPTOML
bool run_cpptoml(const BenchCase &c, BenchIter &it, size_t &top_keys,
                 std::string &err);
#endif

static bool run_teptris(const BenchCase &c, BenchIter &it, size_t &top_keys,
                        std::string &err)
{
    teptris_document *doc = NULL;
    double t1 = now_ms();
    teptris_status st = teptris_parse(c.data.data(), c.data.size(), NULL, &doc);
    double t2 = now_ms();
    if (st != TEPTRIS_OK) {
        const teptris_error *e = teptris_document_error(doc);
        err = e->message;
        teptris_document_free(doc);
        return false;
    }
    top_keys = teptris_node_table_length(teptris_document_root(doc));

    char *buf = NULL;
    size_t blen = 0;
    if (teptris_document_emit(doc, &buf, &blen) == TEPTRIS_OK) {
        free(buf);
    }
    double t3 = now_ms();
    teptris_document_free(doc);
    double t4 = now_ms();

    it.parse_ms = t2 - t1;
    it.emit_ms = t3 - t2;
    it.destroy_ms = t4 - t3;
    return true;
}

struct LibEntry {
    const char *name;
    RunFn fn;
};

// optional 4th argv: comma-separated lib filter ("teptris",
// "teptris,tomlc99", ...). The CI A/B rounds interleave teptris-only
// base-vs-head — competitors are pinned sources identical in both
// trees, so the lane measures them once in a single full-matrix sweep
// instead of in every round of both trees.
static bool selected(const std::string &only, const char *name)
{
    if (only.empty()) return true;
    std::string set = "," + only + ",";
    return set.find("," + std::string(name) + ",") != std::string::npos;
}

int main(int argc, char **argv)
{
    std::string dir = (argc > 1) ? argv[1] : "bench-corpus";
    int warmup = (argc > 2) ? atoi(argv[2]) : 2;
    int reps = (argc > 3) ? atoi(argv[3]) : 30;
    std::string only = (argc > 4) ? argv[4] : "";

    std::vector<LibEntry> libs;
    if (selected(only, "teptris")) libs.push_back({"teptris", run_teptris});
#ifdef HAVE_TOMLC99
    if (selected(only, "tomlc99")) libs.push_back({"tomlc99", run_tomlc99});
#endif
#ifdef HAVE_TOML11
    if (selected(only, "toml11")) libs.push_back({"toml11", run_toml11});
#endif
#ifdef HAVE_TOMLPLUS
    if (selected(only, "tomlplusplus")) libs.push_back({"tomlplusplus", run_tomlplus});
#endif
#ifdef HAVE_TOMLC17
    if (selected(only, "tomlc17")) libs.push_back({"tomlc17", run_tomlc17});
#endif
#ifdef HAVE_CPPTOML
    if (selected(only, "cpptoml")) libs.push_back({"cpptoml", run_cpptoml});
#endif
    if (libs.empty()) {
        fprintf(stderr, "lib filter \"%s\" matched nothing\n", only.c_str());
        return 1;
    }

    std::vector<BenchCase> cases = load_corpus(dir);
    if (cases.empty()) {
        fprintf(stderr, "no corpus files in %s (run scripts/gen_bench_corpus.py)\n",
                dir.c_str());
        return 1;
    }

    printf("{\n  \"files\": [\n");
    for (size_t ci = 0; ci < cases.size(); ci++) {
        const BenchCase &c = cases[ci];
        printf("    {\"name\": \"%s\", \"bytes\": %zu, \"libs\": {\n", c.name.c_str(),
               c.data.size());
        for (size_t li = 0; li < libs.size(); li++) {
            BenchResult r = bench_case(c, libs[li].fn, warmup, reps);
            double mbps = 0;
            if (r.ok && r.parse_min > 0) {
                mbps = (double)c.data.size() / (1024.0 * 1024.0) / (r.parse_min / 1000.0);
            }
            printf("      \"%s\": {\"ok\": %s, \"parse_min_ms\": %.4f, "
                   "\"parse_med_ms\": %.4f, \"destroy_min_ms\": %.4f, "
                   "\"emit_min_ms\": %.4f, \"mb_per_s\": %.1f, "
                   "\"top_keys\": %zu, \"error\": \"%s\"}%s\n",
                   libs[li].name, r.ok ? "true" : "false", r.parse_min,
                   r.parse_med, r.destroy_min, r.emit_min, mbps, r.top_keys,
                   r.error.c_str(), li + 1 < libs.size() ? "," : "");
        }
        printf("    }}%s\n", ci + 1 < cases.size() ? "," : "");
    }
    printf("  ]\n}\n");
    return 0;
}
