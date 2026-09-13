#include "teptris/teptris.h"

#include "util.hpp"

/* Inline v1 corpus: valid cases with expected typed-JSON trees (insertion
 * order, compact), and invalid cases that must produce an error status.
 * Offline floor for TODO.impl/07 (toml-test runner lands with corpora
 * fetch). */

TEST(Corpus, Valid)
{
    struct {
        const char *in;
        const char *json;
    } cases[] = {
        {"a = 1", R"({"a":{"type":"integer","value":"1"}})"},
        {"a = -1", R"({"a":{"type":"integer","value":"-1"}})"},
        {"a = 1.5", R"({"a":{"type":"float","value":"1.5"}})"},
        {"a = 'str'", R"({"a":{"type":"string","value":"str"}})"},
        {"a = true", R"({"a":{"type":"bool","value":"true"}})"},
        {"a = [1, 2]",
         R"({"a":[{"type":"integer","value":"1"},{"type":"integer","value":"2"}]})"},
        {"[t]\nx = 1", R"({"t":{"x":{"type":"integer","value":"1"}}})"},
        {"[t]\n", R"({"t":{}})"},
        {"a.b = 1", R"({"a":{"b":{"type":"integer","value":"1"}}})"},
        {"[[t]]\nx = 1\n[[t]]\nx = 2",
         R"({"t":[{"x":{"type":"integer","value":"1"}},{"x":{"type":"integer","value":"2"}}]})"},
        {"o = {n = \"v\"}", R"({"o":{"n":{"type":"string","value":"v"}}})"},
        {"d = 1979-05-27",
         R"({"d":{"type":"date-local","value":"1979-05-27"}})"},
        {"d = 07:32:00",
         R"({"d":{"type":"time-local","value":"07:32:00"}})"},
        {"d = 1979-05-27T07:32:00Z",
         R"({"d":{"type":"datetime","value":"1979-05-27T07:32:00Z"}})"},
        {"i = inf", R"({"i":{"type":"float","value":"inf"}})"},
        {"n = nan", R"({"n":{"type":"float","value":"nan"}})"},
        {"h = 0x10", R"({"h":{"type":"integer","value":"16"}})"},
        {"u = 1_2", R"({"u":{"type":"integer","value":"12"}})"},
        {"s = \"\\u00e9\"", R"({"s":{"type":"string","value":"é"}})"},
        {"e = ''", R"({"e":{"type":"string","value":""}})"},
        {"\"quoted key\" = 1",
         R"({"quoted key":{"type":"integer","value":"1"}})"},
        {"m = [0.1, true, 'x']", R"({"m":[{"type":"float","value":"0.1"},{"type":"bool","value":"true"},{"type":"string","value":"x"}]})"},
        {"[a]\nx = 1\n[a.b]\ny = 2",
         R"({"a":{"x":{"type":"integer","value":"1"},"b":{"y":{"type":"integer","value":"2"}}}})"},
        {"w = 1979-05-27 07:32:00Z",
         R"({"w":{"type":"datetime","value":"1979-05-27T07:32:00Z"}})"},
    };
    for (auto &c : cases) {
        teptris_document *doc = parse_ok(c.in);
        ASSERT_NE(doc, nullptr);
        DocGuard g(doc);
        EXPECT_EQ(emit_json(doc), c.json) << "input: " << c.in;
    }
}

TEST(Corpus, Invalid)
{
    const char *bad[] = {
        "a =",
        "a = = 1",
        "= 1",
        "[",
        "[]",
        "[a",
        "[[a]",
        "a = 'x",
        "a = \"x",
        "a = [1 2]",
        "a = {x = 1 y = 2}",
        "[a]\n[a]",
        "a = 1\na = 1",
        "a = 1e",
        "a = 0b2",
        "a = 0o8",
        "a = 0xG",
        "# comment\x7f",
        "a\x01 = 1",
        "a = 1979-05-27T07:32:00+07:0",
        "a = 12:00",
        "a = tru",
        "a = +nan trailing",
        "\ta = 1\rb = 2",
        "a = \"\\x41\"",
        "a = \"\\u00\"",
        "a = \"\\uD800\"",
        "a = \"\\U00110000\"",
        "a = \"\"\"unterminated",
    };
    for (const char *in : bad) {
        teptris_document *doc = nullptr;
        teptris_status st = parse_status(in, &doc);
        EXPECT_NE(st, TEPTRIS_OK) << "input: " << in;
        DocGuard g(doc);
    }
}
