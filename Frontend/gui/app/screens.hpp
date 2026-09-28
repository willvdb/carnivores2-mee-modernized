#pragma once
// Thin RmlUi binding layer: owns documents, data models and focus handling;
// reads and drives the headless PresentationModel. No SDL here.
#include "c2/frontend/gui/presentation.hpp"
#include <RmlUi/Core.h>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace c2::frontend::gui::app {
struct ScreenCallbacks {
    std::function<void()> open_folder_dialog;                       // native dialog (UI thread)
    std::function<void(RequestId, std::string)> check_folder;       // off-thread existence probe
};

class Screens {
public:
    Screens(Rml::Context& context, PresentationModel& model, std::filesystem::path asset_root, ScreenCallbacks callbacks);
    ~Screens();
    Screens(const Screens&) = delete;
    Screens& operator=(const Screens&) = delete;

    // Loads the four documents and binds data models. False on failure.
    bool load(std::string& error);
    // Applies model state to documents: visibility, dirty variables, selection
    // classes, focus restoration and the lodge aspect-fit. Idempotent.
    void sync();
    // Back semantics shared by Escape, the controller's back button and the
    // Back button: open dropdown > modal dialog > screen. False when idle.
    bool handle_back();
    bool dismiss_open_dropdown();
    // Deliberate cross-pane navigation on the console. RmlUi's spatial `nav:
    // auto` searches only inside the focused element's scroll container, so
    // leaving the list (Right) or returning to it (Left, when the pane has
    // nothing further left) is decided here. Returns true when it moved focus.
    bool cross_pane_right();
    bool cross_pane_left();
    // Re-fits the 16:9 lodge stage to the context (window pixel size and dp ratio).
    void relayout_lodge();
    // Native folder dialog lifecycle, marshalled to the UI thread by the app.
    void folder_dialog_started(RequestId id);
    void folder_dialog_finished(RequestId id, std::optional<std::string> folder, std::string error);
    bool folder_dialog_pending() const noexcept { return dialog_request_ != 0; }
    RequestId next_dialog_request() { return ++dialog_counter_; }

    Rml::ElementDocument* lodge() const noexcept { return lodge_; }
    Rml::ElementDocument* console() const noexcept { return console_; }
    Rml::ElementDocument* setup() const noexcept { return setup_; }
    Rml::ElementDocument* preview() const noexcept { return preview_; }
    // Current focus element id, or empty (test/diagnostic aid).
    std::string focused_id() const;
    // Lodge stage size in dp after the last relayout.
    Rml::Vector2f lodge_stage_dp() const noexcept { return stage_dp_; }
    bool artwork_missing(const char* slot) const;

private:
    struct HunterView { std::string id, dom_id, name, short_id, suffix; bool selected = false; };
    struct ExpeditionView { std::string id, dom_id, label, mode, flavor, short_id; bool selected = false; };
    struct OptionView { std::string id, label; };

    void bind_lodge();
    void bind_console();
    void bind_preview();
    void bind_setup();
    void refresh_views();
    void dirty_all();
    void apply_selection_classes();
    void show_screen(Screen screen, bool force);
    void focus_console_default();
    static bool focus_by_id(Rml::ElementDocument* doc, const std::string& id);
    void on_confirm_setup();

    Rml::Context& context_;
    PresentationModel& model_;
    std::filesystem::path root_;
    ScreenCallbacks callbacks_;
    Rml::ElementDocument* lodge_ = nullptr;
    Rml::ElementDocument* console_ = nullptr;
    Rml::ElementDocument* setup_ = nullptr;
    Rml::ElementDocument* preview_ = nullptr;
    Rml::DataModelHandle lodge_model_, console_model_, preview_model_, setup_model_;
    std::vector<HunterView> hunters_;
    std::vector<ExpeditionView> expeditions_;
    std::vector<OptionView> areas_, weapons_, times_;
    std::vector<std::string> notes_;
    std::string preview_return_focus_;
    std::optional<Screen> shown_;
    bool preview_shown_ = false;
    bool syncing_ = false;
    std::uint64_t synced_version_ = ~std::uint64_t{0};
    RequestId dialog_request_ = 0;
    RequestId dialog_counter_ = 0;
    std::string dialog_error_;
    Rml::Vector2f stage_dp_;
    bool lodge_art_missing_ = false;
    bool preview_art_missing_ = false;
};
} // namespace c2::frontend::gui::app
