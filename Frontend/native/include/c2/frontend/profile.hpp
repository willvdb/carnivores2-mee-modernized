#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace c2::frontend {
// Explicit in-process selection, never a historical helper path or trust pin.
enum class ProfileCodec { unavailable, native };
enum class ProfileLayout { unsupported_iceage_family, unknown, size_candidate_only,
                           c2_1660_candidate, mee_7176_candidate };
class ProfileError : public std::runtime_error { public: using std::runtime_error::runtime_error; };
struct ProfileStats {
    std::int32_t shots_raw{}, success_raw{};
    std::uint32_t path_bits{}, time_bits{};
};
struct ProfileItem {
    std::int32_t type{}, weapon{}, phase{}, height{}, weight{}, score{}, date_raw{}, time_raw{};
    std::uint32_t scale_bits{}, range_bits{};
    std::array<std::int32_t, 4> reserved{};
};
struct SaveInspection {
    std::array<std::uint8_t, 128> name_bytes{};
    std::u32string name_display_latin1;
    std::int32_t registration{}, score{}, rank{};
    ProfileStats last_raw, total_raw;
    std::array<ProfileItem, 24> items_raw{};
};
struct RoomInspection {
    std::int32_t version_raw{}, high_score_raw{};
    std::array<ProfileItem, 128> items_raw{};
};
// Owns immutable projections, independent of input lifetime. Candidate layouts
// establish byte roundtrip only, never gameplay compatibility or semantic validity.
class ProfileInspection {
public:
    ProfileLayout layout() const noexcept;
    bool codec_roundtrip_exact() const noexcept;
    const std::optional<SaveInspection>& save() const noexcept;
    const std::optional<RoomInspection>& room() const noexcept;
    // Python codec_inspect projection: insertion order, ASCII escapes, indent 2 + LF.
    std::string export_json() const;
private:
    struct Impl;
    std::shared_ptr<const Impl> impl_;
    explicit ProfileInspection(std::shared_ptr<const Impl>);
    friend ProfileInspection inspect_profile_bytes(std::string_view, std::string_view,
                                                    ProfileCodec, std::string_view);
};
// Pure memory operation. Exact 'sav' selects save; every other kind selects room.
// Exact 'iceage-triassic' takes precedence over size and codec availability.
// No environment lookup, filesystem access, process execution or persisted encoding.
ProfileInspection inspect_profile_bytes(std::string_view bytes, std::string_view kind,
    ProfileCodec codec = ProfileCodec::unavailable, std::string_view dialect = "unknown");
}
