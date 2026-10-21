#include "teptris/teptris.h"

#include "util.hpp"

TEST(Errors, LineColumn)
{
    teptris_document *doc = nullptr;
    teptris_status st = parse_status("a = 1\nb = ?\n", &doc);
    EXPECT_EQ(st, TEPTRIS_ERR_SYNTAX);
    DocGuard g(doc);
    const teptris_error *e = teptris_document_error(doc);
    EXPECT_EQ(e->line, 2u);
    EXPECT_EQ(e->column, 5u);

    teptris_document *doc2 = nullptr;
    st = parse_status("a = 1\n\n  c = @bad\n", &doc2);
    EXPECT_EQ(st, TEPTRIS_ERR_SYNTAX);
    DocGuard g2(doc2);
    e = teptris_document_error(doc2);
    EXPECT_EQ(e->line, 3u);
    EXPECT_EQ(e->column, 7u);
}

TEST(Errors, UnterminatedStringPointsAtLine)
{
    teptris_document *doc = nullptr;
    teptris_status st = parse_status("ok = 1\nbad = \"unterminated\n", &doc);
    EXPECT_EQ(st, TEPTRIS_ERR_SYNTAX);
    DocGuard g(doc);
    const teptris_error *e = teptris_document_error(doc);
    EXPECT_EQ(e->line, 2u);
}

TEST(Errors, Encoding)
{
    const char in[] = {'a', ' ', '=', ' ', (char)0xFF, '\n'};
    teptris_document *doc = nullptr;
    teptris_status st = parse_status(std::string(in, sizeof(in)), &doc);
    EXPECT_EQ(st, TEPTRIS_ERR_ENCODING);
    DocGuard g(doc);

    /* truncated UTF-8 */
    const char trunc[] = {'a', ' ', '=', ' ', '"', (char)0xE2, (char)0x82,
                          '"', '\n'};
    teptris_document *doc2 = nullptr;
    st = parse_status(std::string(trunc, sizeof(trunc)), &doc2);
    EXPECT_EQ(st, TEPTRIS_ERR_ENCODING);
    DocGuard g2(doc2);
}

TEST(Errors, BOMAccepted)
{
    const char in[] = {(char)0xEF, (char)0xBB, (char)0xBF, 'a', ' ', '=',
                       ' ', '1', '\n'};
    std::string input(in, sizeof(in));
    teptris_document *doc = nullptr;
    EXPECT_EQ(parse_status(input, &doc), TEPTRIS_OK);
    DocGuard g(doc);
    int64_t v = 0;
    teptris_node_integer(get(teptris_document_root(doc), "a"), &v);
    EXPECT_EQ(v, 1);
}

TEST(Errors, LoneCarriageReturn)
{
    teptris_document *doc = nullptr;
    EXPECT_EQ(parse_status("a = 1\rb = 2\n", &doc), TEPTRIS_ERR_SYNTAX);
    DocGuard g(doc);
}

TEST(Errors, ControlInComment)
{
    teptris_document *doc = nullptr;
    std::string in = "# comment\x01\na = 1\n";
    EXPECT_EQ(parse_status(in, &doc), TEPTRIS_ERR_SYNTAX);
    DocGuard g(doc);
}

TEST(Errors, DepthGuard)
{
    teptris_document *doc = nullptr;
    std::string in = "a = ";
    in.append(600, '[');
    in.append(600, ']');
    teptris_status st = parse_status(in, &doc);
    EXPECT_EQ(st, TEPTRIS_ERR_DEPTH);
    DocGuard g(doc);
}

TEST(Errors, DepthOptionRaisesLimit)
{
    std::string in = "a = ";
    in.append(600, '[');
    in.append(600, ']');
    teptris_options opts;
    opts.max_depth = 1000;
    memset(opts._reserved, 0, sizeof(opts._reserved));
    teptris_document *doc = nullptr;
    teptris_status st = teptris_parse(in.data(), in.size(), &opts, &doc);
    EXPECT_EQ(st, TEPTRIS_OK);
    DocGuard g(doc);
}

TEST(Errors, TrailingGarbage)
{
    const char *bad[] = {
        "a = 1 b = 2",
        "a = 1true",
        "a = truex",
        "[a] extra",
        "= 1",
        "a =",
    };
    for (const char *in : bad) {
        teptris_document *doc = nullptr;
        teptris_status st = parse_status(in, &doc);
        EXPECT_EQ(st, TEPTRIS_ERR_SYNTAX) << "input: " << in;
        DocGuard g(doc);
    }
}

TEST(Errors, MissingTrailingNewlineOk)
{
    teptris_document *doc = nullptr;
    EXPECT_EQ(parse_status("a = 1", &doc), TEPTRIS_OK);
    DocGuard g(doc);
}

TEST(Errors, ByteOffsetSpans)
{
    /* E5: every error carries a machine-readable point span - the
     * byte offset of the offending position, start == end */
    struct {
        const char *in;
        size_t offset;
    } cases[] = {
        {"a = ", 4},        /* at EOF, after '=' */
        {"a = 1\nb = = 2", 10}, /* the value position at the first '=' */
        {"a\x01 = 1", 1},   /* the invalid control byte */
    };
    for (auto &c : cases) {
        teptris_document *doc = nullptr;
        teptris_status st = teptris_parse(c.in, strlen(c.in), nullptr, &doc);
        EXPECT_NE(st, TEPTRIS_OK) << "input: " << c.in;
        ASSERT_NE(doc, nullptr);
        DocGuard g(doc);
        const teptris_error *e = teptris_document_error(doc);
        EXPECT_EQ(e->start_offset, c.offset) << "input: " << c.in;
        EXPECT_EQ(e->end_offset, e->start_offset) << "input: " << c.in;
    }
}
