#include "bench_common.hpp"
#include "bench_paths.h"

#ifdef HAVE_CPPTOML

#include <memory>
#include <sstream>

#include <cpptoml.h>

bool run_cpptoml(const BenchCase &c, BenchIter &it, size_t &top_keys,
                 std::string &err)
{
    /* cpptoml parses from a stream; the per-iteration stream copy is timed
     * with the parse (ledgered; <2% of parse cost). */
    double t0 = now_ms();
    std::istringstream iss(c.data);
    std::shared_ptr<cpptoml::table> t;
    try {
        cpptoml::parser p(iss);
        t = p.parse();
    } catch (const std::exception &e) {
        double t1 = now_ms();
        err = e.what();
        it.parse_ms = t1 - t0;
        return false;
    }
    double t1 = now_ms();
    top_keys = (size_t)std::distance(t->begin(), t->end());

    double d0 = now_ms();
    t.reset();
    double d1 = now_ms();
    it.parse_ms = t1 - t0;
    it.destroy_ms = d1 - d0;
    return true;
}

#endif /* HAVE_CPPTOML */
