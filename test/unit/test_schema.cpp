/* The schema layer: compile-time contract (unsupported constructs are
 * rejected, never ignored), the four TOML/JSON incompatibility
 * policies, every supported keyword class, the recursive-$ref memo,
 * the error envelope shape, and the validate/validate_doc parity. */
#include <cstdio>
#include <cstring>
#include <string>

#include "teptris/schema.h"
#include "teptris/teptris.h"
#include "util.hpp"

namespace {

struct SchemaDoc {
    teptris_schema *sch = nullptr;
    ~SchemaDoc()
    {
        teptris_schema_free(sch);
    }
};

/* compile helper: returns the compiled schema or nullptr (diag
 * checked by the caller when a failure is expected) */
teptris_schema *compile_ok(const std::string &json)
{
    teptris_schema *sch = nullptr;
    char *diag = nullptr;
    size_t dlen = 0;
    teptris_status st = teptris_schema_compile(json.data(), json.size(), &sch,
                                               &diag, &dlen);
    teptris_diag_free(diag);
    EXPECT_EQ(st, TEPTRIS_OK) << "schema: " << json;
    if (st != TEPTRIS_OK) {
        teptris_schema_free(sch);
        return nullptr;
    }
    return sch;
}

/* compile helper with a base directory: resources resolve relative
 * to it (file-backed), exactly like the CLI */
teptris_schema *compile_ex_ok(const std::string &json,
                              const std::string &base_dir)
{
    teptris_schema *sch = nullptr;
    char *diag = nullptr;
    size_t dlen = 0;
    teptris_status st = teptris_schema_compile_ex(
        json.data(), json.size(), base_dir.data(), base_dir.size(), &sch,
        &diag, &dlen);
    teptris_diag_free(diag);
    EXPECT_EQ(st, TEPTRIS_OK) << "schema: " << json;
    if (st != TEPTRIS_OK) {
        teptris_schema_free(sch);
        return nullptr;
    }
    return sch;
}

std::string compile_diag(const std::string &json)
{
    teptris_schema *sch = nullptr;
    char *diag = nullptr;
    size_t dlen = 0;
    teptris_status st = teptris_schema_compile(json.data(), json.size(), &sch,
                                               &diag, &dlen);
    EXPECT_NE(st, TEPTRIS_OK) << "expected compile failure: " << json;
    EXPECT_EQ(sch, nullptr) << "failed compile must hand back no schema";
    std::string got(diag != nullptr ? diag : "", dlen);
    teptris_diag_free(diag);
    return got;
}

/* validate helper: "" = valid, otherwise the error-list JSON */
std::string validate_str(teptris_schema *sch, const std::string &toml)
{
    char *buf = nullptr;
    size_t blen = 0;
    teptris_status st = teptris_schema_validate(sch, toml.data(), toml.size(),
                                                &buf, &blen);
    EXPECT_EQ(st, TEPTRIS_OK);
    if (st != TEPTRIS_OK) {
        return "<status-failure>";
    }
    std::string got(buf, blen);
    free(buf);
    return got;
}

std::string validate_doc_str(teptris_schema *sch, const std::string &toml)
{
    teptris_document *doc = parse_ok(toml);
    if (doc == nullptr) {
        return "<parse-failure>";
    }
    char *buf = nullptr;
    size_t blen = 0;
    teptris_status st = teptris_schema_validate_doc(sch, doc, &buf, &blen);
    teptris_document_free(doc);
    EXPECT_EQ(st, TEPTRIS_OK);
    if (st != TEPTRIS_OK) {
        return "<status-failure>";
    }
    std::string got(buf, blen);
    free(buf);
    return got;
}

bool valid(teptris_schema *sch, const std::string &toml)
{
    return validate_str(sch, toml) == "[]";
}

/* first error's path from the error-list JSON */
std::string first_path(const std::string &errors)
{
    size_t p = errors.find("\"path\":\"");
    if (p == std::string::npos) {
        return "";
    }
    size_t s = p + 8;
    std::string out;
    for (size_t i = s; i < errors.size(); i++) {
        if (errors[i] == '\\' && i + 1 < errors.size()) {
            out += errors[i + 1];
            i++;
            continue;
        }
        if (errors[i] == '"') {
            break;
        }
        out += errors[i];
    }
    return out;
}

/* read a vendored fixture file (tests use absolute paths from the
 * compile definition; the file stays open for the caller to close) */
std::string read_fixture(const std::string &path, FILE **fptr)
{
    *fptr = fopen(path.c_str(), "rb");
    std::string out;
    if (*fptr == nullptr) {
        return out;
    }
    char rb[4096];
    size_t r;
    while ((r = fread(rb, 1, sizeof rb, *fptr)) > 0) {
        out.append(rb, r);
    }
    return out;
}

} /* namespace */

/* ------------------------------------------------------------ compile -- */

TEST(SchemaCompile, RejectsBrokenJson)
{
    EXPECT_NE(compile_diag("{\"type\":}").find("error"), std::string::npos);
    EXPECT_NE(compile_diag("{\"type\":\"object\"} extra").find("trailing"),
              std::string::npos);
    EXPECT_NE(compile_diag("").find("error"), std::string::npos);
}

TEST(SchemaCompile, RejectsUnknownTypeNames)
{

    SchemaDoc ok;
    ok.sch = compile_ok("{\"type\":[\"string\",\"number\"]}");
    ASSERT_NE(ok.sch, nullptr);
    /* "null" is a real type name now; only unknown names reject */
    SchemaDoc nl;
    nl.sch = compile_ok(
        "{\"properties\":{\"k\":{\"type\":[\"string\",\"null\"]}}}");
    ASSERT_NE(nl.sch, nullptr);
    /* policy 1: null matches no TOML value; a string matches */
    EXPECT_TRUE(valid(nl.sch, "k = 'x'\n"));
    EXPECT_FALSE(valid(nl.sch, "k = 1\n"));
}

/* the 2026 SOTA keywords compile and enforce (no compile-time
 * rejection anymore); only pointer-suffixed dynamic fragments and
 * unmatched anchors still name themselves at compile */
TEST(SchemaCompile, SotaKeywordsCompileAndEnforce)
{
    SchemaDoc u;
    u.sch = compile_ok(
        "{\"properties\":{\"a\":{}},\"unevaluatedProperties\":false}");
    ASSERT_NE(u.sch, nullptr);
    EXPECT_TRUE(valid(u.sch, "a = 1\n"));
    EXPECT_FALSE(valid(u.sch, "a = 1\nb = 2\n"));
    std::string diag = compile_diag("{\"$dynamicRef\":\"#nope\"}");
    EXPECT_NE(diag.find("unresolvable"), std::string::npos);
    /* a pointer-form $dynamicRef reads exactly like a plain $ref */
    SchemaDoc ptr;
    ptr.sch = compile_ok(
        "{\"$defs\":{\"x\":{\"type\":\"integer\"}},"
        "\"properties\":{\"k\":{\"$dynamicRef\":\"#/$defs/x\"}}}");
    ASSERT_NE(ptr.sch, nullptr);
    EXPECT_TRUE(valid(ptr.sch, "k = 1\n"));
    EXPECT_FALSE(valid(ptr.sch, "k = 'x'\n"));
    /* a URI-form $recursiveRef reads exactly like a plain $ref, so a
     * network target hits the no-network policy */
    diag = compile_diag("{\"$recursiveRef\":\"https://x/\"}");
    EXPECT_NE(diag.find("remote"), std::string::npos);
}

TEST(SchemaCompile, RejectsRemoteRefs)
{
    std::string diag = compile_diag("{\"$ref\":\"https://x.example/s.json\"}");
    EXPECT_NE(diag.find("remote"), std::string::npos);
}

TEST(SchemaCompile, RejectsBadLocalRefs)
{
    EXPECT_NE(compile_diag("{\"$ref\":\"#/no/such/where\"}").find("error"),
              std::string::npos);
}

TEST(SchemaCompile, RejectsDegenerateNumerics)
{
    EXPECT_NE(compile_diag({"{\"multipleOf\":0}"}).find("error"),
              std::string::npos);
    EXPECT_NE(compile_diag({"{\"multipleOf\":-2}"}).find("error"),
              std::string::npos);
    SchemaDoc half;
    half.sch = compile_ok("{\"properties\":{\"k\":{\"multipleOf\":0.5}}}");
    ASSERT_NE(half.sch, nullptr);
}

TEST(SchemaCompile, UnknownKeywordsAreExtensions)
{
    /* unrecognized keywords must be ignored (2020-12 extension rule),
     * not rejected */
    SchemaDoc s;
    s.sch = compile_ok(
        "{\"x-vendor\":\"whatever\",\"properties\":{\"k\":{\"type\":\"integer\"}}}");
    ASSERT_NE(s.sch, nullptr);
    EXPECT_TRUE(valid(s.sch, "k = 3\n"));
}

TEST(SchemaCompile, EscapedStringLongPlainTailCompiles)
{
    /* the escaped-string rebuild used to size its arena buffer from
     * the plain PREFIX alone; a long plain tail after the first
     * escape wrote past the arena block (ASan, nightly fuzz
     * 2026-11-06, jput_utf8 heap-buffer-overflow) */
    std::string json = "{\"description\":\"A\\u00e9";
    json.append(128 * 1024, 'B');
    json += "\"}";
    SchemaDoc doc;
    doc.sch = compile_ok(json);
    ASSERT_NE(doc.sch, nullptr);
}

TEST(SchemaCompile, DiagIsSingleMallocdJson)
{
    teptris_schema *sch = nullptr;
    char *diag = nullptr;
    size_t dlen = 0;
    ASSERT_EQ(teptris_schema_compile("{\"bad\":", 7, &sch, &diag, &dlen),
              TEPTRIS_ERR_ARG);
    ASSERT_NE(diag, nullptr);
    EXPECT_EQ(dlen, strlen(diag));
    EXPECT_EQ(diag[0], '{');
    teptris_diag_free(diag);
}

/* ------------------------------------------------------------ policies -- */

TEST(SchemaPolicy1, NullMatchesNoTomlValue)
{
    /* "type": ["string", null] = a string or an ABSENT key */
    SchemaDoc s;
    s.sch = compile_ok(
        "{\"type\":\"object\",\"properties\":{\"x\":{\"type\":[\"string\","
        "null]}}}");
    ASSERT_NE(s.sch, nullptr);
    EXPECT_TRUE(valid(s.sch, "x = \"s\"\n"));
    EXPECT_TRUE(valid(s.sch, "y = 1\n")); /* absent is fine */
    EXPECT_FALSE(valid(s.sch, "x = 1\n"));
    EXPECT_FALSE(valid(s.sch, "x = true\n"));
}

TEST(SchemaPolicy2, IntegerIsTomlNative)
{
    SchemaDoc s;
    s.sch = compile_ok("{\"properties\":{\"k\":{\"type\":\"integer\"}}}");
    ASSERT_NE(s.sch, nullptr);
    EXPECT_TRUE(valid(s.sch, "k = 1\n"));
    EXPECT_TRUE(valid(s.sch, "k = -9223372036854775808\n"));
    /* the 2020-12 rule: a float with a zero fractional part is an
     * integer (the official suite tests it; 1e3 too) */
    EXPECT_TRUE(valid(s.sch, "k = 1.0\n"));
    EXPECT_TRUE(valid(s.sch, "k = 1e3\n"));
    /* "number" admits both, keeping int identity */
    SchemaDoc n;
    n.sch = compile_ok("{\"properties\":{\"k\":{\"type\":\"number\"}}}");
    ASSERT_NE(n.sch, nullptr);
    EXPECT_TRUE(valid(n.sch, "k = 1\n"));
    EXPECT_TRUE(valid(n.sch, "k = 1.5\n"));
    EXPECT_FALSE(valid(n.sch, "k = \"1\"\n"));
}

TEST(SchemaPolicy3, DatetimesAreStringsWithFormat)
{
    SchemaDoc s;
    s.sch = compile_ok("{\"properties\":{\"k\":{\"type\":\"string\"}}}");
    ASSERT_NE(s.sch, nullptr);
    EXPECT_TRUE(valid(s.sch, "k = 2026-01-02T03:04:05Z\n"));
    EXPECT_TRUE(valid(s.sch, "k = 2026-01-02\n"));
    EXPECT_TRUE(valid(s.sch, "k = 03:04:05\n"));
    EXPECT_TRUE(valid(s.sch, "k = 2026-01-02T03:04:05.999-08:00\n"));

    SchemaDoc dt;
    dt.sch = compile_ok(
        "{\"properties\":{\"k\":{\"type\":\"string\",\"format\":"
        "\"date-time\"}}}");
    ASSERT_NE(dt.sch, nullptr);
    EXPECT_TRUE(valid(dt.sch, "k = 2026-01-02T03:04:05Z\n"));
    EXPECT_TRUE(valid(dt.sch, "k = \"2026-01-02T03:04:05Z\"\n"));
    EXPECT_FALSE(valid(dt.sch, "k = 2026-01-02\n"));
    EXPECT_FALSE(valid(dt.sch, "k = \"not-a-date\"\n"));
    /* a local datetime lacks the offset RFC 3339 requires; it
     * satisfies date-time-local instead */
    EXPECT_FALSE(valid(dt.sch, "k = 2026-01-02T03:04:05\n"));

    SchemaDoc d;
    d.sch = compile_ok(
        "{\"properties\":{\"k\":{\"type\":\"string\",\"format\":\"date\"}}}");
    ASSERT_NE(d.sch, nullptr);
    EXPECT_TRUE(valid(d.sch, "k = 2026-01-02\n"));
    /* a full datetime is not a full-date */
    EXPECT_FALSE(valid(d.sch, "k = 2026-01-02T03:04:05Z\n"));

    SchemaDoc t;
    t.sch = compile_ok(
        "{\"properties\":{\"k\":{\"type\":\"string\",\"format\":\"time\"}}}");
    ASSERT_NE(t.sch, nullptr);
    /* the RFC 3339 full-time: an offset is required on strings */
    EXPECT_TRUE(valid(t.sch, "k = \"03:04:05Z\"\n"));
    EXPECT_FALSE(valid(t.sch, "k = 03:04:05\n"));
    EXPECT_FALSE(valid(t.sch, "k = 2026-01-02\n"));

    /* the TOML-native local formats (tombi's named formats) */
    SchemaDoc tl;
    tl.sch = compile_ok(
        "{\"properties\":{\"k\":{\"format\":\"time-local\"}}}");
    ASSERT_NE(tl.sch, nullptr);
    EXPECT_TRUE(valid(tl.sch, "k = 03:04:05\n"));
    EXPECT_TRUE(valid(tl.sch, "k = 03:04:05.123\n"));
    EXPECT_FALSE(valid(tl.sch, "k = \"03:04:05Z\"\n"));

    SchemaDoc dtl;
    dtl.sch = compile_ok(
        "{\"properties\":{\"k\":{\"format\":\"date-time-local\"}}}");
    ASSERT_NE(dtl.sch, nullptr);
    EXPECT_TRUE(valid(dtl.sch, "k = 2026-01-02T03:04:05\n"));
    EXPECT_FALSE(valid(dtl.sch, "k = 2026-01-02T03:04:05Z\n"));
}

TEST(SchemaPolicy4, NonFiniteFloatsAreFloats)
{
    SchemaDoc num;
    num.sch = compile_ok("{\"properties\":{\"k\":{\"type\":\"number\"}}}");
    ASSERT_NE(num.sch, nullptr);
    EXPECT_TRUE(valid(num.sch, "k = nan\n"));
    EXPECT_TRUE(valid(num.sch, "k = inf\n"));

    /* nan fails every ordered comparison */
    SchemaDoc lo;
    lo.sch = compile_ok("{\"properties\":{\"k\":{\"minimum\":0}}}");
    ASSERT_NE(lo.sch, nullptr);
    EXPECT_FALSE(valid(lo.sch, "k = nan\n"));
    SchemaDoc hi;
    hi.sch = compile_ok("{\"properties\":{\"k\":{\"maximum\":10}}}");
    ASSERT_NE(hi.sch, nullptr);
    EXPECT_FALSE(valid(hi.sch, "k = nan\n"));
    SchemaDoc both;
    both.sch = compile_ok("{\"properties\":{\"k\":{\"exclusiveMinimum\":0,\"exclusiveMaximum\":10}}}");
    ASSERT_NE(both.sch, nullptr);
    EXPECT_FALSE(valid(both.sch, "k = nan\n"));

    /* inf violates an upper bound */
    EXPECT_FALSE(valid(hi.sch, "k = inf\n"));
    SchemaDoc lo0;
    lo0.sch = compile_ok(
        "{\"properties\":{\"k\":{\"minimum\":0}}}");
    ASSERT_NE(lo0.sch, nullptr);
    EXPECT_TRUE(valid(lo0.sch, "k = inf\n"));
    SchemaDoc exmax;
    exmax.sch = compile_ok("{\"properties\":{\"k\":{\"exclusiveMaximum\":10}}}");
    ASSERT_NE(exmax.sch, nullptr);
    EXPECT_FALSE(valid(exmax.sch, "k = inf\n"));
}

/* ------------------------------------------------------------- object -- */

TEST(SchemaObject, RequiredAndProperties)
{
    SchemaDoc s;
    s.sch = compile_ok(
        "{\"type\":\"object\",\"properties\":{\"name\":{\"type\":\"string\"},"
        "\"port\":{\"type\":\"integer\"}},\"required\":[\"name\",\"port\"]}");
    ASSERT_NE(s.sch, nullptr);
    EXPECT_TRUE(valid(s.sch, "name = \"a\"\nport = 1\n"));
    EXPECT_FALSE(valid(s.sch, "name = \"a\"\n"));
    std::string errs = validate_str(s.sch, "port = 1\n");
    EXPECT_NE(errs.find("name"), std::string::npos);
    EXPECT_EQ(first_path(errs), "");
}

TEST(SchemaObject, NestedErrorPathsAndSchemaPaths)
{
    SchemaDoc s;
    s.sch = compile_ok(
        "{\"properties\":{\"srv\":{\"properties\":{\"port\":{\"type\":"
        "\"integer\"}}}}}");
    ASSERT_NE(s.sch, nullptr);
    std::string errs = validate_str(s.sch, "[srv]\nport = \"x\"\n");
    EXPECT_EQ(first_path(errs), "srv.port");
    EXPECT_NE(errs.find("#/properties.srv/properties.port"),
              std::string::npos);
}

TEST(SchemaObject, PatternPropertiesAndAdditionalFalse)
{
    SchemaDoc s;
    s.sch = compile_ok(
        "{\"patternProperties\":{\"^x-\":{\"type\":\"integer\"}},"
        "\"additionalProperties\":false}");
    ASSERT_NE(s.sch, nullptr);
    EXPECT_TRUE(valid(s.sch, "x-a = 1\n"));
    EXPECT_FALSE(valid(s.sch, "x-a = \"s\"\n"));
    std::string errs = validate_str(s.sch, "x-a = 1\nbad = 2\n");
    EXPECT_NE(errs.find("bad"), std::string::npos);
}

TEST(SchemaObject, PropertyNames)
{
    SchemaDoc s;
    s.sch = compile_ok(
        "{\"propertyNames\":{\"pattern\":\"^[a-z][a-z0-9-]*$\"}}");
    ASSERT_NE(s.sch, nullptr);
    EXPECT_TRUE(valid(s.sch, "ok-key = 1\n"));
    EXPECT_FALSE(valid(s.sch, "\"9bad\" = 1\n"));
}

TEST(SchemaObject, CountsAndDependencies)
{
    SchemaDoc s;
    s.sch = compile_ok(
        "{\"minProperties\":1,\"maxProperties\":2,\"dependentRequired\":{"
        "\"a\":[\"b\"]},\"dependentSchemas\":{\"c\":{\"required\":[\"d\"]}}}");
    ASSERT_NE(s.sch, nullptr);
    EXPECT_TRUE(valid(s.sch, "z = 0\n"));
    EXPECT_TRUE(valid(s.sch, "a = 1\nb = 2\n"));
    EXPECT_FALSE(valid(s.sch, "a = 1\n"));
    EXPECT_FALSE(valid(s.sch, "c = 1\n"));
    EXPECT_TRUE(valid(s.sch, "c = 1\nd = 2\n"));
    EXPECT_FALSE(valid(s.sch, "p = 1\nq = 2\nr = 3\n"));
}

/* -------------------------------------------------------------- array -- */

TEST(SchemaArray, PrefixItemsThenItems)
{
    SchemaDoc s;
    s.sch = compile_ok(
        "{\"properties\":{\"k\":{\"type\":\"array\",\"prefixItems\":[{\"type\":\"string\"},{"
        "\"type\":\"integer\"}],\"items\":{\"type\":\"boolean\"}}}}");
    ASSERT_NE(s.sch, nullptr);
    EXPECT_TRUE(valid(s.sch, "k = [\"a\", 1, true, false]\n"));
    EXPECT_FALSE(valid(s.sch, "k = [1, \"a\"]\n"));
    EXPECT_FALSE(valid(s.sch, "k = [\"a\", 1, \"x\"]\n"));
    std::string errs = validate_str(s.sch, "k = [1]\n");
    EXPECT_EQ(first_path(errs), "k.0");
}

TEST(SchemaArray, ContainsWithCounts)
{
    SchemaDoc s;
    s.sch = compile_ok(
        "{\"properties\":{\"k\":{\"contains\":{\"type\":\"integer\"},\"minContains\":2,"
        "\"maxContains\":3}}}");
    ASSERT_NE(s.sch, nullptr);
    EXPECT_TRUE(valid(s.sch, "k = [1, \"a\", 2, \"b\", 3]\n"));
    EXPECT_FALSE(valid(s.sch, "k = [1, \"a\"]\n"));
    EXPECT_FALSE(valid(s.sch, "k = [1, 2, 3, 4]\n"));
}

TEST(SchemaArray, UniqueItems)
{
    SchemaDoc s;
    s.sch = compile_ok("{\"properties\":{\"k\":{\"uniqueItems\":true}}}");
    ASSERT_NE(s.sch, nullptr);
    EXPECT_TRUE(valid(s.sch, "k = [1, 2, \"a\"]\n"));
    /* numeric identity: 1 and 1.0 are equal (bools stay distinct) */
    EXPECT_FALSE(valid(s.sch, "k = [1, 1.0]\n"));
    EXPECT_FALSE(valid(s.sch, "k = [1, 1]\n"));
    EXPECT_TRUE(valid(s.sch, "k = [1, true]\n"));
    EXPECT_FALSE(valid(s.sch, "k = [{a = 1}, {a = 1}]\n"));
}

TEST(SchemaArray, MinMaxItems)
{
    SchemaDoc s;
    s.sch = compile_ok("{\"properties\":{\"k\":{\"minItems\":1,\"maxItems\":2}}}");
    ASSERT_NE(s.sch, nullptr);
    EXPECT_FALSE(valid(s.sch, "k = []\n"));
    EXPECT_TRUE(valid(s.sch, "k = [1]\n"));
    EXPECT_TRUE(valid(s.sch, "k = [1, 2]\n"));
    EXPECT_FALSE(valid(s.sch, "k = [1, 2, 3]\n"));
}

/* ------------------------------------------------------------- string -- */

TEST(SchemaString, LengthInCodePoints)
{
    SchemaDoc s;
    s.sch = compile_ok("{\"properties\":{\"k\":{\"minLength\":3,\"maxLength\":4}}}");
    ASSERT_NE(s.sch, nullptr);
    /* two code points, six bytes */
    EXPECT_TRUE(valid(s.sch, "k = \"\\u00e9\\u00e9\\u00e9\"\n"));
    EXPECT_FALSE(valid(s.sch, "k = \"\\u00e9\\u00e9\"\n"));
    EXPECT_FALSE(valid(s.sch, "k = \"abcde\"\n"));
}

TEST(SchemaString, Pattern)
{
    SchemaDoc s;
    s.sch = compile_ok(
        "{\"properties\":{\"v\":{\"pattern\":\"^[a-z]+-[0-9]+$\"}}}");
    ASSERT_NE(s.sch, nullptr);
    EXPECT_TRUE(valid(s.sch, "v = \"abc-12\"\n"));
    EXPECT_FALSE(valid(s.sch, "v = \"abc\"\n"));
    /* case-insensitive flag */
    SchemaDoc ci;
    ci.sch = compile_ok("{\"properties\":{\"v\":{\"pattern\":\"(?i)^abc$\"}}}");
    ASSERT_NE(ci.sch, nullptr);
    EXPECT_TRUE(valid(ci.sch, "v = \"ABC\"\n"));
}

/* ----------------------------------------------------------- numerics -- */

TEST(SchemaNumeric, BoundsWithExactIntegerPaths)
{
    SchemaDoc s;
    s.sch = compile_ok(
        "{\"properties\":{\"k\":{\"minimum\":-10,\"maximum\":10,\"exclusiveMinimum\":-10,"
        "\"exclusiveMaximum\":10}}}");
    ASSERT_NE(s.sch, nullptr);
    EXPECT_TRUE(valid(s.sch, "k = 0\n"));
    EXPECT_TRUE(valid(s.sch, "k = 9\n"));
    EXPECT_FALSE(valid(s.sch, "k = 10\n"));
    EXPECT_FALSE(valid(s.sch, "k = -10\n"));
}

TEST(SchemaNumeric, FloatBoundsCatchIntegers)
{
    SchemaDoc s;
    s.sch = compile_ok("{\"properties\":{\"k\":{\"minimum\":0.5}}}");
    ASSERT_NE(s.sch, nullptr);
    EXPECT_TRUE(valid(s.sch, "k = 1\n"));
    EXPECT_FALSE(valid(s.sch, "k = 0\n"));
}

TEST(SchemaNumeric, MultipleOf)
{
    SchemaDoc i;
    i.sch = compile_ok({"{\"properties\":{\"k\":{\"multipleOf\":3}}}"});
    ASSERT_NE(i.sch, nullptr);
    EXPECT_TRUE(valid(i.sch, "k = 9\n"));
    EXPECT_FALSE(valid(i.sch, "k = 10\n"));
    EXPECT_TRUE(valid(i.sch, "k = -6\n"));
    SchemaDoc f;
    f.sch = compile_ok({"{\"properties\":{\"k\":{\"multipleOf\":0.5}}}"});
    ASSERT_NE(f.sch, nullptr);
    EXPECT_TRUE(valid(f.sch, "k = 2.5\n"));
    EXPECT_TRUE(valid(f.sch, "k = 3\n"));
    EXPECT_FALSE(valid(f.sch, "k = 2.75\n"));
}

/* ------------------------------------------------------ value constraints */

TEST(SchemaValues, EnumIsTypeStrict)
{
    SchemaDoc s;
    s.sch = compile_ok("{\"properties\":{\"k\":{\"enum\":[1, \"one\", true]}}}");
    ASSERT_NE(s.sch, nullptr);
    EXPECT_TRUE(valid(s.sch, "k = 1\n"));
    EXPECT_TRUE(valid(s.sch, "k = \"one\"\n"));
    EXPECT_TRUE(valid(s.sch, "k = true\n"));
    /* numeric identity across the enum (bools and strings distinct) */
    EXPECT_TRUE(valid(s.sch, "k = 1.0\n"));
    EXPECT_FALSE(valid(s.sch, "k = \"1\"\n"));
}

TEST(SchemaValues, ConstDeepEqual)
{
    SchemaDoc s;
    s.sch = compile_ok("{\"const\":{\"a\":[1, \"x\"]}}");
    ASSERT_NE(s.sch, nullptr);
    EXPECT_TRUE(valid(s.sch, "a = [1, \"x\"]\n"));
    /* const implies the type; a scalar against a table const fails */
    EXPECT_FALSE(valid(s.sch, "k = 1\n"));
}

TEST(SchemaValues, DatetimeEnumEntries)
{
    /* schema-side datetime strings match TOML datetime values */
    SchemaDoc s;
    s.sch = compile_ok("{\"properties\":{\"k\":{\"enum\":[\"2026-01-02T03:04:05Z\"]}}}");
    ASSERT_NE(s.sch, nullptr);
    EXPECT_TRUE(valid(s.sch, "k = 2026-01-02T03:04:05Z\n"));
    EXPECT_FALSE(valid(s.sch, "k = 2026-01-02T03:04:06Z\n"));
}

/* ---------------------------------------------------------- applicators -- */

TEST(SchemaApplicators, AllOfAnyOfOneOfNot)
{
    SchemaDoc all;
    all.sch = compile_ok(
        "{\"properties\":{\"k\":{\"allOf\":[{\"type\":\"integer\"},{\"minimum\":5}]}}}");
    ASSERT_NE(all.sch, nullptr);
    EXPECT_TRUE(valid(all.sch, "k = 7\n"));
    EXPECT_FALSE(valid(all.sch, "k = 2\n"));
    EXPECT_FALSE(valid(all.sch, "k = \"s\"\n"));

    SchemaDoc any;
    any.sch = compile_ok(
        "{\"properties\":{\"k\":{\"anyOf\":[{\"type\":\"integer\"},{\"const\":\"x\"}]}}}");
    ASSERT_NE(any.sch, nullptr);
    EXPECT_TRUE(valid(any.sch, "k = 1\n"));
    EXPECT_TRUE(valid(any.sch, "k = \"x\"\n"));
    EXPECT_FALSE(valid(any.sch, "k = \"y\"\n"));

    SchemaDoc one;
    one.sch = compile_ok(
        "{\"properties\":{\"k\":{\"oneOf\":[{\"type\":\"integer\"},{\"type\":\"number\",\"minimum\":0}]}}}");
    ASSERT_NE(one.sch, nullptr);
    /* an integer matches BOTH schemas here -> not exactly one */
    EXPECT_FALSE(valid(one.sch, "k = 1\n"));
    EXPECT_TRUE(valid(one.sch, "k = -1\n")); /* only the integer branch */
    EXPECT_TRUE(valid(one.sch, "k = 1.5\n")); /* only the number branch */

    SchemaDoc nots;
    nots.sch = compile_ok("{\"properties\":{\"k\":{\"not\":{\"type\":\"integer\"}}}}");
    ASSERT_NE(nots.sch, nullptr);
    EXPECT_TRUE(valid(nots.sch, "k = \"s\"\n"));
    EXPECT_FALSE(valid(nots.sch, "k = 1\n"));
}

TEST(SchemaApplicators, IfThenElse)
{
    SchemaDoc s;
    s.sch = compile_ok(
        "{\"if\":{\"properties\":{\"kind\":{\"const\":\"port\"}}},\"then\":{"
        "\"required\":[\"port\"]},\"else\":{\"required\":[\"path\"]}}");
    ASSERT_NE(s.sch, nullptr);
    EXPECT_TRUE(valid(s.sch, "kind = \"port\"\nport = 1\n"));
    EXPECT_FALSE(valid(s.sch, "kind = \"port\"\n"));
    EXPECT_TRUE(valid(s.sch, "kind = \"file\"\npath = \"/x\"\n"));
    EXPECT_FALSE(valid(s.sch, "kind = \"file\"\n"));
}

/* ---------------------------------------------------------------- $ref -- */

TEST(SchemaRef, PointerTargets)
{
    SchemaDoc s;
    s.sch = compile_ok(
        "{\"definitions\":{\"port\":{\"type\":\"integer\",\"minimum\":1}},"
        "\"properties\":{\"p\":{\"$ref\":\"#/definitions/port\"}}}");
    ASSERT_NE(s.sch, nullptr);
    EXPECT_TRUE(valid(s.sch, "p = 8080\n"));
    EXPECT_FALSE(valid(s.sch, "p = 0\n"));
    EXPECT_FALSE(valid(s.sch, "p = \"x\"\n"));
}

TEST(SchemaRef, RecursiveSelf)
{
    SchemaDoc s;
    s.sch = compile_ok(
        "{\"properties\":{\"v\":{\"type\":\"integer\"},\"kids\":{\"type\":"
        "\"array\",\"items\":{\"$ref\":\"#\"}}},\"required\":[\"v\"]}");
    ASSERT_NE(s.sch, nullptr);
    EXPECT_TRUE(valid(s.sch,
                      "v = 0\n[[kids]]\nv = 1\n[[kids.kids]]\nv = 2\n"));
    std::string errs = validate_str(s.sch, "v = 0\n[[kids]]\nv = \"x\"\n");
    EXPECT_EQ(first_path(errs), "kids.0.v");
}

TEST(SchemaRef, AnchorFragment)
{
    SchemaDoc s;
    s.sch = compile_ok(
        "{\"definitions\":{\"p\":{\"$anchor\":\"portType\",\"type\":"
        "\"integer\"}},\"properties\":{\"p\":{\"$ref\":\"#portType\"}}}");
    ASSERT_NE(s.sch, nullptr);
    EXPECT_TRUE(valid(s.sch, "p = 1\n"));
    EXPECT_FALSE(valid(s.sch, "p = \"x\"\n"));
}

TEST(SchemaRef, SharedNodeSiblings)
{
    /* a $ref used twice must validate both occurrences */
    SchemaDoc s;
    s.sch = compile_ok(
        "{\"definitions\":{\"u\":{\"type\":\"string\"}},\"properties\":{"
        "\"a\":{\"$ref\":\"#/definitions/u\"},\"b\":{\"$ref\":"
        "\"#/definitions/u\"}}}");
    ASSERT_NE(s.sch, nullptr);
    EXPECT_TRUE(valid(s.sch, "a = \"x\"\nb = \"y\"\n"));
    EXPECT_FALSE(valid(s.sch, "a = \"x\"\nb = 1\n"));
}

/* ------------------------------------------------------------- envelope -- */

TEST(SchemaEnvelope, EmptySchemaMatchesEverything)
{
    SchemaDoc s;
    s.sch = compile_ok("{}");
    ASSERT_NE(s.sch, nullptr);
    EXPECT_TRUE(valid(s.sch, "[any]\nk = 1979-05-27T07:32:00Z\nf = nan\n"));
}

TEST(SchemaEnvelope, ErrorListIsBoundedAndShaped)
{
    SchemaDoc s;
    s.sch = compile_ok(
        "{\"properties\":{"
        "\"a\":{\"type\":\"integer\"},\"b\":{\"type\":\"integer\"}}}");
    ASSERT_NE(s.sch, nullptr);
    std::string errs = validate_str(s.sch, "a = \"x\"\nb = \"y\"\n");
    /* the envelope is one JSON array */
    ASSERT_EQ(errs.front(), '[');
    ASSERT_EQ(errs.back(), ']');
    /* every element carries exactly the three keys */
    size_t count = 0;
    for (size_t i = 0; i + 1 < errs.size(); i++) {
        if (errs[i] == '{' && errs[i + 1] == '"') {
            count++;
            EXPECT_NE(errs.find("\"path\":", i), std::string::npos);
            EXPECT_NE(errs.find("\"schemaPath\":", i), std::string::npos);
            EXPECT_NE(errs.find("\"message\":", i), std::string::npos);
        }
    }
    EXPECT_EQ(count, size_t(2));
}

TEST(SchemaEnvelope, ErrorPathsEscapeQuotes)
{
    SchemaDoc s;
    s.sch = compile_ok(
        "{\"properties\":{\"weird.key\":{\"type\":\"integer\"}}}");
    ASSERT_NE(s.sch, nullptr);
    std::string errs = validate_str(s.sch, "\"weird.key\" = \"x\"\n");
    /* the segment is quoted, the JSON string escapes those quotes */
    EXPECT_NE(errs.find("\\\"weird.key\\\""), std::string::npos);
}

TEST(SchemaEnvelope, ValidateDocMatchesValidate)
{
    SchemaDoc s;
    s.sch = compile_ok(
        "{\"properties\":{\"p\":{\"type\":\"integer\"}},\"required\":[\"p\"]}");
    ASSERT_NE(s.sch, nullptr);
    std::string toml = "p = \"x\"\n";
    EXPECT_EQ(validate_str(s.sch, toml), validate_doc_str(s.sch, toml));
    EXPECT_EQ(validate_str(s.sch, "p = 1\n"), validate_doc_str(s.sch, "p = 1\n"));
}

TEST(SchemaEnvelope, SyntaxErrorsAreReported)
{
    SchemaDoc s;
    s.sch = compile_ok("{}");
    ASSERT_NE(s.sch, nullptr);
    char *buf = nullptr;
    size_t blen = 0;
    EXPECT_EQ(teptris_schema_validate(s.sch, "a = = 1\n", 8, &buf, &blen),
              TEPTRIS_ERR_SYNTAX);
    EXPECT_EQ(buf, nullptr);
}

TEST(SchemaEnvelope, DeepDocumentsDoNotBlowTheStack)
{
    SchemaDoc s;
    s.sch = compile_ok("{}");
    ASSERT_NE(s.sch, nullptr);
    std::string toml;
    for (int i = 0; i < 200; i++) {
        toml += "[a" + std::to_string(i) + "]\n";
    }
    EXPECT_TRUE(valid(s.sch, toml));
}

TEST(SchemaEnvelope, CompileFreesCleanly)
{
    /* compile/free loop over a doc-bounded $ref cycle: the arena
     * teardown must not leak (ASan) */
    for (int i = 0; i < 32; i++) {
        SchemaDoc s;
        s.sch = compile_ok(
            "{\"definitions\":{\"r\":{\"properties\":{\"kids\":{\"type\":"
            "\"array\",\"items\":{\"$ref\":\"#/definitions/r\"}}}}},"
            "\"properties\":{\"k\":{\"$ref\":\"#/definitions/r\"}}}");
        ASSERT_NE(s.sch, nullptr);
        EXPECT_TRUE(valid(s.sch, "[k]\n[[k.kids]]\n[[k.kids.kids]]\n"));
    }
}

TEST(SchemaEnvelope, UnguardedRefCycleErrorsInsteadOfLooping)
{
    /* a schema that is its own ref target consumes no document
     * structure on each lap; the walk must report, not recurse */
    SchemaDoc s;
    s.sch = compile_ok(
        "{\"definitions\":{\"r\":{\"$ref\":\"#/definitions/r\",\"type\":"
        "\"object\"}},\"properties\":{\"k\":{\"$ref\":\"#/definitions/"
        "r\"}}}");
    ASSERT_NE(s.sch, nullptr);
    EXPECT_FALSE(valid(s.sch, "k = {}\n"));
}

/* --------------------------------------------- vendored real manifests -- */

TEST(SchemaFixtures, CargoLikeManifest)
{
    FILE *f = nullptr;
    std::string json = read_fixture(
        std::string(TEPTRIS_SOURCE_DIR) + "/test/fixtures/schema/cargo-like.json",
        &f);
    ASSERT_NE(f, nullptr);
    fclose(f);

    SchemaDoc s;
    s.sch = compile_ok(json);
    ASSERT_NE(s.sch, nullptr);

    /* the conforming manifest validates silently */
    std::string ok = read_fixture(
        std::string(TEPTRIS_SOURCE_DIR) +
            "/test/fixtures/schema/cargo-like-valid.toml",
        &f);
    ASSERT_NE(f, nullptr);
    fclose(f);
    EXPECT_EQ(validate_str(s.sch, ok), "[]");

    /* the violating manifest names every broken key */
    std::string bad = read_fixture(
        std::string(TEPTRIS_SOURCE_DIR) +
            "/test/fixtures/schema/cargo-like-violations.toml",
        &f);
    ASSERT_NE(f, nullptr);
    fclose(f);
    std::string errs = validate_str(s.sch, bad);
    EXPECT_NE(errs.find("\"path\":\"package.name\""), std::string::npos);
    EXPECT_NE(errs.find("\"path\":\"package.version\""), std::string::npos);
    EXPECT_NE(errs.find("\"path\":\"package.edition\""), std::string::npos);
    EXPECT_NE(errs.find("\"path\":\"dependencies.serde\""), std::string::npos);
    EXPECT_NE(errs.find("\"path\":\"profile.release.opt-level\""),
              std::string::npos);
}

TEST(SchemaFixtures, PyprojectLikeManifest)
{
    FILE *f = nullptr;
    std::string json = read_fixture(
        std::string(TEPTRIS_SOURCE_DIR) +
            "/test/fixtures/schema/pyproject-like.json",
        &f);
    ASSERT_NE(f, nullptr);
    fclose(f);

    SchemaDoc s;
    s.sch = compile_ok(json);
    ASSERT_NE(s.sch, nullptr);

    std::string ok = read_fixture(
        std::string(TEPTRIS_SOURCE_DIR) +
            "/test/fixtures/schema/pyproject-like-valid.toml",
        &f);
    ASSERT_NE(f, nullptr);
    fclose(f);
    EXPECT_EQ(validate_str(s.sch, ok), "[]");

    std::string bad = read_fixture(
        std::string(TEPTRIS_SOURCE_DIR) +
            "/test/fixtures/schema/pyproject-like-violations.toml",
        &f);
    ASSERT_NE(f, nullptr);
    fclose(f);
    std::string errs = validate_str(s.sch, bad);
    EXPECT_NE(errs.find("\"path\":\"tool.demo.mode\""), std::string::npos);
    EXPECT_NE(errs.find("\"path\":\"tool.demo.retries\""), std::string::npos);
    EXPECT_NE(errs.find("\"path\":\"tool.demo.ratio\""), std::string::npos);
    EXPECT_NE(errs.find("\"path\":\"tool.demo.tags\""), std::string::npos);
    EXPECT_NE(errs.find("\"path\":\"tool.demo.since\""), std::string::npos);
}

/* ================================================== taplo parity set */

TEST(SchemaDeps, DependenciesArrayForm)
{
    SchemaDoc s;
    s.sch = compile_ok(
        {"{\"dependencies\":{\"credit_card\":[\"billing_address\"]}}"});
    ASSERT_NE(s.sch, nullptr);
    EXPECT_TRUE(valid(s.sch, "name = 'x'\n"));
    EXPECT_TRUE(valid(s.sch,
                      "credit_card = 1\nbilling_address = 'y'\n"));
    EXPECT_FALSE(valid(s.sch, "credit_card = 1\n"));
    std::string errs = validate_str(s.sch, "credit_card = 1\n");
    EXPECT_NE(errs.find("billing_address"), std::string::npos);
}

TEST(SchemaDeps, DependenciesSchemaForm)
{
    SchemaDoc s;
    s.sch = compile_ok(
        {"{\"dependencies\":{\"credit_card\":{\"properties\":"
         "{\"billing_address\":{\"type\":\"string\"}}}}}"});
    ASSERT_NE(s.sch, nullptr);
    EXPECT_TRUE(valid(s.sch, "credit_card = 1\n"));
    EXPECT_TRUE(valid(s.sch,
                      "credit_card = 1\nbilling_address = 'y'\n"));
    EXPECT_FALSE(valid(s.sch,
                       "credit_card = 1\nbilling_address = 2\n"));
}

TEST(SchemaDeps, DependenciesMixedAndWrongShape)
{
    SchemaDoc s;
    s.sch = compile_ok(
        {"{\"dependencies\":{\"a\":[\"b\"],\"c\":{\"required\":[\"d\"]}}}"});
    ASSERT_NE(s.sch, nullptr);
    EXPECT_FALSE(valid(s.sch, "a = 1\nc = true\n"));
    EXPECT_TRUE(valid(s.sch, "a = 1\nc = true\nd = 1\nb = 1\n"));
    /* non-array, non-schema values are a compile error, never silence */
    std::string diag =
        compile_diag("{\"dependencies\":{\"a\":42}}");
    EXPECT_NE(diag.find("dependencies"), std::string::npos);
}

TEST(SchemaTuple, AdditionalItemsAndItemsArray)
{
    SchemaDoc s;
    s.sch = compile_ok(
        {"{\"properties\":{\"t\":{\"items\":[{\"type\":\"string\"},"
         "{\"type\":\"integer\"}]}}}"});
    ASSERT_NE(s.sch, nullptr);
    EXPECT_TRUE(valid(s.sch, "t = ['a', 1]\n"));
    EXPECT_FALSE(valid(s.sch, "t = [1, 'a']\n"));
    /* tuple form leaves extra items unconstrained (draft-07 default) */
    EXPECT_TRUE(valid(s.sch, "t = ['a', 1, true]\n"));
    EXPECT_EQ(first_path(validate_str(s.sch, "t = [1, 'a']\n")), "t.0");
}

TEST(SchemaTuple, AdditionalItemsFalse)
{
    SchemaDoc s;
    s.sch = compile_ok(
        {"{\"properties\":{\"t\":{\"items\":[{\"type\":\"string\"}],"
         "\"additionalItems\":false}}}"});
    ASSERT_NE(s.sch, nullptr);
    EXPECT_TRUE(valid(s.sch, "t = ['a']\n"));
    EXPECT_FALSE(valid(s.sch, "t = ['a', 1]\n"));
    EXPECT_EQ(first_path(validate_str(s.sch, "t = ['a', 1]\n")), "t.1");
}

TEST(SchemaTuple, AdditionalItemsSchema)
{
    SchemaDoc s;
    s.sch = compile_ok(
        {"{\"properties\":{\"t\":{\"items\":[{\"type\":\"string\"}],"
         "\"additionalItems\":{\"type\":\"integer\"}}}}"});
    ASSERT_NE(s.sch, nullptr);
    EXPECT_TRUE(valid(s.sch, "t = ['a', 1, 2]\n"));
    EXPECT_FALSE(valid(s.sch, "t = ['a', 'b']\n"));
    EXPECT_EQ(first_path(validate_str(s.sch, "t = ['a', 'b']\n")), "t.1");
}

TEST(SchemaTuple, AdditionalItemsWithoutTuple)
{
    SchemaDoc s;
    s.sch = compile_ok(
        {"{\"properties\":{\"t\":{\"additionalItems\":{\"type\":"
         "\"integer\"}}}}"});
    ASSERT_NE(s.sch, nullptr);
    EXPECT_TRUE(valid(s.sch, "t = [1, 2]\n"));
    EXPECT_FALSE(valid(s.sch, "t = [1, 'x']\n"));
}

TEST(SchemaContent, Base64AndJsonMediaType)
{
    SchemaDoc s;
    s.sch = compile_ok(
        {"{\"properties\":{\"d\":{\"contentEncoding\":\"base64\"},"
         "\"j\":{\"contentMediaType\":\"application/json\"}}}"});
    ASSERT_NE(s.sch, nullptr);
    EXPECT_TRUE(valid(s.sch, "d = 'aGVsbG8='\n"));
    EXPECT_TRUE(valid(s.sch, "d = ''\n")); /* empty decodes to empty */
    EXPECT_FALSE(valid(s.sch, "d = 'not base64!'\n"));
    EXPECT_FALSE(valid(s.sch, "d = 'aGVsbG8'\n")); /* no padding */
    EXPECT_TRUE(valid(s.sch, "j = '{\"a\": [1, 2]}'\n"));
    EXPECT_FALSE(valid(s.sch, "j = '{oops}'\n"));
    /* base64 content is decoded before the media check */
    SchemaDoc both;
    both.sch = compile_ok(
        {"{\"properties\":{\"x\":{\"contentEncoding\":\"base64\","
         "\"contentMediaType\":\"application/json\"}}}"});
    ASSERT_NE(both.sch, nullptr);
    /* base64("7B") is "{" — decoded text parses as JSON? no: "{" alone
     * is not valid JSON; use base64 of "[]" */
    EXPECT_TRUE(valid(both.sch, "x = 'W10='\n")); /* "[]" */
    EXPECT_FALSE(valid(both.sch, "x = 'eHg='\n")); /* "xx" */
}

TEST(SchemaContent, UnknownNamesAreAnnotations)
{
    SchemaDoc s;
    s.sch = compile_ok(
        {"{\"properties\":{\"d\":{\"contentEncoding\":\"gzip\","
         "\"contentMediaType\":\"image/png\"}}}"});
    ASSERT_NE(s.sch, nullptr);
    EXPECT_TRUE(valid(s.sch, "d = 'anything at all'\n"));
}

TEST(SchemaNumeric, MultipleOfExactDecimals)
{
    SchemaDoc s;
    s.sch = compile_ok({"{\"properties\":{\"k\":{\"multipleOf\":0.1}}}"});
    ASSERT_NE(s.sch, nullptr);
    EXPECT_TRUE(valid(s.sch, "k = 0.3\n"));  /* the 2.9999... trap */
    EXPECT_TRUE(valid(s.sch, "k = 3.0\n"));
    EXPECT_TRUE(valid(s.sch, "k = -0.7\n"));
    EXPECT_FALSE(valid(s.sch, "k = 0.35\n"));
    SchemaDoc c;
    c.sch = compile_ok({"{\"properties\":{\"k\":{\"multipleOf\":0.01}}}"});
    ASSERT_NE(c.sch, nullptr);
    EXPECT_TRUE(valid(c.sch, "k = 0.07\n"));
    EXPECT_TRUE(valid(c.sch, "k = 7\n"));
    EXPECT_FALSE(valid(c.sch, "k = 0.075\n"));
    SchemaDoc big;
    big.sch = compile_ok({"{\"properties\":{\"k\":{\"multipleOf\":1e300}}}"});
    ASSERT_NE(big.sch, nullptr);
    EXPECT_TRUE(valid(big.sch, "k = 2e300\n"));
    EXPECT_FALSE(valid(big.sch, "k = 1.5e300\n"));
}

TEST(SchemaValues, ConstMatchesZAndOffsetSpellings)
{
    SchemaDoc z;
    z.sch = compile_ok(
        {"{\"properties\":{\"k\":{\"const\":\"2026-01-02T03:04:05Z\"}}}"});
    ASSERT_NE(z.sch, nullptr);
    EXPECT_TRUE(valid(z.sch, "k = 2026-01-02T03:04:05Z\n"));
    /* the same instant written with a +00:00 offset in the schema */
    SchemaDoc off;
    off.sch = compile_ok(
        {"{\"properties\":{\"k\":{\"const\":"
         "\"2026-01-02T03:04:05+00:00\"}}}"});
    ASSERT_NE(off.sch, nullptr);
    EXPECT_TRUE(valid(off.sch, "k = 2026-01-02T03:04:05Z\n"));
    EXPECT_FALSE(valid(off.sch, "k = 2026-01-02T03:04:05+01:00\n"));
}

TEST(SchemaFormats, DateAndDateTimeStrings)
{
    SchemaDoc s;
    s.sch = compile_ok(
        {"{\"properties\":{\"d\":{\"format\":\"date\"},"
         "\"t\":{\"format\":\"date-time\"}}}"});
    ASSERT_NE(s.sch, nullptr);
    EXPECT_TRUE(valid(s.sch, "d = '2026-01-31'\n"));
    EXPECT_TRUE(valid(s.sch, "d = 2026-01-31\n")); /* a TOML date value */
    EXPECT_FALSE(valid(s.sch, "d = '2026-02-30'\n")); /* no such day */
    EXPECT_FALSE(valid(s.sch, "d = '2026-1-31'\n"));
    EXPECT_TRUE(valid(s.sch, "t = '2026-01-02T03:04:05Z'\n"));
    EXPECT_TRUE(valid(s.sch, "t = '2026-01-02T03:04:05.25+01:00'\n"));
    EXPECT_TRUE(valid(s.sch, "t = 2026-01-02T03:04:05Z\n"));
    /* RFC 3339 requires the offset on plain strings */
    EXPECT_FALSE(valid(s.sch, "t = '2026-01-02T03:04:05'\n"));
    EXPECT_FALSE(valid(s.sch, "t = '2026-13-02T03:04:05Z'\n"));
}

TEST(SchemaFormats, TimeStrings)
{
    SchemaDoc s;
    s.sch = compile_ok({"{\"properties\":{\"t\":{\"format\":\"time\"}}}"});
    ASSERT_NE(s.sch, nullptr);
    /* the crate's TIME_RE wants an offset on strings */
    EXPECT_TRUE(valid(s.sch, "t = '07:32:00Z'\n"));
    EXPECT_TRUE(valid(s.sch, "t = '07:32:00+01:00'\n"));
    EXPECT_TRUE(valid(s.sch, "t = '07:32:00.123456Z'\n"));
    EXPECT_FALSE(valid(s.sch, "t = '07:32:00'\n"));
    EXPECT_FALSE(valid(s.sch, "t = '24:32:00Z'\n"));
    /* a TOML local time value still satisfies the format by kind
     * (policy 3; taplo rejects it through its JSON round trip) */
    /* a TOML local time value satisfies time-local, not the
     * RFC 3339 full-time (the kind table) */
    SchemaDoc tll;
    tll.sch = compile_ok({"{\"properties\":{\"t\":{\"format\":\"time-local\"}}}"});
    ASSERT_NE(tll.sch, nullptr);
    EXPECT_TRUE(valid(tll.sch, "t = 07:32:00\n"));
    EXPECT_FALSE(valid(s.sch, "t = 07:32:00\n"));
}

TEST(SchemaFormats, EmailAndHostname)
{
    SchemaDoc s;
    s.sch = compile_ok(
        {"{\"properties\":{\"e\":{\"format\":\"email\"},"
         "\"h\":{\"format\":\"hostname\"}}}"});
    ASSERT_NE(s.sch, nullptr);
    EXPECT_TRUE(valid(s.sch, "e = 'a@b'\n"));
    EXPECT_TRUE(valid(s.sch, "e = 'first.last@sub.domain'\n"));
    EXPECT_FALSE(valid(s.sch, "e = 'no-at-sign'\n"));
    EXPECT_FALSE(valid(s.sch, "e = '.a@b'\n"));
    EXPECT_FALSE(valid(s.sch, "e = 'a..b@c'\n"));
    EXPECT_FALSE(valid(s.sch, "e = 'a.@b'\n"));
    EXPECT_TRUE(valid(s.sch, "h = 'example.com'\n"));
    EXPECT_TRUE(valid(s.sch, "h = 'a-b.example'\n"));
    EXPECT_FALSE(valid(s.sch, "h = '-a.b'\n"));
    EXPECT_FALSE(valid(s.sch, "h = 'a.b-'\n"));
    EXPECT_FALSE(valid(s.sch, "h = ''\n"));
    EXPECT_FALSE(valid(s.sch, "h = 'a..b'\n")); /* empty labels: the
        unified suite's hostname grammar is strict (the old crate-
        parity pin predated the 2026 suite reorganization) */
    EXPECT_FALSE(valid(s.sch, "h = '.'\n"));
    EXPECT_FALSE(valid(s.sch, "h = '.example'\n"));
    EXPECT_FALSE(valid(s.sch, "h = 'example.'\n"));
}

TEST(SchemaFormats, IpAddresses)
{
    SchemaDoc s;
    s.sch = compile_ok(
        {"{\"properties\":{\"v4\":{\"format\":\"ipv4\"},"
         "\"v6\":{\"format\":\"ipv6\"}}}"});
    ASSERT_NE(s.sch, nullptr);
    EXPECT_TRUE(valid(s.sch, "v4 = '192.168.0.1'\n"));
    EXPECT_TRUE(valid(s.sch, "v4 = '0.0.0.0'\n"));
    EXPECT_FALSE(valid(s.sch, "v4 = '192.168.01.1'\n"));
    EXPECT_FALSE(valid(s.sch, "v4 = '192.168.0'\n"));
    EXPECT_FALSE(valid(s.sch, "v4 = '256.1.1.1'\n"));
    EXPECT_TRUE(valid(s.sch, "v6 = '::1'\n"));
    EXPECT_TRUE(valid(s.sch, "v6 = '2001:db8::8a2e:370:7334'\n"));
    EXPECT_TRUE(valid(s.sch, "v6 = '::ffff:192.168.0.1'\n"));
    EXPECT_TRUE(valid(s.sch, "v6 = '1:2:3:4:5:6:7:8'\n"));
    EXPECT_FALSE(valid(s.sch, "v6 = '1:2:3:4:5:6:7'\n"));
    EXPECT_FALSE(valid(s.sch, "v6 = '1::2::3'\n"));
    EXPECT_FALSE(valid(s.sch, "v6 = '1:2:3:4:5:6:7:8:9'\n"));
}

TEST(SchemaFormats, PointersAndUuidAndUri)
{
    SchemaDoc s;
    s.sch = compile_ok(
        {"{\"properties\":{\"p\":{\"format\":\"json-pointer\"},"
         "\"r\":{\"format\":\"relative-json-pointer\"},"
         "\"u\":{\"format\":\"uuid\"},"
         "\"w\":{\"format\":\"uri\"}}}"});
    ASSERT_NE(s.sch, nullptr);
    EXPECT_TRUE(valid(s.sch, "p = ''\n"));
    EXPECT_TRUE(valid(s.sch, "p = '/a/b~1c~0d'\n"));
    EXPECT_FALSE(valid(s.sch, "p = 'a/b'\n"));
    EXPECT_FALSE(valid(s.sch, "p = '/a~2'\n"));
    EXPECT_TRUE(valid(s.sch, "r = '0'\n")); /* the bare integer IS a
        valid relative pointer (the unified suite's stance; the old
        pin demanded a # or tail) */
    EXPECT_TRUE(valid(s.sch, "r = '0#'\n"));
    EXPECT_TRUE(valid(s.sch, "r = '1/a/b'\n"));
    EXPECT_TRUE(valid(s.sch, "u = '123e4567-e89b-12d3-a456-426614174000'\n"));
    /* the unified suite's uuid stance: exactly 8-4-4-4-12; the
     * undashed 32-hex form is not a uuid-literal (was crate-parity) */
    EXPECT_FALSE(valid(s.sch, "u = '123e4567e89b12d3a456426614174000'\n"));
    EXPECT_FALSE(valid(s.sch, "u = '123e4567-e89b-12d3-a456'\n"));
    EXPECT_TRUE(valid(s.sch, "w = 'https://example.com/x?y=1'\n"));
    EXPECT_TRUE(valid(s.sch, "w = 'mailto:a@b.c'\n"));
    EXPECT_FALSE(valid(s.sch, "w = 'example.com'\n"));
    EXPECT_FALSE(valid(s.sch, "w = '1http://a'\n"));
}

TEST(SchemaFormats, RegexAndDurationAndTemplate)
{
    SchemaDoc s;
    s.sch = compile_ok(
        {"{\"properties\":{\"r\":{\"format\":\"regex\"},"
         "\"d\":{\"format\":\"duration\"},"
         "\"t\":{\"format\":\"uri-template\"}}}"});
    ASSERT_NE(s.sch, nullptr);
    EXPECT_TRUE(valid(s.sch, "r = '^[a-z]+$'\n"));
    EXPECT_FALSE(valid(s.sch, "r = '[unclosed'\n"));
    EXPECT_TRUE(valid(s.sch, "d = 'P1Y2M3DT4H5M6S'\n"));
    EXPECT_TRUE(valid(s.sch, "d = 'P1W'\n"));
    EXPECT_TRUE(valid(s.sch, "d = 'PT1M2S'\n"));
    /* the unified suite's strict grammar: no sign, no fraction,
     * no reorder, weeks sole-only (the old permissive pin
     * '-PT0.5S' predated the 2026 reorganization) */
    EXPECT_FALSE(valid(s.sch, "d = '-PT0.5S'\n"));
    EXPECT_FALSE(valid(s.sch, "d = 'PT0.5S'\n"));
    EXPECT_FALSE(valid(s.sch, "d = 'P1Y2W'\n"));
    EXPECT_FALSE(valid(s.sch, "d = 'PT1H2S'\n"));
    EXPECT_FALSE(valid(s.sch, "d = '1 day'\n"));
    EXPECT_FALSE(valid(s.sch, "d = 'P'\n"));
    EXPECT_TRUE(valid(s.sch, "t = 'http://example.com/{id}'\n"));
    EXPECT_TRUE(valid(s.sch, "t = '/users/{id}/tags{?q}'\n"));
    EXPECT_FALSE(valid(s.sch, "t = 'http://x/{no-closing'\n"));
    EXPECT_FALSE(valid(s.sch, "t = 'a b'\n")); /* a space is a bad literal */
}

TEST(SchemaFormats, SemverFamily)
{
    SchemaDoc s;
    s.sch = compile_ok(
        {"{\"properties\":{\"v\":{\"format\":\"semver\"},"
         "\"r\":{\"format\":\"semver-requirement\"}}}"});
    ASSERT_NE(s.sch, nullptr);
    EXPECT_TRUE(valid(s.sch, "v = '1.2.3'\n"));
    EXPECT_TRUE(valid(s.sch, "v = '0.0.0'\n"));
    EXPECT_TRUE(valid(s.sch, "v = '1.2.3-alpha.1+b7'\n"));
    EXPECT_FALSE(valid(s.sch, "v = '1.2'\n"));
    EXPECT_FALSE(valid(s.sch, "v = '01.2.3'\n"));
    EXPECT_FALSE(valid(s.sch, "v = '1.02.3'\n"));
    EXPECT_TRUE(valid(s.sch, "r = '^1.2'\n"));
    EXPECT_TRUE(valid(s.sch, "r = '>= 1.2, < 1.5'\n"));
    EXPECT_TRUE(valid(s.sch, "r = '1.2.*'\n"));
    EXPECT_TRUE(valid(s.sch, "r = '*'\n"));
    EXPECT_TRUE(valid(s.sch, "r = '~1.2.3'\n"));
    EXPECT_FALSE(valid(s.sch, "r = ''\n"));
    EXPECT_FALSE(valid(s.sch, "r = 'not a req'\n"));
    EXPECT_FALSE(valid(s.sch, "r = '>=,'\n"));
}

/* ============================================ 2026 SOTA surface */

TEST(SchemaUneval, AdjacentKeywordsEvaluate)
{
    SchemaDoc s;
    s.sch = compile_ok(
        {"{\"properties\":{\"a\":{}},\"unevaluatedProperties\":false}"});
    ASSERT_NE(s.sch, nullptr);
    EXPECT_TRUE(valid(s.sch, "a = 1\n"));
    EXPECT_FALSE(valid(s.sch, "a = 1\nb = 2\n"));
    EXPECT_EQ(first_path(validate_str(s.sch, "a = 1\nb = 2\n")), "b");

    SchemaDoc arr;
    arr.sch = compile_ok(
        {"{\"properties\":{\"x\":{\"prefixItems\":[{\"type\":\"integer\"}],"
         "\"items\":false,\"unevaluatedItems\":false}}}"});
    ASSERT_NE(arr.sch, nullptr);
    EXPECT_TRUE(valid(arr.sch, "x = [1]\n"));
    EXPECT_FALSE(valid(arr.sch, "x = [1, 2]\n"));
}

TEST(SchemaUneval, CousinsDoNotShareAnnotations)
{
    /* the official suite: "unevaluatedProperties can't see inside
     * cousins" (both orders) */
    SchemaDoc a;
    a.sch = compile_ok(
        {"{\"allOf\":[{\"properties\":{\"a\":{}}},"
         "{\"unevaluatedProperties\":false}]}"});
    ASSERT_NE(a.sch, nullptr);
    EXPECT_FALSE(valid(a.sch, "a = 1\n"));

    SchemaDoc b;
    b.sch = compile_ok(
        {"{\"allOf\":[{\"unevaluatedProperties\":false},"
         "{\"properties\":{\"a\":{}}}]}"});
    ASSERT_NE(b.sch, nullptr);
    EXPECT_FALSE(valid(b.sch, "a = 1\n"));

    /* the same-object adjacency DOES hold: nested properties */
    SchemaDoc nested;
    nested.sch = compile_ok(
        {"{\"properties\":{\"a\":{}},"
         "\"allOf\":[{\"additionalProperties\":{\"type\":\"integer\"}}],"
         "\"unevaluatedProperties\":false}"});
    ASSERT_NE(nested.sch, nullptr);
    EXPECT_TRUE(valid(nested.sch, "a = 1\nb = 2\n"));
    EXPECT_FALSE(valid(nested.sch, "a = 1\nb = 'x'\n"));
}

TEST(SchemaUneval, AnyOfBranchesBubbleAndTrialsDoNot)
{
    /* a successful anyOf branch's annotations bubble up to the
     * enclosing object */
    SchemaDoc s;
    s.sch = compile_ok(
        {"{\"anyOf\":[{\"properties\":{\"a\":{\"type\":\"integer\"}}},"
         "{\"properties\":{\"b\":{}},\"additionalProperties\":false}],"
         "\"unevaluatedProperties\":false}"});
    ASSERT_NE(s.sch, nullptr);
    EXPECT_TRUE(valid(s.sch, "b = 1\n")); /* branch 2 annotates b */
    EXPECT_TRUE(valid(s.sch, "a = 1\n")); /* branch 1 annotates a */
    EXPECT_FALSE(valid(s.sch, "c = 1\n"));

    /* failed anyOf branches annotate nothing */
    SchemaDoc f;
    f.sch = compile_ok(
        {"{\"anyOf\":[{\"properties\":{\"a\":{\"type\":\"integer\"}}},true],"
         "\"unevaluatedProperties\":false}"});
    ASSERT_NE(f.sch, nullptr);
    EXPECT_TRUE(valid(f.sch, "a = 1\n"));
    /* branch 1 rejects the string; the pass-through fallback annotates
     * nothing -> a is unevaluated */
    EXPECT_FALSE(valid(f.sch, "a = 'x'\n"));
}

TEST(SchemaUneval, UnevalTrueAnnotatesAndNotDiscards)
{
    /* unevaluatedProperties:true annotates what it applied to */
    SchemaDoc s;
    s.sch = compile_ok(
        {"{\"properties\":{\"a\":{}},"
         "\"allOf\":[{\"unevaluatedProperties\":true}],"
         "\"unevaluatedProperties\":false}"});
    ASSERT_NE(s.sch, nullptr);
    EXPECT_TRUE(valid(s.sch, "a = 1\nb = 2\n"));

    /* the not-trial's annotations are discarded */
    SchemaDoc n;
    n.sch = compile_ok(
        {"{\"properties\":{\"a\":{}},\"not\":{\"properties\":{\"b\":{}}},"
         "\"unevaluatedProperties\":false}"});
    ASSERT_NE(n.sch, nullptr);
    EXPECT_FALSE(valid(n.sch, "a = 1\nb = 2\n"));
}

TEST(SchemaUneval, ContainsAnnotatesIndices)
{
    SchemaDoc s;
    s.sch = compile_ok(
        {"{\"properties\":{\"x\":{\"unevaluatedItems\":false,"
         "\"contains\":{\"type\":\"integer\"}}}}"});
    ASSERT_NE(s.sch, nullptr);
    EXPECT_TRUE(valid(s.sch, "x = [1, 2]\n"));
    EXPECT_FALSE(valid(s.sch, "x = [1, 's', 2]\n"));
    EXPECT_EQ(first_path(validate_str(s.sch, "x = [1, 's', 2]\n")), "x.1");
}

TEST(SchemaDynamic, InnermostAnchorWins)
{
    /* the canonical 2020-12 tree/leaf pair */
    SchemaDoc tree;
    tree.sch = compile_ok(
        {"{\"$dynamicAnchor\":\"node\",\"type\":\"object\","
         "\"properties\":{\"value\":{\"type\":\"integer\"},"
         "\"children\":{\"type\":\"array\",\"items\":"
         "{\"$dynamicRef\":\"#node\"}}}}"});
    ASSERT_NE(tree.sch, nullptr);
    EXPECT_TRUE(valid(tree.sch,
                      "value = 1\n[[children]]\nvalue = 2\n"
                      "[[children.children]]\nvalue = 3\n"));
    EXPECT_FALSE(valid(tree.sch, "value = 1\n[[children]]\nvalue = 'x'\n"));

    /* a leaf-anchored document re-binds the dynamic ref */
    SchemaDoc leaf;
    leaf.sch = compile_ok(
        {"{\"$dynamicAnchor\":\"node\",\"type\":\"object\","
         "\"properties\":{\"value\":{\"type\":\"string\"},"
         "\"children\":false}}"});
    ASSERT_NE(leaf.sch, nullptr);
    /* reuse the tree schema but swap the anchor: children carries no
     * value here, the ref binds to the leaf -> integers under
     * children fail the leaf's own shape */
    SchemaDoc hybrid;
    hybrid.sch = compile_ok(
        {"{\"$dynamicAnchor\":\"node\",\"type\":\"object\","
         "\"properties\":{\"value\":{\"type\":\"integer\"},"
         "\"children\":{\"type\":\"array\",\"items\":"
         "{\"$dynamicRef\":\"#node\"}}}}",
         });
    (void)hybrid;
    EXPECT_FALSE(valid(leaf.sch, "value = 'leaf'\n[[children]]\nvalue = 99\n"));
}

TEST(SchemaRegex, UnicodePropertyEscapes)
{
    SchemaDoc letter;
    letter.sch = compile_ok(
        "{\"properties\":{\"k\":{\"pattern\":\"^\\\\p{Letter}+$\"}}}");
    ASSERT_NE(letter.sch, nullptr);
    EXPECT_TRUE(valid(letter.sch, "k = 'Hello'\n"));
    EXPECT_TRUE(valid(letter.sch, "k = '\xcf\x80'\n")); /* π */
    EXPECT_FALSE(valid(letter.sch, "k = '123'\n"));

    /* negated forms: \P and \p{^...} */
    SchemaDoc neg;
    neg.sch = compile_ok(
        "{\"properties\":{\"k\":{\"pattern\":\"^\\\\P{Letter}+$\"}}}");
    ASSERT_NE(neg.sch, nullptr);
    EXPECT_TRUE(valid(neg.sch, "k = '123'\n"));
    EXPECT_FALSE(valid(neg.sch, "k = 'abc'\n"));

    /* properties compose inside classes */
    SchemaDoc mix;
    mix.sch = compile_ok(
        "{\"properties\":{\"k\":{\"pattern\":"
        "\"^[\\\\p{Letter}\\\\d]+$\"}}}");
    ASSERT_NE(mix.sch, nullptr);
    EXPECT_TRUE(valid(mix.sch, "k = 'a1\xcf\x80'\n"));
    EXPECT_FALSE(valid(mix.sch, "k = 'a-\xcf\x80'\n"));

    /* an unknown property names itself at compile (the contract:
     * unsupported constructs are rejected, never ignored) */
    std::string diag = compile_diag(
        "{\"properties\":{\"k\":{\"pattern\":\"^\\\\p{Greek}$\"}}}");
    EXPECT_NE(diag.find("unknown"), std::string::npos);
}

TEST(SchemaFormat, DialectGating)
{
    /* the practical default (no $schema): assert */
    SchemaDoc plain;
    plain.sch = compile_ok(
        "{\"properties\":{\"k\":{\"format\":\"email\"}}}");
    ASSERT_NE(plain.sch, nullptr);
    EXPECT_FALSE(valid(plain.sch, "k = '2962'\n"));
    EXPECT_TRUE(valid(plain.sch, "k = 'a@b.co'\n"));

    /* a declared standard dialect: the spec's annotation default */
    SchemaDoc anno;
    anno.sch = compile_ok(
        "{\"$schema\":\"https://json-schema.org/draft/2020-12/schema\","
        "\"properties\":{\"k\":{\"format\":\"email\"}}}");
    ASSERT_NE(anno.sch, nullptr);
    EXPECT_TRUE(valid(anno.sch, "k = '2962'\n"));

    /* the sanctioned opt-back-in: the format-assertion dialect */
    SchemaDoc assert_;
    assert_.sch = compile_ok(
        "{\"$schema\":\"https://json-schema.org/draft/2020-12/"
        "format-assertion\",\"properties\":{\"k\":{\"format\":"
        "\"email\"}}}");
    ASSERT_NE(assert_.sch, nullptr);
    EXPECT_FALSE(valid(assert_.sch, "k = '2962'\n"));
    EXPECT_TRUE(valid(assert_.sch, "k = 'a@b.co'\n"));

    /* content* follows the same rule */
    SchemaDoc content;
    content.sch = compile_ok(
        "{\"$schema\":\"https://json-schema.org/draft/2020-12/schema\","
        "\"properties\":{\"k\":{\"contentMediaType\":"
        "\"application/json\"}}}");
    ASSERT_NE(content.sch, nullptr);
    EXPECT_TRUE(valid(content.sch, "k = '{:}'\n"));
    SchemaDoc content_plain;
    content_plain.sch = compile_ok(
        "{\"properties\":{\"k\":{\"contentMediaType\":"
        "\"application/json\"}}}");
    ASSERT_NE(content_plain.sch, nullptr);
    EXPECT_FALSE(valid(content_plain.sch, "k = '{:}'\n"));
}

TEST(SchemaCompile, BuiltinMetaschemas)
{
    /* the vendored metaschemas resolve as built-in resources: a
     * parser library never touches the network */
    SchemaDoc d20;
    d20.sch = compile_ok(
        "{\"$ref\":\"https://json-schema.org/draft/2020-12/schema\"}");
    ASSERT_NE(d20.sch, nullptr);
    EXPECT_TRUE(valid(d20.sch, "type = 'string'\n"));
    EXPECT_FALSE(valid(d20.sch, "type = 123\n"));

    SchemaDoc d19;
    d19.sch = compile_ok(
        "{\"$ref\":\"https://json-schema.org/draft/2019-09/schema\"}");
    ASSERT_NE(d19.sch, nullptr);
    EXPECT_TRUE(valid(d19.sch, "title = 'x'\n"));

    /* the legacy draft-07 URL with the empty-fragment form */
    SchemaDoc d7;
    d7.sch = compile_ok(
        "{\"$ref\":\"http://json-schema.org/draft-07/schema#\"}");
    ASSERT_NE(d7.sch, nullptr);
    EXPECT_TRUE(valid(d7.sch, "description = 'd'\n"));
}

TEST(SchemaCompile, VocabularyGating)
{
    /* a custom metaschema that keeps only core + applicator: the
     * validation vocabulary's keywords become annotations */
    /* cwd-relative: the CI matrix runs this on POSIX and Windows */
    const char *meta_path = "teptris_meta_no_validation.json";
    {
        std::FILE *f = std::fopen(meta_path, "wb");
        ASSERT_NE(f, nullptr);
        const char *meta =
            "{\"$schema\":\"https://json-schema.org/draft/2020-12/schema\","
            "\"$id\":\"teptris_meta_no_validation.json\","
            "\"$vocabulary\":{"
            "\"https://json-schema.org/draft/2020-12/vocab/core\":true,"
            "\"https://json-schema.org/draft/2020-12/vocab/applicator\":true},"
            "\"$dynamicAnchor\":\"meta\","
            "\"allOf\":[{\"$ref\":\"teptris_meta_core.json\"}]}";
        std::fputs(meta, f);
        std::fclose(f);
    }
    {
        std::FILE *f = std::fopen("teptris_meta_core.json", "wb");
        ASSERT_NE(f, nullptr);
        const char *core =
            "{\"$id\":\"teptris_meta_core.json\","
            "\"$anchor\":\"meta\","
            "\"type\":\"object\"}";
        std::fputs(core, f);
        std::fclose(f);
    }
    SchemaDoc s;
    s.sch = compile_ex_ok(
        "{\"$schema\":\"teptris_meta_no_validation.json\","
        "\"properties\":{\"k\":false},"
        "\"type\":\"array\",\"minProperties\":7}",
        ".");
    ASSERT_NE(s.sch, nullptr);
    /* applicator + boolean schemas still assert */
    EXPECT_FALSE(valid(s.sch, "k = 1\n"));
    /* the validation vocabulary is out of the dialect: type and
     * minProperties are annotations */
    EXPECT_TRUE(valid(s.sch, "a = 1\nb = 2\n"));
    std::remove(meta_path);
    std::remove("teptris_meta_core.json");
}

TEST(SchemaDynamic, RecursiveRef2019)
{
    SchemaDoc s;
    s.sch = compile_ok(
        {"{\"$recursiveAnchor\":true,\"type\":\"object\","
         "\"properties\":{\"v\":{\"type\":\"integer\"},"
         "\"kids\":{\"type\":\"array\",\"items\":{\"$recursiveRef\":\"#\"}}}}"});
    ASSERT_NE(s.sch, nullptr);
    EXPECT_TRUE(valid(s.sch, "v = 1\n[[kids]]\nv = 2\n[[kids.kids]]\nv = 3\n"));
    EXPECT_FALSE(valid(s.sch, "v = 1\n[[kids]]\nv = 'x'\n"));
}

TEST(SchemaDynamic, OutermostResourceWins)
{
    /* appendix C, embedded: the outermost resource that defines the
     * anchor wins, so the strict guard applies at every depth */
    SchemaDoc s;
    s.sch = compile_ex_ok(
        "{\"$id\":\"https://x/strict\",\"$dynamicAnchor\":\"node\","
        "\"$ref\":\"tree\",\"unevaluatedProperties\":false,"
        "\"$defs\":{\"tree\":{\"$id\":\"tree\","
        "\"$dynamicAnchor\":\"node\",\"type\":\"object\","
        "\"properties\":{\"data\":true,"
        "\"children\":{\"type\":\"array\",\"items\":"
        "{\"$dynamicRef\":\"#node\"}}}}}}",
        ".");
    ASSERT_NE(s.sch, nullptr);
    EXPECT_TRUE(valid(s.sch, "children = [{ data = 1 }]\n"));
    EXPECT_FALSE(valid(s.sch, "children = [{ daat = 1 }]\n"));
    EXPECT_FALSE(valid(s.sch,
                      "children = [{ children = [{ daat = 1 }] }]\n"));
}

TEST(SchemaDynamic, MultipleDynamicPaths)
{
    SchemaDoc s;
    s.sch = compile_ex_ok(
        "{\"$id\":\"https://x/main\","
        "\"if\":{\"properties\":{\"kindOfList\":{\"const\":\"numbers\"}},"
        "\"required\": [\"kindOfList\"]},"
        "\"then\":{\"$ref\":\"numberList\"},"
        "\"else\":{\"$ref\":\"stringList\"},"
        "\"$defs\":{"
        "\"genericList\":{\"$id\":\"genericList\","
        "\"properties\":{\"list\":{\"items\":"
        "{\"$dynamicRef\":\"#itemType\"}}}},"
        "\"numberList\":{\"$id\":\"numberList\",\"$defs\":{"
        "\"itemType\":{\"$dynamicAnchor\":\"itemType\","
        "\"type\":\"number\"}},\"$ref\":\"genericList\"},"
        "\"stringList\":{\"$id\":\"stringList\",\"$defs\":{"
        "\"itemType\":{\"$dynamicAnchor\":\"itemType\","
        "\"type\":\"string\"}},\"$ref\":\"genericList\"}}}",
        ".");
    ASSERT_NE(s.sch, nullptr);
    EXPECT_TRUE(valid(s.sch, "kindOfList = 'numbers'\nlist = [1.1]\n"));
    EXPECT_FALSE(valid(s.sch, "kindOfList = 'numbers'\nlist = ['foo']\n"));
    EXPECT_FALSE(valid(s.sch, "kindOfList = 'strings'\nlist = [1.1]\n"));
    EXPECT_TRUE(valid(s.sch, "kindOfList = 'strings'\nlist = ['foo']\n"));
}

TEST(SchemaDynamic, SkipsIntermediateResources)
{
    /* the reference path enters the item resource directly, skipping
     * bar: the item's own bookend anchor applies (integer) */
    SchemaDoc s;
    s.sch = compile_ok(
        {"{\"$id\":\"https://x/main\",\"type\":\"object\","
         "\"properties\":{\"bar-item\":{\"$ref\":\"item\"}},"
         "\"$defs\":{\"bar\":{\"$id\":\"bar\",\"type\":\"array\","
         "\"items\":{\"$ref\":\"item\"}},"
         "\"item\":{\"$id\":\"item\",\"type\":\"object\","
         "\"properties\":{\"content\":{\"$dynamicRef\":\"#content\"}},"
         "\"$defs\":{\"defaultContent\":{\"$dynamicAnchor\":\"content\","
         "\"type\":\"integer\"}}}}}"});
    ASSERT_NE(s.sch, nullptr);
    EXPECT_TRUE(valid(s.sch, "'bar-item' = { content = 42 }\n"));
    EXPECT_FALSE(valid(s.sch, "'bar-item' = { content = 'value' }\n"));
}

TEST(SchemaContent, ContentSchemaAsserts)
{
    SchemaDoc s;
    s.sch = compile_ok(
        {"{\"properties\":{\"k\":{\"contentEncoding\":\"base64\","
         "\"contentMediaType\":\"application/json\","
         "\"contentSchema\":{\"type\":\"object\",\"required\":[\"n\"],"
         "\"properties\":{\"n\":{\"type\":\"integer\"}}}}}}"});
    ASSERT_NE(s.sch, nullptr);
    EXPECT_TRUE(valid(s.sch, "k = 'eyJuIjogMX0='\n")); /* {"n": 1} */
    EXPECT_FALSE(valid(s.sch, "k = 'eyJhIjogMX0='\n")); /* {"a": 1}: no n */
    EXPECT_FALSE(valid(s.sch, "k = 'W10='\n")); /* []: parses but no n */

    /* contentSchema without a media type sees the decoded string */
    SchemaDoc raw;
    raw.sch = compile_ok(
        {"{\"properties\":{\"k\":{\"contentEncoding\":\"base64\","
         "\"contentSchema\":{\"type\":\"string\",\"minLength\":3}}}}"});
    ASSERT_NE(raw.sch, nullptr);
    EXPECT_TRUE(valid(raw.sch, "k = 'aGVsbG8='\n")); /* hello */
    EXPECT_FALSE(valid(raw.sch, "k = 'aGk='\n"));    /* hi */
}
