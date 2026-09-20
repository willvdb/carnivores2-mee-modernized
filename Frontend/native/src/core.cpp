#include "c2/frontend/core.hpp"

#include "picosha2/picosha2.h"
#include <algorithm>
#include <array>
#include <limits>

namespace c2::frontend {
std::string_view version() noexcept { return "0.1.0-foundation"; }

std::string sha256(std::string_view bytes) {
    static_assert(std::numeric_limits<unsigned char>::digits == 8);
    static_assert(std::numeric_limits<picosha2::word_t>::digits >= 32);
    picosha2::hash256_one_by_one hash;
    // Bound both the vendor's temporary buffer and word_t length conversion
    // (unsigned long is 32-bit on Windows and 64-bit on Linux).
    while (!bytes.empty()) {
        const auto count = std::min<std::size_t>(bytes.size(), 65536);
        hash.process(bytes.begin(), bytes.begin() + count);
        bytes.remove_prefix(count);
    }
    hash.finish();
    std::array<unsigned char, 32> digest{};
    hash.get_hash_bytes(digest.begin(), digest.end());
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (auto byte : digest) {
        result.push_back(hex[byte >> 4]);
        result.push_back(hex[byte & 15]);
    }
    return result;
}
} // namespace c2::frontend
