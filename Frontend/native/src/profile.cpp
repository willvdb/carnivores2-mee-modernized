#include "c2/frontend/profile.hpp"
#include "json_compat.hpp"
#include "../../../Shared/LegacyProfile.h"
#include <algorithm>

namespace c2::frontend {
struct ProfileInspection::Impl {
    ProfileLayout layout = ProfileLayout::unknown;
    std::optional<SaveInspection> save;
    std::optional<RoomInspection> room;
};
ProfileInspection::ProfileInspection(std::shared_ptr<const Impl> p) : impl_(std::move(p)) {}
ProfileLayout ProfileInspection::layout() const noexcept { return impl_->layout; }
bool ProfileInspection::codec_roundtrip_exact() const noexcept { return impl_->save.has_value() || impl_->room.has_value(); }
const std::optional<SaveInspection>& ProfileInspection::save() const noexcept { return impl_->save; }
const std::optional<RoomInspection>& ProfileInspection::room() const noexcept { return impl_->room; }
namespace {
ProfileStats stats(const LegacyProfile::Stats& v) { return {v.shots, v.hits, v.path, v.time}; }
ProfileItem item(const LegacyProfile::Item& v) {
    return {v.type, v.weapon, v.phase, v.height, v.weight, v.score, v.date, v.time,
            v.scale, v.range, v.reserved};
}
using compat::Value;
Value object() { Value v; v.kind = compat::Kind::object; return v; }
Value number(std::int64_t n) { Value v; v.kind = compat::Kind::integer; v.integer = std::to_string(n); return v; }
Value string(std::u32string s) { Value v; v.kind = compat::Kind::string; v.string = std::move(s); return v; }
Value stats_json(const ProfileStats& s) {
    auto v = object();
    v.object = {{U"shots_raw", number(s.shots_raw)}, {U"success_raw", number(s.success_raw)},
                {U"path_bits", number(s.path_bits)}, {U"time_bits", number(s.time_bits)}};
    return v;
}
template<std::size_t N> Value items_json(const std::array<ProfileItem, N>& items) {
    Value a; a.kind = compat::Kind::array;
    for (const auto& i : items) {
        auto v = object();
        v.object = {{U"type", number(i.type)}, {U"weapon", number(i.weapon)},
            {U"phase", number(i.phase)}, {U"height", number(i.height)},
            {U"weight", number(i.weight)}, {U"score", number(i.score)},
            {U"date_raw", number(i.date_raw)}, {U"time_raw", number(i.time_raw)},
            {U"scale_bits", number(i.scale_bits)}, {U"range_bits", number(i.range_bits)}};
        Value reserved; reserved.kind = compat::Kind::array;
        for (const auto n : i.reserved) reserved.array.push_back(number(n));
        v.object.emplace_back(U"reserved", std::move(reserved));
        a.array.push_back(std::move(v));
    }
    return a;
}
}
ProfileInspection inspect_profile_bytes(std::string_view bytes, std::string_view kind,
                                        ProfileCodec codec, std::string_view dialect) {
    auto result = std::make_shared<ProfileInspection::Impl>();
    if (dialect == "iceage-triassic") result->layout = ProfileLayout::unsupported_iceage_family;
    else if (bytes.size() != (kind == "sav" ? LegacyProfile::SaveSize : LegacyProfile::RoomSize))
        result->layout = ProfileLayout::unknown;
    else if (codec == ProfileCodec::unavailable) result->layout = ProfileLayout::size_candidate_only;
    else {
        if (codec != ProfileCodec::native) throw ProfileError("invalid profile codec selection");
        const auto* data = reinterpret_cast<const std::uint8_t*>(bytes.data());
        if (kind == "sav") {
            LegacyProfile::Save decoded;
            if (!LegacyProfile::DecodeSave(data, bytes.size(), decoded)) throw ProfileError("profile codec decode failed");
            const auto encoded = LegacyProfile::EncodeSave(decoded);
            if (!std::equal(encoded.begin(), encoded.end(), data)) throw ProfileError("profile codec roundtrip mismatch");
            SaveInspection s;
            const auto& p = decoded.profile;
            s.name_bytes = p.header.name;
            for (auto b : s.name_bytes) { if (!b) break; s.name_display_latin1.push_back(b); }
            s.registration = p.header.registration; s.score = p.header.score; s.rank = p.header.rank;
            s.last_raw = stats(p.last); s.total_raw = stats(p.total);
            std::transform(p.items.begin(), p.items.end(), s.items_raw.begin(), item);
            result->save = std::move(s); result->layout = ProfileLayout::c2_1660_candidate;
        } else {
            LegacyProfile::Room decoded;
            if (!LegacyProfile::DecodeRoom(data, bytes.size(), decoded)) throw ProfileError("profile codec decode failed");
            const auto encoded = LegacyProfile::EncodeRoom(decoded);
            if (!std::equal(encoded.begin(), encoded.end(), data)) throw ProfileError("profile codec roundtrip mismatch");
            RoomInspection r;
            r.version_raw = decoded.version; r.high_score_raw = decoded.highScore;
            std::transform(decoded.items.begin(), decoded.items.end(), r.items_raw.begin(), item);
            result->room = std::move(r); result->layout = ProfileLayout::mee_7176_candidate;
        }
    }
    return ProfileInspection(std::move(result));
}
std::string ProfileInspection::export_json() const {
    auto out = object();
    std::u32string layout_name;
    switch (layout()) {
    case ProfileLayout::unsupported_iceage_family: layout_name = U"unsupported-iceage-family"; break;
    case ProfileLayout::unknown: layout_name = U"unknown"; break;
    case ProfileLayout::size_candidate_only: layout_name = U"size-candidate-only"; break;
    case ProfileLayout::c2_1660_candidate: layout_name = U"c2-1660-candidate"; break;
    case ProfileLayout::mee_7176_candidate: layout_name = U"mee-7176-candidate"; break;
    }
    out.object.emplace_back(U"layout", string(std::move(layout_name)));
    Value exact; exact.kind = compat::Kind::boolean; exact.boolean = codec_roundtrip_exact();
    out.object.emplace_back(U"codec_roundtrip_exact", std::move(exact));
    if (layout() == ProfileLayout::size_candidate_only)
        out.object.emplace_back(U"diagnostic", string(U"codec-helper-unavailable"));
    if (save()) {
        const auto& s = *save();
        constexpr char32_t hex[] = U"0123456789abcdef";
        std::u32string name_hex;
        for (auto b : s.name_bytes) { name_hex.push_back(hex[b >> 4]); name_hex.push_back(hex[b & 15]); }
        out.object.emplace_back(U"name_hex", string(std::move(name_hex)));
        out.object.emplace_back(U"registration", number(s.registration));
        out.object.emplace_back(U"score", number(s.score));
        out.object.emplace_back(U"rank", number(s.rank));
        out.object.emplace_back(U"last_raw", stats_json(s.last_raw));
        out.object.emplace_back(U"total_raw", stats_json(s.total_raw));
        out.object.emplace_back(U"items_raw", items_json(s.items_raw));
        out.object.emplace_back(U"name_display_latin1", string(s.name_display_latin1));
    } else if (room()) {
        const auto& r = *room();
        out.object.emplace_back(U"version_raw", number(r.version_raw));
        out.object.emplace_back(U"high_score_raw", number(r.high_score_raw));
        out.object.emplace_back(U"items_raw", items_json(r.items_raw));
    }
    return compat::display(out);
}
}
