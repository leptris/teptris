/* Schema-side benchmark: compile throughput (schema JSON bytes/sec)
 * and validate throughput (TOML bytes/sec against a compiled schema).
 * teptris-only tier — no C competitor ships a JSON Schema engine; the
 * cross-engine numbers live in the tombi AB section of LEDGER.md. The
 * compile cell recompiles from scratch each iteration (arena + memo +
 * property tables); the validate cell compiles once, then times
 * validate_doc over the parsed corpus. Same CLI arg convention as
 * bench_emit: <corpus-dir> [rounds]. */
#include "bench_common.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "teptris/schema.h"
#include "teptris/teptris.h"

struct schema_stat {
    double min_ms = 0, med_ms = 0;
    double mb_per_s = 0;
    size_t bytes = 0;
};

static schema_stat time_compile(const char *json, size_t len, int rounds)
{
    schema_stat st;
    std::vector<double> ms;
    for (int i = 0; i < rounds; i++) {
        teptris_schema *sch = nullptr;
        char *diag = nullptr;
        size_t dlen = 0;
        double t0 = now_ms();
        teptris_status s = teptris_schema_compile(json, len, &sch, &diag,
                                                  &dlen);
        double t1 = now_ms();
        teptris_diag_free(diag);
        if (s != TEPTRIS_OK) {
            teptris_schema_free(sch);
            continue;
        }
        teptris_schema_free(sch);
        ms.push_back(t1 - t0);
    }
    if (ms.empty()) return st;
    std::sort(ms.begin(), ms.end());
    st.min_ms = ms.front();
    st.med_ms = ms[ms.size() / 2];
    st.bytes = len;
    st.mb_per_s = (double)len / 1048576.0 / (st.med_ms / 1000.0);
    return st;
}

static schema_stat time_validate(const BenchCase &c, const char *json,
                                 size_t jlen, int rounds)
{
    schema_stat st;
    teptris_schema *sch = nullptr;
    char *diag = nullptr;
    size_t dlen = 0;
    if (teptris_schema_compile(json, jlen, &sch, &diag, &dlen) !=
        TEPTRIS_OK) {
        teptris_diag_free(diag);
        return st;
    }
    teptris_diag_free(diag);
    teptris_document *doc = nullptr;
    if (teptris_parse(c.data.data(), c.data.size(), nullptr, &doc) !=
        TEPTRIS_OK) {
        teptris_schema_free(sch);
        return st;
    }
    std::vector<double> ms;
    for (int i = 0; i < rounds; i++) {
        char *buf = nullptr;
        size_t blen = 0;
        double t0 = now_ms();
        teptris_status s = teptris_schema_validate_doc(sch, doc, &buf, &blen);
        double t1 = now_ms();
        free(buf);
        if (s != TEPTRIS_OK) break;
        ms.push_back(t1 - t0);
    }
    teptris_document_free(doc);
    teptris_schema_free(sch);
    if (ms.empty()) return st;
    std::sort(ms.begin(), ms.end());
    st.min_ms = ms.front();
    st.med_ms = ms[ms.size() / 2];
    st.bytes = c.data.size();
    st.mb_per_s = (double)c.data.size() / 1048576.0 / (st.med_ms / 1000.0);
    return st;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: bench_schema <corpus-dir> [rounds]\n");
        return 2;
    }
    int rounds = argc > 2 ? atoi(argv[2]) : 15;
    auto cases = load_corpus(argv[1]);
    if (cases.empty()) {
        fprintf(stderr, "no corpus files in %s\n", argv[1]);
        return 2;
    }

    /* The compile corpus: the vendored metaschemas are real, large
     * schema documents shipping in the binary. */
    static const char *compile_json =
        "{\"$schema\":\"https://json-schema.org/draft/2020-12/schema\","
        "\"type\":\"object\",\"properties\":{";

    /* Build a scaled schema (2k properties with patterns) so the
     * compile cell measures megabytes, not microseconds. */
    std::string big = compile_json;
    for (int i = 0; i < 2000; i++) {
        char one[256];
        snprintf(one, sizeof one,
                 "\"field_%d\":{\"type\":\"object\",\"properties\":{"
                 "\"name\":{\"type\":\"string\",\"pattern\":\"^f%d-[a-z]+$\"},"
                 "\"count\":{\"type\":\"integer\",\"minimum\":0}}},",
                 i, i);
        big += one;
    }
    big += "\"tail\":{\"type\":\"boolean\"}}}";

    schema_stat comp = time_compile(big.data(), big.size(), rounds);
    printf("{\"schema_compile\": {\"bytes\": %zu, \"min_ms\": %.3f, "
           "\"med_ms\": %.3f, \"mb_per_s\": %.1f}}\n",
           comp.bytes, comp.min_ms, comp.med_ms, comp.mb_per_s);

    /* validate tier: a schema over the corpus shape, validated against
     * every corpus file (parse untimed, validate timed) */
    static const char *val_schema =
        "{\"type\":\"object\",\"properties\":{"
        "\"name\":{\"type\":\"string\"},"
        "\"tags\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}},"
        "\"port\":{\"type\":\"integer\",\"minimum\":0},"
        "\"ratio\":{\"type\":\"number\"},\"on\":{\"type\":\"boolean\"},"
        "\"dob\":{\"type\":\"string\",\"format\":\"date\"}},"
        "\"additionalProperties\":true}";
    printf("{\"schema_validate\": [\n");
    bool first = true;
    for (const auto &c : cases) {
        schema_stat st = time_validate(c, val_schema, strlen(val_schema),
                                       rounds);
        if (!first) printf(",\n");
        first = false;
        printf("  {\"name\": \"%s\", \"bytes\": %zu, \"min_ms\": %.3f, "
               "\"med_ms\": %.3f, \"mb_per_s\": %.1f}",
               c.name.c_str(), st.bytes, st.min_ms, st.med_ms, st.mb_per_s);
    }
    printf("\n]}\n");
    return 0;
}
