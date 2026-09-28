// test_command_line_parse.cpp -- bounded, anchored command-line contracts

#include <gtest/gtest.h>

#include "Core/CommandLineParse.h"

namespace {

TEST(CommandLineParse, CopiesValuesAtExactCapacity)
{
    char destination[4] = {};
    EXPECT_EQ(CopyCommandLineOption(destination, sizeof(destination),
                                    "prj=abc", "prj="),
              CommandLineCopyResult::Copied);
    EXPECT_STREQ(destination, "abc");
}

TEST(CommandLineParse, RejectsEmptyAndOverlongValuesWithoutChangingDestination)
{
    char destination[4] = "old";
    EXPECT_EQ(CopyCommandLineOption(destination, sizeof(destination),
                                    "prj=", "prj="),
              CommandLineCopyResult::Invalid);
    EXPECT_STREQ(destination, "old");

    EXPECT_EQ(CopyCommandLineOption(destination, sizeof(destination),
                                    "prj=abcd", "prj="),
              CommandLineCopyResult::Invalid);
    EXPECT_STREQ(destination, "old");
}

TEST(CommandLineParse, OptionMatchingIsCaseInsensitiveButAnchored)
{
    char project[32] = {};
    EXPECT_EQ(CopyCommandLineOption(project, sizeof(project),
                                    "PrJ=huntdat/areas/area1", "prj="),
              CommandLineCopyResult::Copied);
    EXPECT_STREQ(project, "huntdat/areas/area1");

    EXPECT_EQ(CopyCommandLineOption(project, sizeof(project),
                                    "xprj=not-an-option", "prj="),
              CommandLineCopyResult::NotPresent);
    EXPECT_STREQ(project, "huntdat/areas/area1");

    const char* value = nullptr;
    EXPECT_TRUE(CommandLineOptionValue("SERVER=localhost", "server=", &value));
    ASSERT_NE(value, nullptr);
    EXPECT_STREQ(value, "localhost");
}

TEST(CommandLineParse, StrictIntegersAndFloatsRejectMalformedValues)
{
    int integer = 77;
    EXPECT_TRUE(ParseCommandLineInt("1023", integer));
    EXPECT_EQ(integer, 1023);
    EXPECT_FALSE(ParseCommandLineInt("12abc", integer));
    EXPECT_EQ(integer, 1023);
    EXPECT_FALSE(ParseCommandLineInt("99999999999999999999", integer));

    float decimal = 1.0f;
    EXPECT_TRUE(ParseCommandLineFloat("-12.5", decimal));
    EXPECT_FLOAT_EQ(decimal, -12.5f);
    EXPECT_FALSE(ParseCommandLineFloat("1.5oops", decimal));
    EXPECT_FLOAT_EQ(decimal, -12.5f);
    EXPECT_FALSE(ParseCommandLineFloat("nan", decimal));
}

TEST(CommandLineParse, ResolutionRequiresPositiveWholeWxH)
{
    int width = 0;
    int height = 0;
    EXPECT_TRUE(ParseCommandLineResolution("1920x1080", width, height));
    EXPECT_EQ(width, 1920);
    EXPECT_EQ(height, 1080);

    EXPECT_TRUE(ParseCommandLineResolution("800X600", width, height));
    EXPECT_EQ(width, 800);
    EXPECT_EQ(height, 600);
    EXPECT_FALSE(ParseCommandLineResolution("800x", width, height));
    EXPECT_FALSE(ParseCommandLineResolution("800x600junk", width, height));
    EXPECT_FALSE(ParseCommandLineResolution("0x600", width, height));
    EXPECT_FALSE(ParseCommandLineResolution("800x-1", width, height));
}

}  // namespace
