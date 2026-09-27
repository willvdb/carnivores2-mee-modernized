// test_script_value_parse.cpp -- strict _RES.TXT scalar parsing

#include <cstdio>

#include <gtest/gtest.h>

#include "Loaders/ScriptBlockParse.h"
#include "Loaders/ScriptValueParse.h"

namespace {

class TempScript final
{
public:
    TempScript()
        : stream(tmpfile())
    {}

    ~TempScript()
    {
        if (stream)
            fclose(stream);
    }

    FILE* stream;
};

}  // namespace

TEST(ScriptValueParse, AcceptsLineEndingAndCommentTails)
{
    int integer = 0;
    EXPECT_TRUE(ParseScriptInt(" 42\r\n", integer));
    EXPECT_EQ(integer, 42);
    EXPECT_TRUE(ParseScriptInt("-7 // comment", integer));
    EXPECT_EQ(integer, -7);

    float decimal = 0.0f;
    EXPECT_TRUE(ParseScriptFloat(" 1.25\n", decimal));
    EXPECT_FLOAT_EQ(decimal, 1.25f);
}

TEST(ScriptValueParse, PreservesLegacyNumericPrefixParsing)
{
    // atoi/atof semantics: a valid numeric prefix wins and trailing text is
    // ignored, so legacy mod values such as `1.5f`, `1000.0`, or `4oops`
    // still load instead of aborting the hunt.
    int integer = 99;
    EXPECT_TRUE(ParseScriptInt("4oops", integer));
    EXPECT_EQ(integer, 4);
    EXPECT_TRUE(ParseScriptInt("1000.0", integer));
    EXPECT_EQ(integer, 1000);

    float decimal = 2.0f;
    EXPECT_TRUE(ParseScriptFloat("1.5oops", decimal));
    EXPECT_FLOAT_EQ(decimal, 1.5f);
    EXPECT_TRUE(ParseScriptFloat("1.0f", decimal));
    EXPECT_FLOAT_EQ(decimal, 1.0f);
}

TEST(ScriptValueParse, RejectsNonNumericAndNonFiniteValues)
{
    int integer = 99;
    EXPECT_FALSE(ParseScriptInt("oops", integer));
    EXPECT_FALSE(ParseScriptInt("99999999999999999999", integer));
    EXPECT_EQ(integer, 99);

    float decimal = 2.0f;
    EXPECT_FALSE(ParseScriptFloat("oops", decimal));
    EXPECT_FALSE(ParseScriptFloat("nan", decimal));
    EXPECT_FALSE(ParseScriptFloat("inf", decimal));
    EXPECT_FLOAT_EQ(decimal, 2.0f);
}

TEST(ScriptValueParse, PreservesLegacyDecimalToIntegerConversion)
{
    int integer = 0;
    EXPECT_TRUE(ParseScriptLegacyInt("5.5\r\n", integer));
    EXPECT_EQ(integer, 5);
    EXPECT_TRUE(ParseScriptLegacyInt("13 // comment", integer));
    EXPECT_EQ(integer, 13);
    EXPECT_TRUE(ParseScriptLegacyInt("1l\r\n", integer));
    EXPECT_EQ(integer, 1);
    EXPECT_TRUE(ParseScriptLegacyInt("7L // legacy suffix", integer));
    EXPECT_EQ(integer, 7);
    EXPECT_TRUE(ParseScriptLegacyInt("5.5oops", integer));
    EXPECT_EQ(integer, 5);
    EXPECT_TRUE(ParseScriptLegacyInt("1ll", integer));
    EXPECT_EQ(integer, 1);
    EXPECT_FALSE(ParseScriptLegacyInt("oops", integer));
}

TEST(ScriptBlockParse, ConsumesNestedBodyAndLeavesFollowingField)
{
    TempScript script;
    ASSERT_NE(script.stream, nullptr);
    fputs(" xmin = 10\n nested { value = '}' }\n }\n spawnrate = 0.5\n",
          script.stream);
    rewind(script.stream);

    ASSERT_TRUE(ConsumeScriptBlockBody(script.stream));

    char line[64] = {};
    ASSERT_NE(fgets(line, sizeof(line), script.stream), nullptr);
    EXPECT_STREQ(line, " spawnrate = 0.5\n");
}

TEST(ScriptBlockParse, IgnoresBracesInCommentsAndQuotedValues)
{
    TempScript script;
    ASSERT_NE(script.stream, nullptr);
    fputs(" value = '}' // { comment }\n }\n next = 1\n", script.stream);
    rewind(script.stream);

    ASSERT_TRUE(ConsumeScriptBlockBody(script.stream));

    char line[32] = {};
    ASSERT_NE(fgets(line, sizeof(line), script.stream), nullptr);
    EXPECT_STREQ(line, " next = 1\n");
}

TEST(ScriptBlockParse, RejectsUnterminatedBody)
{
    TempScript script;
    ASSERT_NE(script.stream, nullptr);
    fputs(" value = 1\n", script.stream);
    rewind(script.stream);

    EXPECT_FALSE(ConsumeScriptBlockBody(script.stream));
}
