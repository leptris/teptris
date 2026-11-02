/* The schema layer: compile-time contract (unsupported constructs are
 * rejected, never ignored), the four TOML/JSON incompatibility
 * policies, every supported keyword class, the recursive-$ref memo,
 * the error envelope shape, and the validate/validate_doc parity. */
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
    EXPECT_NE(compile_diag("{\"type\":\"null\"}").find("unknown type"),
              std::string::npos);
    SchemaDoc ok;
    ok.sch = compile_ok("{\"type\":[\"string\",\"number\"]}");
    ASSERT_NE(ok.sch, nullptr);
    /* a quoted "null" inside the array is a type NAME, not absence */
    EXPECT_NE(compile_diag("{\"type\":[\"string\",\"null\"]}").find("unknown"),
              std::string::npos);
}

TEST(SchemaCompile, RejectsUnsupportedKeywords)
{
    EXPECT_NE(
        compile_diag("{\"unevaluatedProperties\":false}").find("unsupported"),
        std::string::npos);
    EXPECT_NE(
        compile_diag("{\"unevaluatedItems\":false}").find("unsupported"),
        std::string::npos);
    EXPECT_NE(compile_diag("{\"$dynamicRef\":\"#x\"}").find("unsupported"),
              std::string::npos);
    EXPECT_NE(compile_diag({"{\"$recursiveRef\":\"#\"}"}).find("unsupported"),
              std::string::npos);
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
    EXPECT_EQ(compile_ok("{\"properties\":{\"k\":{\"multipleOf\":0.5}}}") != nullptr, true);
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
    EXPECT_FALSE(valid(s.sch, "k = 1.0\n")); /* float never integer */
    EXPECT_FALSE(valid(s.sch, "k = 1e3\n"));
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
    EXPECT_TRUE(valid(t.sch, "k = 03:04:05\n"));
    EXPECT_TRUE(valid(t.sch, "k = 03:04:05.123\n"));
    EXPECT_FALSE(valid(t.sch, "k = 2026-01-02\n"));
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
    /* type-strict equality: 1 and 1.0 differ */
    EXPECT_TRUE(valid(s.sch, "k = [1, 1.0]\n"));
    EXPECT_FALSE(valid(s.sch, "k = [1, 1]\n"));
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
    /* 1.0 is a float; the enum's 1 is an integer */
    EXPECT_FALSE(valid(s.sch, "k = 1.0\n"));
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
