/* Format-preserving scalar edit: byte-identity of everything except
 * the spliced value; section scoping; the v2 spans (multiline,
 * inline) and their rejections. */
#include <cstring>
#include <string>

#include "teptris/teptris.h"
#include "util.hpp"

namespace {

std::string edit_ok(const std::string &src, const char *kp,
                    const char *repl)
{
    char *out = nullptr;
    size_t olen = 0;
    teptris_status st = teptris_edit_scalar(src.data(), src.size(), kp,
                                            repl, &out, &olen);
    EXPECT_EQ(st, TEPTRIS_OK);
    if (st != TEPTRIS_OK) {
        return {};
    }
    std::string got(out, olen);
    free(out);
    return got;
}

} /* namespace */

TEST(Edit, PreservesEverythingButTheValue)
{
    std::string src = "# header comment\n"
                      "version = \"1.2.3\"   # trailing\n"
                      "\n"
                      "[package]\n"
                      "name = \"demo\"\n";
    std::string got = edit_ok(src, "version", "\"2.0.0\"");
    EXPECT_EQ(got,
              "# header comment\n"
              "version = \"2.0.0\"   # trailing\n"
              "\n"
              "[package]\n"
              "name = \"demo\"\n");
}

TEST(Edit, SectionScopedAndNested)
{
    std::string src = "[t]\nx = 1\ny = 2\n";
    EXPECT_EQ(edit_ok(src, "t.x", "42"), "[t]\nx = 42\ny = 2\n");
    std::string src2 = "[a.b]\nx = 1\n";
    EXPECT_EQ(edit_ok(src2, "a.b.x", "7"), "[a.b]\nx = 7\n");
}

TEST(Edit, DottedKeyLine)
{
    std::string src = "a.b = 1\nc = 2\n";
    EXPECT_EQ(edit_ok(src, "a.b", "9"), "a.b = 9\nc = 2\n");
}

TEST(Edit, TrailingCommentAndSpacingPreserved)
{
    std::string src = "v = 0.5 # ratio\n";
    EXPECT_EQ(edit_ok(src, "v", "0.75"), "v = 0.75 # ratio\n");
}

TEST(Edit, QuotedKey)
{
    std::string src = "\"my key\" = 1\n";
    EXPECT_EQ(edit_ok(src, "my key", "2"), "\"my key\" = 2\n");
}

TEST(Edit, NoMatchRejections)
{
    teptris_document *doc = nullptr; /* unused; silences style lints */
    (void)doc;
    const char *src = "[t]\nx = 1\n";
    char *out = nullptr;
    size_t olen = 0;
    /* wrong section: the root has no x */
    EXPECT_EQ(teptris_edit_scalar(src, strlen(src), "x", "9", &out, &olen),
              TEPTRIS_ERR_STATE);
    EXPECT_EQ(teptris_edit_scalar("a = 1\n", 6, "b", "9", &out, &olen),
              TEPTRIS_ERR_STATE);
}

TEST(Edit, MultilineStringTarget)
{
    std::string src = "m = \"\"\"\nline one\nline two\n\"\"\"  # keep\nz = 9\n";
    std::string got = edit_ok(src, "m", "\"new\"");
    EXPECT_EQ(got, "m = \"new\"  # keep\nz = 9\n");
    /* a literal multiline target splices too */
    std::string src2 = "q = '''\nraw\n'''\nz = 1\n";
    EXPECT_EQ(edit_ok(src2, "q", "3"), "q = 3\nz = 1\n");
}

TEST(Edit, InlineCollectionTargets)
{
    std::string src = "t = {a = 1, b = \"x\"}  # c\nz = 0\n";
    EXPECT_EQ(edit_ok(src, "t", "{a = 2, b = \"y\"}"),
              "t = {a = 2, b = \"y\"}  # c\nz = 0\n");
    std::string src2 = "arr = [\n  1,\n  2,\n]\nz = 0\n";
    EXPECT_EQ(edit_ok(src2, "arr", "[3, 4]"), "arr = [3, 4]\nz = 0\n");
    /* brackets inside strings do not close the collection */
    std::string src3 = "t = {s = \"]}\", n = 1}\nz = 0\n";
    EXPECT_EQ(edit_ok(src3, "t", "{s = \"<>\", n = 2}"),
              "t = {s = \"<>\", n = 2}\nz = 0\n");
}

TEST(Edit, MultilineReplacement)
{
    std::string repl = "\"\"\"\nline one\nline two\n\"\"\"";
    std::string got = edit_ok("a = 1\nz = 2\n", "a", repl.c_str());
    EXPECT_EQ(got, "a = \"\"\"\nline one\nline two\n\"\"\"\nz = 2\n");
}

TEST(Edit, CrlfSourceKeepsCrlf)
{
    std::string src = "# c\r\nversion = \"1.0\"\r\nport = 8080\r\n";
    std::string got = edit_ok(src, "port", "9090");
    EXPECT_EQ(got, "# c\r\nversion = \"1.0\"\r\nport = 9090\r\n");
}

TEST(Edit, V2Rejections)
{
    char *out = nullptr;
    size_t olen = 0;
    /* unterminated multiline string */
    const char *ml = "m = \"\"\"\nline\n";
    EXPECT_EQ(teptris_edit_scalar(ml, strlen(ml), "m", "x", &out, &olen),
              TEPTRIS_ERR_ARG);
    /* unbalanced inline collection */
    const char *inl = "t = {a = 1\n";
    EXPECT_EQ(teptris_edit_scalar(inl, strlen(inl), "t", "x", &out, &olen),
              TEPTRIS_ERR_ARG);
    /* a string-target splice that leaves garbage rejects (verified):
     * the span covers only "a", the junk after it breaks the parse */
    const char *junk = "k = \"a\" trailing_garbage\n";
    EXPECT_EQ(teptris_edit_scalar(junk, strlen(junk), "k", "\"b\"", &out,
                                  &olen),
              TEPTRIS_ERR_ARG);
}

TEST(Edit, BadReplacementRejected)
{
    char *out = nullptr;
    size_t olen = 0;
    EXPECT_EQ(teptris_edit_scalar("a = 1\n", 6, "a", "@@@", &out, &olen),
              TEPTRIS_ERR_ARG);
    EXPECT_EQ(teptris_edit_scalar("a = 1\n", 6, "a", "", &out, &olen),
              TEPTRIS_ERR_ARG);
}

TEST(Edit, SplicedOutputStillParses)
{
    std::string src = "[deps]\nversion = \"1.0.0\"\nflag = false\n";
    std::string got = edit_ok(src, "deps.version", "\"2.0.0\"");
    teptris_document *doc = nullptr;
    ASSERT_EQ(teptris_parse(got.data(), got.size(), nullptr, &doc),
              TEPTRIS_OK);
    DocGuard g(doc);
}

TEST(Edit, DottedKeyUnderSection)
{
    /* the audit-9 find: a dotted key LINE continuing the open section
     * was unfindable (ERR_STATE) */
    std::string src = "[[items]]\na.b = 1\n\n[[items]]\nname = \"y\"\n";
    EXPECT_EQ(edit_ok(src, "items.a.b", "2"),
              "[[items]]\na.b = 2\n\n[[items]]\nname = \"y\"\n");
    std::string src2 = "[deep.path]\nc.d = \"x\"\n";
    EXPECT_EQ(edit_ok(src2, "deep.path.c.d", "\"y\""),
              "[deep.path]\nc.d = \"y\"\n");
}

TEST(Edit, NoTrailingNewlineAndAotFirstMatch)
{
    /* the value may be the last bytes of the file */
    EXPECT_EQ(edit_ok(std::string("a = 1\nb = 8080"), "b", "9090"),
              "a = 1\nb = 9090");
    /* two elements share the header: the FIRST match wins (pinned) */
    std::string src = "[[items]]\nname = \"x\"\n\n[[items]]\nname = \"y\"\n";
    EXPECT_EQ(edit_ok(src, "items.name", "\"z\""),
              "[[items]]\nname = \"z\"\n\n[[items]]\nname = \"y\"\n");
}
