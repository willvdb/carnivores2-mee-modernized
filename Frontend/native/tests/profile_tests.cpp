#include "c2/frontend/profile.hpp"
#include <algorithm>
#include <iostream>
#include <iterator>
#include <limits>
#include <type_traits>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
using namespace c2::frontend;
static void require(bool condition) { if (!condition) throw std::runtime_error("profile API assertion failed"); }
static void self_test() {
    static_assert(std::is_same_v<decltype(ProfileStats::path_bits), std::uint32_t>);
    static_assert(std::is_same_v<decltype(ProfileItem::scale_bits), std::uint32_t>);
    static_assert(std::is_const_v<std::remove_reference_t<decltype(*std::declval<ProfileInspection>().save())>>);
    auto owned = [] {
        std::string bytes(1660, '\xff');
        bytes[0] = 'A'; bytes[1] = '\x80'; bytes[2] = 0;
        auto v = inspect_profile_bytes(bytes, "sav", ProfileCodec::native);
        auto copy = v;
        std::fill(bytes.begin(), bytes.end(), 0);
        require(copy.export_json() == v.export_json());
        return copy;
    }();
    require(owned.layout() == ProfileLayout::c2_1660_candidate && owned.codec_roundtrip_exact());
    require(!owned.room() && owned.save()->registration == -1);
    require(owned.save()->name_display_latin1 == U"A\x80");
    require(owned.save()->name_bytes[127] == 255);
    require(owned.save()->last_raw.path_bits == UINT32_MAX);
    require(owned.save()->items_raw[23].reserved[3] == -1);
    const std::string room(7176, '\xff'), save(1660, 0);
    auto r = inspect_profile_bytes(room, "sab", ProfileCodec::native);
    require(r.layout() == ProfileLayout::mee_7176_candidate && !r.save());
    require(r.room()->version_raw == -1 && r.room()->high_score_raw == -1);
    require(r.room()->items_raw[127].range_bits == UINT32_MAX);
    for (auto codec : {ProfileCodec::unavailable, ProfileCodec::native}) {
        const auto unknown = inspect_profile_bytes("", "sav", codec);
        require(unknown.layout() == ProfileLayout::unknown && !unknown.save() && !unknown.room());
        const auto unsupported = inspect_profile_bytes("", "sav", codec, "iceage-triassic");
        require(unsupported.layout() == ProfileLayout::unsupported_iceage_family && !unsupported.codec_roundtrip_exact());
        require(inspect_profile_bytes(save, std::string("sav\0", 4), codec).layout() == ProfileLayout::unknown);
        require(inspect_profile_bytes(room, std::string("sav\0", 4), codec).layout() ==
            (codec == ProfileCodec::native ? ProfileLayout::mee_7176_candidate : ProfileLayout::size_candidate_only));
        require(inspect_profile_bytes("", "sav", codec, std::string("iceage-triassic\0", 16)).layout() == ProfileLayout::unknown);
    }
    const auto unavailable = inspect_profile_bytes(save, "sav");
    require(unavailable.layout() == ProfileLayout::size_candidate_only && !unavailable.codec_roundtrip_exact());
    require(!unavailable.save() && !unavailable.room());
}
int main(int argc, char** argv) {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY); _setmode(_fileno(stdout), _O_BINARY);
#endif
    try {
        if (argc == 1) { self_test(); return 0; }
        if (argc != 4) return 2;
        std::string bytes(std::istreambuf_iterator<char>(std::cin), {});
        auto result = inspect_profile_bytes(bytes, argv[1], std::string(argv[2]) == "native" ?
            ProfileCodec::native : ProfileCodec::unavailable, argv[3]);
        // Mutation before export catches accidentally borrowed decoded/name data.
        std::fill(bytes.begin(), bytes.end(), 0); bytes.clear(); bytes.shrink_to_fit();
        std::cout << result.export_json();
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
