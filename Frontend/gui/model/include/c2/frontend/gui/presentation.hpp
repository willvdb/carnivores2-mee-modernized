#pragma once
// Headless presentation/controller model for the GUI evaluation slice.
// Owns screen state, selected identities, demo edits, loading/error state and
// pending request identity. Contains no SDL, OpenGL, RmlUi types or widget
// pointers; the binding layer owns elements and focus implementation.
#include "c2/frontend/gui/source.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace c2::frontend::gui {
using RequestId = std::uint64_t;
enum class Screen { lodge, console, setup };
enum class LodgeDestination { expeditions, profile, exit };
enum class LoadState { idle, loading, ready, error };
enum class FolderStatus { unchecked, checking, ok, error };
enum class ConfirmResult { committed, invalid, checking_folder };

struct DataSource {
    enum class Kind { demo, supplied };
    Kind kind = Kind::demo;
    std::string directory;   // UTF-8 display path
    std::string label() const;
};
struct DemoOption {
    std::string id;
    std::string label;
};
// In-memory evaluation settings. Never written to a store.
struct DemoSettings {
    std::string hunter_name;
    std::string content_folder;
};
struct SetupDraft {
    DemoSettings values;
    FolderStatus folder_status = FolderStatus::unchecked;
    std::string folder_message;
    std::string name_message;
    RequestId folder_request = 0;
};
struct PreviewSummary {
    std::string expedition_label, expedition_id, expedition_path;
    std::string hunter_label, hunter_id;
    std::string area, weapon, time_of_day;
    std::string data_source;
    std::vector<std::string> notes;
};

class PresentationModel {
public:
    explicit PresentationModel(DataSource source);

    // --- data source and loading -------------------------------------------
    const DataSource& source() const noexcept { return source_; }
    // Marks loading and returns the request identity the completion must carry.
    RequestId begin_load();
    // Applies a completion. Returns false (and changes nothing) when `id` is
    // not the newest load request.
    bool complete_load(RequestId id, SnapshotResult result);
    LoadState load_state() const noexcept { return load_state_; }
    const std::string& load_error() const noexcept { return load_error_; }
    const StoreSnapshot* snapshot() const noexcept { return snapshot_ ? &*snapshot_ : nullptr; }
    RequestId current_load_request() const noexcept { return load_request_; }

    // --- routing ------------------------------------------------------------
    Screen screen() const noexcept { return screen_; }
    // Only from the lodge. `exit` sets exit_requested() and stays on the lodge.
    bool navigate(LodgeDestination destination);
    // Dismisses the preview dialog first, then leaves the current screen for
    // the lodge (setup leaves by cancel). Returns false when on the lodge
    // with nothing to dismiss. Dropdown dismissal belongs to the binding.
    bool back();
    bool exit_requested() const noexcept { return exit_requested_; }
    // The destination used to leave the lodge; restore focus there on return.
    std::optional<LodgeDestination> lodge_return_focus() const noexcept { return lodge_return_focus_; }

    // --- hunters: a local viewing selection, never a persisted change --------
    bool view_hunter(const std::string& id);
    std::optional<std::string> viewed_hunter() const;   // defaults to the store's active hunter
    const HunterRow* viewed_hunter_row() const;

    // --- expeditions ----------------------------------------------------------
    bool select_expedition(const std::string& id);
    void clear_expedition();
    const std::optional<std::string>& selected_expedition() const noexcept { return selected_expedition_; }
    const ExpeditionRow* selected_expedition_row() const;

    // --- demo loadout: labeled demonstration values, not catalog data --------
    static const std::vector<DemoOption>& demo_areas();
    static const std::vector<DemoOption>& demo_weapons();
    static const std::vector<DemoOption>& demo_times();
    bool set_area(const std::string& id);
    bool set_weapon(const std::string& id);
    bool set_time(const std::string& id);
    const std::string& area() const noexcept { return area_; }
    const std::string& weapon() const noexcept { return weapon_; }
    const std::string& time_of_day() const noexcept { return time_; }

    // --- demo preview dialog and the disabled launch --------------------------
    bool can_open_preview() const;
    bool open_preview();
    void close_preview();
    bool preview_open() const noexcept { return preview_open_; }
    PreviewSummary preview_summary() const;
    static const char* launch_unavailable_reason();

    // --- profile/setup: in-memory evaluation state only ----------------------
    const DemoSettings& confirmed_settings() const noexcept { return confirmed_; }
    const SetupDraft& draft() const noexcept { return draft_; }
    void set_draft_name(std::string name);
    void set_draft_folder(std::string folder);   // resets the folder check
    // Starts a folder check; the binding runs the filesystem probe off-thread
    // and reports with complete_folder_check(). Returns 0 for an empty path.
    RequestId request_folder_check();
    bool complete_folder_check(RequestId id, bool is_directory, std::string message);
    // Validates the draft. `checking_folder` means a check was started and
    // the confirm completes automatically when that check succeeds.
    ConfirmResult confirm_setup();
    void cancel_setup();

    // --- change tracking ------------------------------------------------------
    std::uint64_t version() const noexcept { return version_; }

private:
    void touch() noexcept { ++version_; }
    void commit_draft();
    DataSource source_;
    RequestId next_request_ = 1;
    RequestId load_request_ = 0;
    LoadState load_state_ = LoadState::idle;
    std::string load_error_;
    std::optional<StoreSnapshot> snapshot_;
    Screen screen_ = Screen::lodge;
    bool exit_requested_ = false;
    std::optional<LodgeDestination> lodge_return_focus_;
    std::optional<std::string> viewed_hunter_;
    std::optional<std::string> selected_expedition_;
    std::string area_, weapon_, time_;
    bool preview_open_ = false;
    DemoSettings confirmed_;
    SetupDraft draft_;
    bool confirm_after_check_ = false;
    std::uint64_t version_ = 0;
};
} // namespace c2::frontend::gui
