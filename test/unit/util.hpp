#ifndef TEPTRIS_TEST_UTIL_HPP
#define TEPTRIS_TEST_UTIL_HPP

#include <cstring>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "teptris/teptris.h"

struct DocGuard {
    teptris_document *doc;
    explicit DocGuard(teptris_document *d) : doc(d) {}
    ~DocGuard()
    {
        teptris_document_free(doc);
    }
};

inline teptris_document *parse_ok(const std::string &toml)
{
    /* Key views point into the parse input (zero-copy, by design), and
     * tests pass temporaries — keep every input alive for the run. */
    static std::vector<std::string> keep;
    keep.push_back(toml);
    teptris_document *doc = nullptr;
    teptris_status st = teptris_parse(keep.back().data(), keep.back().size(),
                                      nullptr, &doc);
    if (st != TEPTRIS_OK) {
        const teptris_error *e = teptris_document_error(doc);
        ADD_FAILURE() << "parse failed: " << e->message << " at " << e->line
                      << ":" << e->column << " for input: " << toml;
        teptris_document_free(doc);
        return nullptr;
    }
    return doc;
}

inline teptris_status parse_status(const std::string &toml,
                                   teptris_document **doc)
{
    return teptris_parse(toml.data(), toml.size(), nullptr, doc);
}

inline std::string emit_json(teptris_document *doc)
{
    char *buf = nullptr;
    size_t len = 0;
    if (teptris_document_emit_json(doc, &buf, &len) != TEPTRIS_OK) {
        return "<emit-failed>";
    }
    std::string s(buf, len);
    free(buf);
    return s;
}

inline std::string emit_toml(teptris_document *doc)
{
    char *buf = nullptr;
    size_t len = 0;
    if (teptris_document_emit(doc, &buf, &len) != TEPTRIS_OK) {
        return "<emit-failed>";
    }
    std::string s(buf, len);
    free(buf);
    return s;
}

inline const teptris_node *get(const teptris_node *t, const char *key)
{
    return teptris_node_table_get(t, key, std::strlen(key));
}

inline std::string str(const teptris_view &v)
{
    return std::string(v.ptr, v.len);
}

/* Order-insensitive JSON equality: table key order is not a TOML semantic
 * (the emitter canonicalizes header order), so typed-JSON trees compare
 * with objects as unordered maps and arrays as ordered lists. */
namespace teptris_test {

struct JValue {
    bool is_obj = false;
    bool is_arr = false;
    std::string scalar; /* raw token */
    std::vector<std::pair<std::string, JValue>> members;
    std::vector<JValue> items;
};

inline const char *jws(const char *p)
{
    while (*p == ' ' || *p == '\n' || *p == '\t' || *p == '\r') {
        p++;
    }
    return p;
}

inline const char *jparse(const char *p, JValue *out);

inline const char *jstring(const char *p, std::string *out)
{
    p++; /* '"' */
    while (*p && *p != '"') {
        if (*p == '\\' && p[1]) {
            out->push_back(*p++);
        }
        out->push_back(*p++);
    }
    return (*p == '"') ? p + 1 : p;
}

inline const char *jparse(const char *p, JValue *out)
{
    p = jws(p);
    if (*p == '{') {
        out->is_obj = true;
        p = jws(p + 1);
        if (*p == '}') {
            return p + 1;
        }
        for (;;) {
            std::string key;
            p = jws(jstring(p, &key));
            if (*p == ':') {
                p++;
            }
            JValue v;
            p = jparse(p, &v);
            out->members.emplace_back(key, std::move(v));
            p = jws(p);
            if (*p == ',') {
                p++;
                continue;
            }
            break;
        }
        return (*p == '}') ? p + 1 : p;
    }
    if (*p == '[') {
        out->is_arr = true;
        p = jws(p + 1);
        if (*p == ']') {
            return p + 1;
        }
        for (;;) {
            JValue v;
            p = jparse(p, &v);
            out->items.push_back(std::move(v));
            p = jws(p);
            if (*p == ',') {
                p++;
                continue;
            }
            break;
        }
        return (*p == ']') ? p + 1 : p;
    }
    const char *start = p;
    while (*p && *p != ',' && *p != '}' && *p != ']') {
        p++;
    }
    out->scalar = std::string(start, (size_t)(p - start));
    return p;
}

inline bool jeq(const JValue &a, const JValue &b)
{
    if (a.is_obj != b.is_obj || a.is_arr != b.is_arr) {
        return false;
    }
    if (a.is_obj) {
        if (a.members.size() != b.members.size()) {
            return false;
        }
        for (auto &kv : a.members) {
            bool found = false;
            for (auto &kv2 : b.members) {
                if (kv2.first == kv.first && jeq(kv.second, kv2.second)) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                return false;
            }
        }
        return true;
    }
    if (a.is_arr) {
        if (a.items.size() != b.items.size()) {
            return false;
        }
        for (size_t i = 0; i < a.items.size(); i++) {
            if (!jeq(a.items[i], b.items[i])) {
                return false;
            }
        }
        return true;
    }
    return a.scalar == b.scalar;
}

inline bool json_semantically_equal(const std::string &a, const std::string &b)
{
    JValue va, vb;
    jparse(a.c_str(), &va);
    jparse(b.c_str(), &vb);
    return jeq(va, vb);
}

} // namespace teptris_test

#endif /* TEPTRIS_TEST_UTIL_HPP */
