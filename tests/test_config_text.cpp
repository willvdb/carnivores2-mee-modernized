// test_config_text.cpp
// Unit tests for Hunt/Core/ConfigText.h -- the config-file reader shared by
// the engine (EngineInit.cpp) and the standalone menu (Menu/Resources.cpp).
//
// Every branch here is a stated invariant that had no test behind it: the
// reader must not stop at the first 4096-byte chunk, must turn embedded NUL
// padding into newlines so the engine's C-string parser keeps going, and
// must not mistake UTF-16 for a padded UTF-8 file. The header is pure
// (std::istream only), so no engine linkage is needed.

#include <gtest/gtest.h>

#include <sstream>
#include <string>

#include "Core/ConfigText.h"

namespace {

std::string Read(const std::string& raw, bool* ok = nullptr, size_t* nuls = nullptr) {
    std::istringstream input(raw);
    std::string text;
    size_t nulBytes = 0;
    const bool result = ReadConfigText(input, text, nulBytes);
    if (ok) { *ok = result; }
    if (nuls) { *nuls = nulBytes; }
    return text;
}

TEST(ConfigText, PassesCleanTextThrough) {
    bool ok = false;
    size_t nuls = 99;
    EXPECT_EQ(Read("a=1\nb=2\n", &ok, &nuls), std::string("a=1\nb=2\n"));
    EXPECT_TRUE(ok);
    EXPECT_EQ(nuls, 0u);
}

TEST(ConfigText, ReadsPastTheFirstChunk) {
    // A heavily commented config can push the live settings past one chunk.
    const std::string filler(9000, ';');
    const std::string raw = filler + "key=value\n";

    bool ok = false;
    const std::string text = Read(raw, &ok);

    EXPECT_TRUE(ok);
    EXPECT_EQ(text, raw);
}

TEST(ConfigText, ConvertsNulPaddingAndCountsIt) {
    std::string raw = "key=value";
    raw += '\0';
    raw += '\0';
    raw += "next=1";

    bool ok = false;
    size_t nuls = 0;
    const std::string text = Read(raw, &ok, &nuls);

    EXPECT_TRUE(ok);
    EXPECT_EQ(nuls, 2u);
    EXPECT_EQ(text.find('\0'), std::string::npos);
    EXPECT_EQ(text, std::string("key=value\n\nnext=1"));
}

TEST(ConfigText, RejectsUtf16BomsRatherThanTreatingThemAsPadding) {
    // "key=1" encoded as UTF-16 is mostly zero bytes; without the BOM check
    // those would be rewritten as newlines and the file would look valid.
    const std::string littleEndian = std::string("\xFF\xFE", 2) + "k\0e\0y\0";
    const std::string bigEndian = std::string("\xFE\xFF", 2) + "\0k\0e\0y";

    bool ok = true;
    const std::string le = Read(littleEndian, &ok);
    EXPECT_FALSE(ok);
    EXPECT_TRUE(le.empty());

    ok = true;
    const std::string be = Read(bigEndian, &ok);
    EXPECT_FALSE(ok);
    EXPECT_TRUE(be.empty());
}

TEST(ConfigText, StripsUtf8Bom) {
    const std::string raw = std::string("\xEF\xBB\xBF", 3) + "key=1";

    bool ok = false;
    EXPECT_EQ(Read(raw, &ok), std::string("key=1"));
    EXPECT_TRUE(ok);
}

TEST(ConfigText, RejectsFilesOverTheCap) {
    const std::string raw(1024 * 1024 + 1, 'a');

    bool ok = true;
    const std::string text = Read(raw, &ok);

    EXPECT_FALSE(ok);
    EXPECT_TRUE(text.empty());
}

}  // namespace
