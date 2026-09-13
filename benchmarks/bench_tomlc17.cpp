#include "bench_common.hpp"
#include "bench_paths.h"

#ifdef HAVE_TOMLC17

/* tomlc99 exports toml_parse/toml_free with different signatures; the
 * tomlc17 target renames them (benchmarks/CMakeLists.txt) — mirror here. */
#define toml_parse tepc17_parse
#define toml_parse_file tepc17_parse_file
#define toml_free tepc17_free

extern "C" {
#include "tomlc17.h"
}

bool run_tomlc17(const BenchCase &c, BenchIter &it, size_t &top_keys,
                 std::string &err)
{
    double t0 = now_ms();
    toml_result_t r = toml_parse(c.data.data(), (int)c.data.size());
    double t1 = now_ms();
    if (!r.ok) {
        err = r.errmsg;
        it.parse_ms = t1 - t0;
        toml_free(r);
        return false;
    }
    top_keys = 0; /* no key-count API; the ok flag is the validity signal */
    toml_free(r);
    double t2 = now_ms();
    it.parse_ms = t1 - t0;
    it.destroy_ms = t2 - t1;
    return true;
}

#endif /* HAVE_TOMLC17 */
