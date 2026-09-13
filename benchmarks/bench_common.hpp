#ifndef TEPTRIS_BENCH_COMMON_HPP
#define TEPTRIS_BENCH_COMMON_HPP

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <vector>

struct BenchCase {
    std::string name;
    std::string data;
};

struct BenchIter {
    double parse_ms = 0;
    double destroy_ms = 0;
    double emit_ms = 0;
};

struct BenchResult {
    bool ok = false;
    std::string error;
    double parse_min = 0, parse_med = 0;
    double destroy_min = 0;
    double emit_min = 0;
    size_t top_keys = 0;
    size_t emit_bytes = 0;
};

using RunFn = bool (*)(const BenchCase &c, BenchIter &it, size_t &top_keys,
                       std::string &err);

inline double now_ms()
{
    return std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

inline std::vector<BenchCase> load_corpus(const std::string &dir)
{
    std::vector<BenchCase> cases;
    DIR *d = opendir(dir.c_str());
    if (d == NULL) {
        fprintf(stderr, "cannot open corpus dir %s\n", dir.c_str());
        return cases;
    }
    struct dirent *e;
    std::vector<std::string> names;
    while ((e = readdir(d)) != NULL) {
        std::string n = e->d_name;
        if (n.size() > 5 && n.compare(n.size() - 5, 5, ".toml") == 0) {
            names.push_back(n);
        }
    }
    closedir(d);
    std::sort(names.begin(), names.end());
    for (auto &n : names) {
        FILE *f = fopen((dir + "/" + n).c_str(), "rb");
        if (f == NULL) {
            continue;
        }
        std::string data;
        char buf[1 << 16];
        size_t r;
        while ((r = fread(buf, 1, sizeof(buf), f)) > 0) {
            data.append(buf, r);
        }
        fclose(f);
        cases.push_back(BenchCase{n, data});
    }
    return cases;
}

inline BenchResult bench_case(const BenchCase &c, RunFn fn, int warmup,
                              int reps)
{
    BenchResult res;
    for (int i = 0; i < warmup; i++) {
        BenchIter it;
        size_t top = 0;
        std::string err;
        if (!fn(c, it, top, err)) {
            res.error = err;
            return res;
        }
        res.top_keys = top;
    }
    std::vector<double> ps, ds, es;
    for (int i = 0; i < reps; i++) {
        BenchIter it;
        size_t top = 0;
        std::string err;
        if (!fn(c, it, top, err)) {
            res.error = err;
            return res;
        }
        res.top_keys = top;
        if (it.emit_ms > 0) {
            res.emit_bytes = res.emit_bytes ? res.emit_bytes : (size_t)0;
        }
        ps.push_back(it.parse_ms);
        ds.push_back(it.destroy_ms);
        es.push_back(it.emit_ms);
    }
    std::sort(ps.begin(), ps.end());
    std::sort(ds.begin(), ds.end());
    std::sort(es.begin(), es.end());
    res.ok = true;
    res.parse_min = ps.front();
    res.parse_med = ps[ps.size() / 2];
    res.destroy_min = ds.front();
    res.emit_min = es.empty() ? 0 : es.front();
    return res;
}

#endif /* TEPTRIS_BENCH_COMMON_HPP */
