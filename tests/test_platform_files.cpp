#include <gtest/gtest.h>
#include "../Hunt/Platform/Files.h"
#include <chrono>
#include <filesystem>
#include <fstream>

namespace {
namespace fs = std::filesystem;
struct Files : testing::Test {
    fs::path root = fs::temp_directory_path() / ("carnivores-path-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    void SetUp() override { fs::create_directories(root / "HuntDat" / "Areas"); }
    void TearDown() override { std::error_code ec; fs::remove_all(root, ec); }
    std::string name(const char* tail) { return (root / tail).string(); }
};
TEST_F(Files, NormalizeMixedSeparators) {
    EXPECT_EQ(Platform::NormalizePath("HUNTDAT\\Areas/AREA1.MAP"), "HUNTDAT/Areas/AREA1.MAP");
}
TEST_F(Files, ResolveEveryComponentAndPreserveSpelling) {
    std::ofstream(name("HuntDat/Areas/ArEa1.MAP")) << "legacy";
    std::string resolved;
    ASSERT_TRUE(Platform::ResolveLegacyPath(name("huntdat\\AREAS/area1.map"), resolved));
    EXPECT_TRUE(fs::equivalent(resolved, root / "HuntDat/Areas/ArEa1.MAP"));
    EXPECT_FALSE(Platform::ResolveLegacyPath(name("huntdat/missing/file"), resolved, true));
}
#ifndef _WIN32
TEST_F(Files, ExactMatchWinsButAmbiguousFoldFails) {
    std::ofstream(name("HuntDat/Areas/one"));
    std::ofstream(name("HuntDat/Areas/ONE"));
    std::string resolved;
    EXPECT_TRUE(Platform::ResolveLegacyPath(name("HuntDat/Areas/one"), resolved));
    EXPECT_FALSE(Platform::ResolveLegacyPath(name("HuntDat/Areas/One"), resolved));
}
#endif
TEST_F(Files, WriteReusesExistingLegacyNameAndCreateNewNeverTruncates) {
    std::ofstream(name("TrOpHy00.SAV")) << "old";
    auto file = Platform::OpenFile(name("trophy00.sav").c_str(), Platform::FileMode::Write);
    ASSERT_NE(file, Platform::InvalidFile);
    std::uint32_t count = 0;
    EXPECT_TRUE(Platform::WriteFile(file, "new bytes", 9, &count));
    EXPECT_EQ(count, 9u);
    EXPECT_TRUE(Platform::CloseFile(file));
    EXPECT_EQ(fs::file_size(root / "TrOpHy00.SAV"), 9u);
    EXPECT_EQ(Platform::OpenFile(name("trophy00.sav").c_str(), Platform::FileMode::CreateNew), Platform::InvalidFile);
}
TEST_F(Files, ReadsReportShortTransfersAndSizeKeepsPosition) {
    std::ofstream(name("bytes"), std::ios::binary) << "abc";
    auto file = Platform::OpenFile(name("bytes").c_str(), Platform::FileMode::Read);
    ASSERT_NE(file, Platform::InvalidFile);
    char bytes[8]{};
    std::uint32_t count = 0;
    EXPECT_EQ(Platform::SeekFile(file, 1, Platform::SeekOrigin::Begin), 1);
    EXPECT_EQ(Platform::FileSize(file), 3);
    EXPECT_TRUE(Platform::ReadFile(file, bytes, 8, &count));
    EXPECT_EQ(count, 2u);
    EXPECT_EQ(std::string(bytes, count), "bc");
    EXPECT_TRUE(Platform::CloseFile(file));
    EXPECT_FALSE(Platform::ReadFile(Platform::InvalidFile, bytes, 1, &count));
    EXPECT_EQ(count, 0u);
}
TEST_F(Files, MissingLeafMayBeCreatedWithoutChangingDirectories) {
    auto file = Platform::OpenFile(name("huntdat/AREAS/new.bin").c_str(), Platform::FileMode::CreateNew);
    ASSERT_NE(file, Platform::InvalidFile);
    EXPECT_TRUE(Platform::CloseFile(file));
    EXPECT_TRUE(fs::exists(root / "HuntDat/Areas/new.bin"));
}
}
