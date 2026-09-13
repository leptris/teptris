#include "bench_common.hpp"
#include "bench_paths.h"

#ifdef HAVE_TOML11

#include <vector>

#include TOML11_HEADER

bool run_toml11(const BenchCase &c, BenchIter &it, size_t &top_keys,
                std::string &err)
{
    /* toml11 parses from an owning byte vector; the per-iteration copy is
     * timed with the parse (ledgered; <2% of parse cost). */
    double t0 = now_ms();
    std::vector<unsigned char> bytes(c.data.begin(), c.data.end());
    auto *v = new toml::value();
    try {
        *v = toml::parse(bytes, "bench");
    } catch (const std::exception &e) {
        double t1 = now_ms();
        err = e.what();
        delete v;
        it.parse_ms = t1 - t0;
        return false;
    }
    double t1 = now_ms();
    top_keys = v->as_table().size();

    double e0 = now_ms();
    std::string out = toml::format(*v);
    double e1 = now_ms();
    if (out.empty()) {
        /* format still ran; nothing to assert */
    }

    delete v;
    double t2 = now_ms();
    it.parse_ms = t1 - t0;
    it.emit_ms = e1 - e0;
    it.destroy_ms = t2 - e1;
    return true;
}

#endif /* HAVE_TOML11 */
