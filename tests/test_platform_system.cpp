// These public boundaries must compile without OS or SDL header contamination.
#include "../Hunt/Platform/Platform.h"
#include "../Hunt/Platform/Memory.h"
#include "../Hunt/Platform/Files.h"
#include "../Hunt/Platform/System.h"
#include "../Hunt/Platform/SharedLibrary.h"
#include "../Hunt/Platform/Screenshot.h"
#include "../Hunt/Renderer/CPUText.h"
#include "../Hunt/Renderer/UIText.h"
#include "../Hunt/Core/LegacyKeys.h"
#include "../Hunt/Core/Strings.h"
#if defined(_WINDOWS_) || defined(SDL_MAJOR_VERSION)
#error Portable boundaries must not include Windows or SDL headers.
#endif
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include "temp_path.h"

TEST(PlatformSystem, ArgumentsAreCopiedAndKeepLegacyBackslashes) {
    char exe[] = "game", arg[] = "prj=HUNTDAT\\AREAS\\AREA1";
    char* args[]{exe,arg};
    Platform::SetArguments(2,args);
    arg[0] = 'X';
    ASSERT_EQ(Platform::Arguments().size(),2u);
    EXPECT_EQ(Platform::Arguments()[1],"prj=HUNTDAT\\AREAS\\AREA1");
    Platform::SetArguments(0,nullptr);
    EXPECT_TRUE(Platform::Arguments().empty());
}
TEST(PlatformSystem, ModuleDirectoryIsAbsoluteAndCalendarUsesLegacyFields) {
    EXPECT_TRUE(std::filesystem::path(Platform::ModuleDirectory()).is_absolute());
    const auto date = Platform::LocalTime();
    EXPECT_GE(date.year,2020); EXPECT_GE(date.month,1); EXPECT_LE(date.month,12);
    EXPECT_GE(date.day,1); EXPECT_LE(date.day,31);
    EXPECT_GE(date.hour,0); EXPECT_LE(date.hour,23); EXPECT_GE(date.minute,0); EXPECT_LE(date.minute,59);
}
TEST(PlatformSystem, ConfigTokenizationSkipsCrLfAndKeepsIndependentState) {
    char config[]="\r\nresolution 800x600\r\n\nvolume 200\n";
    char other[]="a:b", *state=nullptr, *otherState=nullptr;
    EXPECT_STREQ(LegacyText::Token(config,"\r\n",&state),"resolution 800x600");
    EXPECT_STREQ(LegacyText::Token(other,":",&otherState),"a");
    EXPECT_STREQ(LegacyText::Token(nullptr,"\r\n",&state),"volume 200");
    EXPECT_EQ(LegacyText::Token(nullptr,"\r\n",&state),nullptr);
    EXPECT_STREQ(LegacyText::Token(nullptr,":",&otherState),"b");
}
TEST(PlatformScreenshot, EncodesBottomUpPaddedRgbWithoutHostStructLayout) {
    const auto path=TestTempPath("screenshot");
    const std::uint16_t pixels[]{0x7c00,0x03e0,0x001f,0xffff, 0x7fff,0,0x4210,0xffff};
    ASSERT_TRUE(Platform::SaveBitmap555(path.c_str(),pixels,3,2,4));
    std::ifstream input(path,std::ios::binary);
    const std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(input),{}};
    input.close(); std::filesystem::remove(path);
    ASSERT_EQ(bytes.size(),78u);
    EXPECT_EQ(bytes[0],'B');EXPECT_EQ(bytes[1],'M');EXPECT_EQ(bytes[2],78);
    EXPECT_EQ(bytes[10],54);EXPECT_EQ(bytes[14],40);EXPECT_EQ(bytes[18],3);EXPECT_EQ(bytes[22],2);
    EXPECT_EQ(bytes[26],1);EXPECT_EQ(bytes[28],24);EXPECT_EQ(bytes[34],24);
    const std::vector<unsigned char> expected{248,248,248,0,0,0,128,128,128,0,0,0, 0,0,248,0,248,0,248,0,0,0,0,0};
    EXPECT_EQ(std::vector<unsigned char>(bytes.begin()+54,bytes.end()),expected);
    EXPECT_FALSE(Platform::SaveBitmap555(path.c_str(),pixels,3,2,2));
    EXPECT_FALSE(Platform::SaveBitmap555(path.c_str(),nullptr,3,2,4));
    EXPECT_FALSE(std::filesystem::exists(path));
}
