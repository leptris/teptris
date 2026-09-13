#include "bench_common.hpp"
#include "bench_paths.h"

#ifdef HAVE_TOMLPLUS

#include <sstream>

#include TOMLPLUS_HEADER

bool run_tomlplus(const BenchCase &c, BenchIter &it, size_t &top_keys,
                  std::string &err)
{
    double t0 = now_ms();
    auto *r = new toml::parse_result();
    try {
        *r = toml::parse(std::string_view(c.data.data(), c.data.size()));
    } catch (const toml::parse_error &e) {
        double t1 = now_ms();
        err = e.what();
        delete r;
        it.parse_ms = t1 - t0;
        return false;
    }
    double t1 = now_ms();
    top_keys = r->size();

    double e0 = now_ms();
    std::ostringstream oss;
    oss << toml::toml_formatter{*r};
    std::string out = oss.str();
    double e1 = now_ms();
    if (out.empty()) {
        /* formatter still ran; nothing to assert */
    }

    delete r;
    double t2 = now_ms();
    it.parse_ms = t1 - t0;
    it.emit_ms = e1 - e0;
    it.destroy_ms = t2 - e1;
    return true;
}

#endif /* HAVE_TOMLPLUS */
