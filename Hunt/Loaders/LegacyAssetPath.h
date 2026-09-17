#pragma once
#include "Hunt.h"
#include "../../Shared/LegacyPath.h"

// Required engine assets retain DoHalt failure policy. This adapter only
// resolves the name; the loader still chooses its I/O API and sharing flags.
inline std::string ResolveLegacyAssetReadPath(const char* logical)
{
    auto result = LegacyPath::Resolve(logical);
    if (!result) {
        auto message = result.Message();
        DoHalt(message.data());
    }
    return result.path.string();
}
