// A non-mutating JSON inspection adapter over the existing portable codec.
#include "../Shared/LegacyProfile.h"
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

static void item(const LegacyProfile::Item& v) {
    std::cout << "{\"type\":" << v.type << ",\"weapon\":" << v.weapon
              << ",\"phase\":" << v.phase << ",\"height\":" << v.height
              << ",\"weight\":" << v.weight << ",\"score\":" << v.score
              << ",\"date_raw\":" << v.date << ",\"time_raw\":" << v.time
              << ",\"scale_bits\":" << v.scale << ",\"range_bits\":" << v.range
              << ",\"reserved\":[";
    for (std::size_t i = 0; i < v.reserved.size(); ++i) {
        if (i) std::cout << ',';
        std::cout << v.reserved[i];
    }
    std::cout << "]}";
}

template<class T> static void items(const T& values) {
    std::cout << '[';
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i) std::cout << ',';
        item(values[i]);
    }
    std::cout << ']';
}

static void stats(const LegacyProfile::Stats& v) {
    // "hits" is the codec field name, not a claim that it measures accuracy.
    std::cout << "{\"shots_raw\":" << v.shots << ",\"success_raw\":" << v.hits
              << ",\"path_bits\":" << v.path << ",\"time_bits\":" << v.time << '}';
}

int main(int argc, char** argv) {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
#endif
    if (argc != 2) return 2;
    const std::string kind = argv[1];
    // Read at most one byte beyond the largest known layout.
    std::vector<std::uint8_t> bytes;
    char ch;
    while (bytes.size() <= LegacyProfile::RoomSize && std::cin.get(ch))
        bytes.push_back(static_cast<std::uint8_t>(static_cast<unsigned char>(ch)));
    if (kind == "save" && bytes.size() == LegacyProfile::SaveSize) {
        LegacyProfile::Save value;
        if (!LegacyProfile::DecodeSave(bytes.data(), bytes.size(), value)) return 3;
        const auto encoded = LegacyProfile::EncodeSave(value);
        if (!std::equal(encoded.begin(), encoded.end(), bytes.begin())) return 4;
        const auto& h = value.profile.header;
        std::cout << "{\"layout\":\"c2-1660-candidate\",\"codec_roundtrip_exact\":true,\"name_hex\":\"";
        constexpr char hex[] = "0123456789abcdef";
        for (const auto c : h.name) std::cout << hex[c >> 4] << hex[c & 15];
        std::cout << "\",\"registration\":" << h.registration << ",\"score\":" << h.score
                  << ",\"rank\":" << h.rank << ",\"last_raw\":";
        stats(value.profile.last);
        std::cout << ",\"total_raw\":"; stats(value.profile.total);
        std::cout << ",\"items_raw\":"; items(value.profile.items);
        std::cout << "}\n";
    } else if (kind == "room" && bytes.size() == LegacyProfile::RoomSize) {
        LegacyProfile::Room value;
        if (!LegacyProfile::DecodeRoom(bytes.data(), bytes.size(), value)) return 3;
        const auto encoded = LegacyProfile::EncodeRoom(value);
        if (!std::equal(encoded.begin(), encoded.end(), bytes.begin())) return 4;
        std::cout << "{\"layout\":\"mee-7176-candidate\",\"codec_roundtrip_exact\":true,\"version_raw\":"
                  << value.version << ",\"high_score_raw\":" << value.highScore << ",\"items_raw\":";
        items(value.items);
        std::cout << "}\n";
    } else {
        std::cout << "{\"layout\":\"unknown\",\"codec_roundtrip_exact\":false}\n";
    }
}
