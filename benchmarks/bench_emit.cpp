/* Emit-side benchmark: parse each corpus file once (untimed), then
 * time every emit mode repeatedly — TOML, conformance JSON, and
 * natural JSON (the 0.3.0 mode real hosts consume). Same JSON schema
 * as bench_parse's teptris-only output, so scripts/bench_ab.py
 * compares emit A/Bs unchanged (it reads mb_per_s; the json modes
 * report under their own keys). The dump path had no C-level
 * regression guard before this. */
#include "bench_common.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "teptris/teptris.h"

using emit_fn = teptris_status (*)(const teptris_document *, char **,
                                   size_t *);

struct emit_stat {
    double min_ms = 0, med_ms = 0;
    size_t bytes = 0;
    const char *err = "";
};

static emit_stat time_emit(const teptris_document *doc, emit_fn fn,
                           int warmup, int reps)
{
    emit_stat st;
    std::vector<double> es;
    for (int i = 0; i < warmup + reps; i++) {
        char *buf = NULL;
        size_t len = 0;
        double t1 = now_ms();
        teptris_status est = fn(doc, &buf, &len);
        double t2 = now_ms();
        if (est != TEPTRIS_OK) {
            st.err = "emit failed";
            free(buf);
            break;
        }
        free(buf);
        st.bytes = len;
        if (i >= warmup) {
            es.push_back(t2 - t1);
        }
    }
    if (es.size() == (size_t)reps) {
        std::sort(es.begin(), es.end());
        st.min_ms = es.front();
        st.med_ms = es[es.size() / 2];
    } else if (st.err[0] == '\0') {
        st.err = "incomplete run";
    }
    return st;
}

int main(int argc, char **argv)
{
    std::string dir = (argc > 1) ? argv[1] : "bench-corpus";
    int warmup = (argc > 2) ? atoi(argv[2]) : 2;
    int reps = (argc > 3) ? atoi(argv[3]) : 30;

    auto cases = load_corpus(dir);
    printf("{\"files\":[");
    bool first_file = true;
    for (auto &c : cases) {
        teptris_document *doc = NULL;
        teptris_status st =
            teptris_parse(c.data.data(), c.data.size(), NULL, &doc);
        emit_stat toml, json, json_nat;
        const char *err = "";
        if (st != TEPTRIS_OK) {
            err = "parse failed";
        } else {
            toml = time_emit(doc, teptris_document_emit, warmup, reps);
            json = time_emit(doc, teptris_document_emit_json, warmup, reps);
            json_nat =
                time_emit(doc, teptris_document_emit_json_natural, warmup,
                          reps);
        }
        const char *mode_err = toml.err[0] ? toml.err
                              : json.err[0] ? json.err
                              : json_nat.err[0] ? json_nat.err : "";
        if (st == TEPTRIS_OK && mode_err[0] != '\0')
            err = mode_err;
        double mbps = (err[0] == '\0' && toml.min_ms > 0)
                          ? (double)c.data.size() / (1024.0 * 1024.0) /
                                (toml.min_ms / 1000.0)
                          : 0.0;
        double json_mbps = (err[0] == '\0' && json.min_ms > 0)
                               ? (double)c.data.size() /
                                     (1024.0 * 1024.0) /
                                     (json.min_ms / 1000.0)
                               : 0.0;
        double json_nat_mbps = (err[0] == '\0' && json_nat.min_ms > 0)
                                   ? (double)c.data.size() /
                                         (1024.0 * 1024.0) /
                                         (json_nat.min_ms / 1000.0)
                                   : 0.0;
        printf("%s{\"name\":\"%s\",\"bytes\":%zu,\"libs\":{\"teptris\":"
               "{\"ok\":%s,\"emit_min_ms\":%.4f,\"emit_med_ms\":%.4f,"
               "\"emit_bytes\":%zu,\"mb_per_s\":%.1f,"
               "\"json_min_ms\":%.4f,\"json_med_ms\":%.4f,"
               "\"json_bytes\":%zu,\"json_mb_per_s\":%.1f,"
               "\"json_nat_min_ms\":%.4f,\"json_nat_med_ms\":%.4f,"
               "\"json_nat_bytes\":%zu,\"json_nat_mb_per_s\":%.1f,"
               "\"error\":\"%s\"}}}",
               first_file ? "" : ",", c.name.c_str(), c.data.size(),
               err[0] == '\0' ? "true" : "false", toml.min_ms, toml.med_ms,
               toml.bytes, mbps, json.min_ms, json.med_ms, json.bytes,
               json_mbps, json_nat.min_ms, json_nat.med_ms, json_nat.bytes,
               json_nat_mbps, err);
        first_file = false;
        if (doc != NULL) {
            teptris_document_free(doc);
        }
    }
    printf("]}\n");
    return 0;
}
