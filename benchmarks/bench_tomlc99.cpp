#include "bench_common.hpp"
#include "bench_paths.h"

#ifdef HAVE_TOMLC99

#include <vector>

#include "toml.h"

bool run_tomlc99(const BenchCase &c, BenchIter &it, size_t &top_keys,
                 std::string &err)
{
    /* tomlc99 requires a mutable NUL-terminated buffer; the per-iteration
     * memcpy is timed with the parse (ledgered; <2% of parse cost). */
    std::vector<char> buf(c.data.size() + 1);
    double t0 = now_ms();
    memcpy(buf.data(), c.data.data(), c.data.size());
    buf[c.data.size()] = '\0';
    char ebuf[512];
    ebuf[0] = '\0';
    toml_table_t *t = toml_parse(buf.data(), ebuf, (int)sizeof(ebuf));
    double t1 = now_ms();
    if (t == NULL) {
        err = ebuf[0] ? ebuf : "parse failed";
        return false;
    }
    top_keys = (size_t)(toml_table_nkval(t) + toml_table_narr(t) +
                        toml_table_ntab(t));
    toml_free(t);
    double t2 = now_ms();
    it.parse_ms = t1 - t0;
    it.destroy_ms = t2 - t1;
    return true;
}

#endif /* HAVE_TOMLC99 */
