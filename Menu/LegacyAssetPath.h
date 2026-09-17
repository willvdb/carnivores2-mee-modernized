#pragma once
#include "../Shared/LegacyPath.h"
#include <iostream>

// Optional menu reads retain their existing stream/fopen failure and fallback
// paths. Empty native paths cannot open a file; never retry the unresolved name.
inline std::filesystem::path ResolveMenuAssetReadPath(const std::string& logical)
{
    auto result = LegacyPath::Resolve(logical);
    if (!result) std::cout << result.Message() << std::endl;
    return result.path;
}
