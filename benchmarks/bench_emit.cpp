/* Emit-side benchmark: parse each corpus file once (untimed), then
 * time teptris_document_emit repeatedly. Same JSON schema as
 * bench_parse's teptris-only output, so scripts/bench_ab.py compares
 * emit A/Bs unchanged. The dump path had no C-level regression guard
 * before this. */
#include "bench_common.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "teptris/teptris.h"

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
        double min_ms = 0, med_ms = 0;
        size_t out_bytes = 0;
        const char *err = "";
        if (st != TEPTRIS_OK) {
            err = "parse failed";
        } else {
            std::vector<double> es;
            for (int i = 0; i < warmup + reps; i++) {
                char *buf = NULL;
                size_t len = 0;
                double t1 = now_ms();
                teptris_status est = teptris_document_emit(doc, &buf, &len);
                double t2 = now_ms();
                if (est != TEPTRIS_OK) {
                    err = "emit failed";
                    free(buf);
                    break;
                }
                free(buf);
                out_bytes = len;
                if (i >= warmup) {
                    es.push_back(t2 - t1);
                }
            }
            if (es.size() == (size_t)reps) {
                std::sort(es.begin(), es.end());
                min_ms = es.front();
                med_ms = es[es.size() / 2];
            } else if (err[0] == '\0') {
                err = "incomplete run";
            }
        }
        double mbps = (err[0] == '\0' && min_ms > 0)
                          ? (double)c.data.size() / (1024.0 * 1024.0) /
                                (min_ms / 1000.0)
                          : 0.0;
        printf("%s{\"name\":\"%s\",\"bytes\":%zu,\"libs\":{\"teptris\":"
               "{\"ok\":%s,\"emit_min_ms\":%.4f,\"emit_med_ms\":%.4f,"
               "\"emit_bytes\":%zu,\"mb_per_s\":%.1f,\"error\":\"%s\"}}}",
               first_file ? "" : ",", c.name.c_str(), c.data.size(),
               err[0] == '\0' ? "true" : "false", min_ms, med_ms, out_bytes,
               mbps, err);
        first_file = false;
        if (doc != NULL) {
            teptris_document_free(doc);
        }
    }
    printf("]}\n");
    return 0;
}
