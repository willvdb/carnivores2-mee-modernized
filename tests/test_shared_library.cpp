#include <gtest/gtest.h>
#include "../Hunt/Platform/SharedLibrary.h"
#include "../Hunt/Audio/OpenAL_Loader.h"

TEST(SharedLibrary, MissingLibraryAndNullHandlesAreSafe) {
    EXPECT_EQ(Platform::OpenSharedLibrary("carnivores-library-that-does-not-exist"), nullptr);
    EXPECT_EQ(Platform::SharedLibrarySymbol(nullptr, "symbol"), nullptr);
    Platform::CloseSharedLibrary(nullptr);
}
TEST(SharedLibrary, ResolvesCallableSymbolAndCanReload) {
    for (int i = 0; i < 2; ++i) {
        auto library = Platform::OpenSharedLibrary(TEST_LIBRARY_PATH);
        ASSERT_NE(library, nullptr);
        auto value = reinterpret_cast<int (*)()>(Platform::SharedLibrarySymbol(library, "TestLibraryValue"));
        ASSERT_NE(value, nullptr);
        EXPECT_EQ(value(), 42);
        EXPECT_EQ(Platform::SharedLibrarySymbol(library, "missing_symbol"), nullptr);
        Platform::CloseSharedLibrary(library);
    }
}
TEST(OpenALLoader, IncompleteLibraryClearsResolvedSymbols) {
    EXPECT_FALSE(LoadOpenAL(TEST_LIBRARY_PATH));
    EXPECT_EQ(alcOpenDevice, nullptr);
    EXPECT_EQ(alGenBuffers, nullptr);
    EXPECT_FALSE(LoadOpenAL("carnivores-library-that-does-not-exist"));
    UnloadOpenAL();
    EXPECT_EQ(alcOpenDevice, nullptr);
}
