/* Format-preserving scalar edit: byte-identity of everything except
 * the spliced value; section scoping; the v1 rejections. */
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

TEST(Edit, V1Rejections)
{
    char *out = nullptr;
    size_t olen = 0;
    const char *ml = "m = \"\"\"\nline\n\"\"\"\n";
    EXPECT_EQ(teptris_edit_scalar(ml, strlen(ml), "m", "x", &out, &olen),
              TEPTRIS_ERR_ARG);
    const char *inl = "t = {a = 1}\n";
    EXPECT_EQ(teptris_edit_scalar(inl, strlen(inl), "t", "x", &out, &olen),
              TEPTRIS_ERR_ARG);
    const char *arr = "a = [1, 2]\n";
    EXPECT_EQ(teptris_edit_scalar(arr, strlen(arr), "a", "x", &out, &olen),
              TEPTRIS_ERR_ARG);
    /* a multiline replacement cannot splice into a single line */
    EXPECT_EQ(teptris_edit_scalar("a = 1\n", 6, "a", "\"\"\"\nx\n\"\"\"",
                                  &out, &olen),
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
