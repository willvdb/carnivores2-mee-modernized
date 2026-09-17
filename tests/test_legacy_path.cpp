#include <gtest/gtest.h>
#include "../Shared/LegacyPath.h"
#include <chrono>
#include <fstream>
#include <random>

namespace fs = std::filesystem;
namespace {
class LegacyPathTest : public testing::Test {
protected:
    fs::path root;
    void SetUp() override {
        const auto id = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
            + "-" + std::to_string(std::random_device{}());
        root = fs::temp_directory_path() / ("carnivores-path-" + id);
        ASSERT_TRUE(fs::create_directory(root));
    }
    void TearDown() override {
        std::error_code ec;
        fs::remove_all(root, ec);
        EXPECT_FALSE(ec) << ec.message();
    }
    fs::path File(const fs::path& relative, const char* contents = "fixture") {
        const auto path = root / relative;
        fs::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary);
        out << contents;
        if (!out) throw std::runtime_error("Cannot write fixture");
        return path;
    }
    void Resolves(const std::string& request, const fs::path& expected) {
        const auto result = LegacyPath::Resolve(request, root);
        ASSERT_TRUE(result) << result.Message();
        EXPECT_EQ(result.path, expected);
        EXPECT_EQ(result.requested, request);
        EXPECT_TRUE(result.Message().empty());
    }
    bool CaseSensitive() {
        File("CaseProbe");
        return !fs::exists(root / "caseprobe");
    }
};

TEST_F(LegacyPathTest, SeparatorsAndCasePreserveDiskSpelling) {
    const auto file = File("HUNTDAT/AREAS/AREA1.RSC");
    for (const auto* request : {"HUNTDAT/AREAS/AREA1.RSC", "huntdat/areas/area1.rsc",
         "HuNtDaT/aReAs/ArEa1.RsC", "huntdat\\areas\\area1.rsc",
         "HuNtDaT\\areas/AREA1.rsc", "huntdat//areas\\\\area1.rsc"})
        Resolves(request, file);
}

TEST_F(LegacyPathTest, EveryDirectoryComponentCanDiffer) {
    const auto file = File("HUNTDAT/Creatures/Tyrannosaurus/TREX.CAR");
    Resolves("huntdat/cREATURES/tYRANNOSAURUS/trex.car", file);
}

TEST_F(LegacyPathTest, ExactMatchWinsAmongCaseCollisions) {
    if (!CaseSensitive()) GTEST_SKIP() << "Host does not support case-colliding siblings";
    const auto a = File("Creatures/Trex.CAR", "first");
    const auto b = File("Creatures/TREX.CAR", "second");
    Resolves("creatures/Trex.CAR", a);
    Resolves("creatures/TREX.CAR", b);
    for (int i = 0; i < 10; ++i) {
        const auto result = LegacyPath::Resolve("creatures/trex.car", root);
        EXPECT_FALSE(result);
        EXPECT_TRUE(result.path.empty());
        EXPECT_EQ(result.error, LegacyPath::Error::Ambiguous);
        EXPECT_EQ(result.component, "trex.car");
        EXPECT_EQ(result.directory, root / "Creatures");
        EXPECT_NE(result.Message().find("ambiguous"), std::string::npos);
    }
}

TEST_F(LegacyPathTest, DirectoryCollisionDoesNotSearchAhead) {
    if (!CaseSensitive()) GTEST_SKIP() << "Host does not support case-colliding siblings";
    const auto a = File("Dir/only-a");
    const auto b = File("DIR/only-b");
    Resolves("Dir/only-a", a);
    Resolves("DIR/only-b", b);
    EXPECT_EQ(LegacyPath::Resolve("dir/only-a", root).error, LegacyPath::Error::Ambiguous);
    // Exact Dir wins even though the desired child exists only in DIR.
    EXPECT_EQ(LegacyPath::Resolve("Dir/only-b", root).error, LegacyPath::Error::Missing);
}

TEST_F(LegacyPathTest, MissingComponentHasUsefulContext) {
    File("HUNTDAT/AREAS/AREA1.RSC");
    const std::string request = "huntdat/missing/area1.rsc";
    const auto result = LegacyPath::Resolve(request, root);
    EXPECT_EQ(result.error, LegacyPath::Error::Missing);
    EXPECT_TRUE(result.path.empty());
    EXPECT_EQ(result.component, "missing");
    EXPECT_EQ(result.directory, root / "HUNTDAT");
    EXPECT_NE(result.Message().find(request), std::string::npos);
    EXPECT_NE(result.Message().find("HUNTDAT"), std::string::npos);
}

TEST_F(LegacyPathTest, DotAndParentRemainFilesystemTraversal) {
    File("HUNTDAT/AREAS/AREA1.RSC");
    Resolves("./huntdat/areas/../AREAS/./area1.rsc",
             root / "./HUNTDAT/AREAS/../AREAS/./AREA1.RSC");
    // Root is a working/content directory, not a sandbox boundary.
    const auto result = LegacyPath::Resolve("../areas/area1.rsc", root / "HUNTDAT/AREAS");
    ASSERT_TRUE(result) << result.Message();
    EXPECT_TRUE(fs::equivalent(result.path, root / "HUNTDAT/AREAS/AREA1.RSC"));
    EXPECT_FALSE(LegacyPath::Resolve("missing/../HUNTDAT", root));
    EXPECT_FALSE(LegacyPath::Resolve("HUNTDAT/AREAS/AREA1.RSC/..", root));
}

TEST_F(LegacyPathTest, SpacesPunctuationAndExtensionsAreLiteral) {
    const auto file = File("Mod Dir/T_rex-2.v1 (old).CAR");
    Resolves("mod dir/t_REX-2.V1 (OLD).car", file);
    for (const auto* bad : {"Mod Dir/T_rex-2.v1 (old)", "Mod Dir/T rex-2.v1 (old).CAR",
         "Mod Dir/T_rex-2.v1 (old).CAR ", "Mod Dir/T_rex", "T_rex-2.v1 (old).CAR"})
        EXPECT_FALSE(LegacyPath::Resolve(bad, root)) << bad;
}

TEST_F(LegacyPathTest, SymlinksAndParentUseNativeTraversal) {
    File("Elsewhere/Child/inside.car");
    File("Elsewhere/Outside.CAR");
    File("HUNTDAT/Outside.CAR");
    fs::create_directory(root / "HUNTDAT");
    std::error_code ec;
    fs::create_directory_symlink(root / "Elsewhere/Child", root / "HUNTDAT/Link", ec);
    if (ec) GTEST_SKIP() << "Host cannot create symlinks: " << ec.message();
    // Windows and POSIX can interpret link/.. differently. Preserve what
    // direct native traversal does on this host, rather than canonicalizing.
    const auto target = root / "HUNTDAT/Link/../Outside.CAR";
    const auto result = LegacyPath::Resolve("huntdat/link/../outside.car", root);
    ASSERT_TRUE(result) << result.Message();
    EXPECT_EQ(result.path, root / "HUNTDAT/Link/../Outside.CAR");
    EXPECT_TRUE(fs::equivalent(result.path, target));
    fs::create_symlink(target, root / "Linked.CAR");
    Resolves("linked.car", root / "Linked.CAR");
    fs::create_symlink(root / "absent", root / "Dangling");
    EXPECT_FALSE(LegacyPath::Resolve("dangling", root));
}

TEST_F(LegacyPathTest, NativeAbsolutePathAndDirectoryRequests) {
    const auto file = File("HUNTDAT/AREAS/AREA1.MAP");
    Resolves((root / "huntdat/areas/area1.map").string(), file);
    Resolves("huntdat/areas", root / "HUNTDAT/AREAS");
    EXPECT_TRUE(LegacyPath::Resolve("huntdat/areas/", root));
    EXPECT_FALSE(LegacyPath::Resolve("huntdat/areas/area1.map/", root));
    EXPECT_TRUE(LegacyPath::Resolve(".", root));
}

TEST_F(LegacyPathTest, DefaultRootIsWorkingDirectory) {
    const auto file = File("DefaultRoot.CAR");
    struct RestoreCwd {
        fs::path previous = fs::current_path();
        ~RestoreCwd() { fs::current_path(previous); }
    } restore;
    fs::current_path(root);
    const auto result = LegacyPath::Resolve("defaultroot.car");
    ASSERT_TRUE(result) << result.Message();
    EXPECT_TRUE(fs::equivalent(result.path, file));
}

TEST_F(LegacyPathTest, EmptyAndEmbeddedNulAreRejected) {
    EXPECT_EQ(LegacyPath::Resolve("", root).error, LegacyPath::Error::InvalidPath);
    File("Existing");
    EXPECT_EQ(LegacyPath::Resolve(std::string("Existing\0extra", 14), root).error,
              LegacyPath::Error::InvalidPath);
}

TEST_F(LegacyPathTest, LookupDoesNotCacheMissesOrCaseMatches) {
    EXPECT_FALSE(LegacyPath::Resolve("new.car", root));
    const auto file = File("New.CAR");
    Resolves("new.car", file);
    fs::remove(file);
    EXPECT_FALSE(LegacyPath::Resolve("new.car", root));
}

#ifndef _WIN32
TEST_F(LegacyPathTest, WindowsRootsAreNotPosixMountGuesses) {
    for (const auto* request : {"C:\\HUNTDAT\\AREA1.RSC", "C:relative.car", "\\\\server\\share\\asset"})
        EXPECT_EQ(LegacyPath::Resolve(request, root).error, LegacyPath::Error::UnsupportedRoot);
}

TEST_F(LegacyPathTest, NonAsciiBytesAreNotLocaleFolded) {
    const std::string upper = "\xc3\x84.CAR";
    const std::string lower = "\xc3\xa4.car";
    const auto file = File(upper);
    Resolves("\xc3\x84.car", file);
    EXPECT_FALSE(LegacyPath::Resolve(lower, root));
}
#endif
}
