#include "screens.hpp"
#include <RmlUi/Core/Elements/ElementFormControlSelect.h>
#include <algorithm>

namespace c2::frontend::gui::app {
namespace {
std::string short_id(const std::string& id) { return id.size() > 8 ? id.substr(id.size() - 8) : id; }
const char* destination_element(LodgeDestination d) {
    switch (d) {
    case LodgeDestination::profile: return "dest-profile";
    case LodgeDestination::exit: return "dest-exit";
    default: return "dest-expeditions";
    }
}
} // namespace

Screens::Screens(Rml::Context& context, PresentationModel& model, std::filesystem::path asset_root, ScreenCallbacks callbacks)
    : context_(context), model_(model), root_(std::move(asset_root)), callbacks_(std::move(callbacks)) {}

Screens::~Screens() {
    // Documents are owned by the context; closing them here keeps listener
    // lifetimes inside this object's lifetime.
    for (auto* doc : {lodge_, console_, setup_, preview_, review_})
        if (doc) doc->Close();
}

bool Screens::load(std::string& error) {
    std::error_code ec;
    lodge_art_missing_ = !std::filesystem::is_regular_file(root_ / "images" / "lodge_background.png", ec);
    preview_art_missing_ = !std::filesystem::is_regular_file(root_ / "images" / "expedition_preview.png", ec);
    for (const auto& o : PresentationModel::demo_areas()) areas_.push_back({o.id, o.label});
    for (const auto& o : PresentationModel::demo_weapons()) weapons_.push_back({o.id, o.label});
    for (const auto& o : PresentationModel::demo_times()) times_.push_back({o.id, o.label});
    bind_lodge();
    bind_console();
    bind_loop();
    bind_preview();
    bind_setup();
    auto load = [&](const char* name) -> Rml::ElementDocument* {
        auto* doc = context_.LoadDocument((root_ / "rml" / name).u8string());
        if (!doc) error += std::string("cannot load document ") + name + "; ";
        return doc;
    };
    lodge_ = load("lodge.rml");
    console_ = load("console.rml");
    setup_ = load("setup.rml");
    preview_ = load("preview.rml");
    review_ = load("review.rml");
    if (!lodge_ || !console_ || !setup_ || !preview_ || !review_) return false;
    if (lodge_art_missing_)
        if (auto* stage = lodge_->GetElementById("lodge-stage")) stage->SetClass("no-artwork", true);
    if (preview_art_missing_)
        if (auto* art = console_->GetElementById("preview-art")) art->SetClass("no-artwork", true);
    // Demo loadout options come from the model's lists, added as real option
    // elements (a data-for template would leave a hidden extra option).
    auto fill = [&](const char* id, const std::vector<OptionView>& options) {
        auto* select = rmlui_dynamic_cast<Rml::ElementFormControlSelect*>(console_->GetElementById(id));
        if (!select) return;
        for (const auto& o : options) select->Add(o.label, o.id);
    };
    fill("area-select", areas_);
    fill("weapon-select", weapons_);
    fill("time-select", times_);
    refresh_views();
    dirty_all();
    show_screen(model_.screen(), true);
    relayout_lodge();
    return true;
}

// --- data models -------------------------------------------------------------
void Screens::bind_lodge() {
    auto ctor = context_.CreateDataModel("lodge");
    ctor.BindFunc("hunter_line", [this](Rml::Variant& v) {
        if (const auto* h = model_.viewed_hunter_row())
            v = "Hunter: " + h->name + " [" + short_id(h->id) + "]" + (h->archived ? " (archived)" : "") +
                "  |  Evaluation name: " + model_.confirmed_settings().hunter_name;
        else if (model_.load_state() == LoadState::ready)
            v = std::string("Hunter: none selected  |  Evaluation name: ") + model_.confirmed_settings().hunter_name;
        else
            v = std::string("Hunter: (store not loaded)");
    });
    ctor.BindFunc("source_line", [this](Rml::Variant& v) { v = model_.source().label(); });
    ctor.BindFunc("status_line", [this](Rml::Variant& v) {
        switch (model_.load_state()) {
        case LoadState::loading: v = std::string("Store: loading..."); break;
        case LoadState::error: v = "Store error: " + model_.load_error(); break;
        case LoadState::ready: {
            const auto* s = model_.snapshot();
            v = "Store: schema " + std::to_string(s->schema_version) + ", " + std::to_string(s->hunters.size()) +
                " hunter(s), " + std::to_string(s->expeditions.size()) + " expedition(s)" +
                (s->manifest_present ? "" : " (no lodge.json: empty default manifest)") +
                (model_.confirmed_settings().content_folder.empty() ? "" : "  |  Evaluation folder: " + model_.confirmed_settings().content_folder);
            break;
        }
        default: v = std::string("Store: idle");
        }
    });
    ctor.BindFunc("art_missing", [this](Rml::Variant& v) { v = lodge_art_missing_; });
    ctor.BindEventCallback("go", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
        if (args.empty()) return;
        const auto where = args[0].Get<Rml::String>();
        if (where == "expeditions") model_.navigate(LodgeDestination::expeditions);
        else if (where == "profile") model_.navigate(LodgeDestination::profile);
        else if (where == "exit") model_.navigate(LodgeDestination::exit);
    });
    lodge_model_ = ctor.GetModelHandle();
}

void Screens::bind_console() {
    auto ctor = context_.CreateDataModel("console");
    if (auto s = ctor.RegisterStruct<ExpeditionView>()) {
        s.RegisterMember("id", &ExpeditionView::id);
        s.RegisterMember("dom_id", &ExpeditionView::dom_id);
        s.RegisterMember("label", &ExpeditionView::label);
        s.RegisterMember("mode", &ExpeditionView::mode);
        s.RegisterMember("flavor", &ExpeditionView::flavor);
        s.RegisterMember("short_id", &ExpeditionView::short_id);
        s.RegisterMember("selected", &ExpeditionView::selected);
    }
    ctor.RegisterArray<std::vector<ExpeditionView>>();
    if (auto s = ctor.RegisterStruct<HunterView>()) {
        s.RegisterMember("id", &HunterView::id);
        s.RegisterMember("dom_id", &HunterView::dom_id);
        s.RegisterMember("name", &HunterView::name);
        s.RegisterMember("short_id", &HunterView::short_id);
        s.RegisterMember("suffix", &HunterView::suffix);
        s.RegisterMember("selected", &HunterView::selected);
    }
    ctor.RegisterArray<std::vector<HunterView>>();
    ctor.Bind("expeditions", &expeditions_);
    ctor.Bind("hunters", &hunters_);
    ctor.BindFunc("source_line", [this](Rml::Variant& v) { v = model_.source().label(); });
    ctor.BindFunc("expedition_count", [this](Rml::Variant& v) { v = static_cast<int>(expeditions_.size()); });
    ctor.BindFunc("state_loading", [this](Rml::Variant& v) { v = model_.load_state() == LoadState::loading; });
    ctor.BindFunc("state_error", [this](Rml::Variant& v) { v = model_.load_state() == LoadState::error; });
    ctor.BindFunc("state_empty", [this](Rml::Variant& v) {
        v = model_.load_state() == LoadState::ready && model_.snapshot()->expeditions.empty() && model_.snapshot()->manifest_present;
    });
    ctor.BindFunc("state_no_manifest", [this](Rml::Variant& v) {
        v = model_.load_state() == LoadState::ready && !model_.snapshot()->manifest_present;
    });
    ctor.BindFunc("load_error", [this](Rml::Variant& v) { v = model_.load_error(); });
    ctor.BindFunc("has_selection", [this](Rml::Variant& v) { v = model_.selected_expedition_row() != nullptr; });
    ctor.BindFunc("sel_label", [this](Rml::Variant& v) { const auto* e = model_.selected_expedition_row(); v = e ? expedition_label(*e) : std::string(); });
    ctor.BindFunc("sel_id", [this](Rml::Variant& v) { const auto* e = model_.selected_expedition_row(); v = e ? e->id : std::string(); });
    ctor.BindFunc("sel_path", [this](Rml::Variant& v) { const auto* e = model_.selected_expedition_row(); v = e ? e->path : std::string(); });
    ctor.BindFunc("sel_mode", [this](Rml::Variant& v) { const auto* e = model_.selected_expedition_row(); v = e ? e->mode : std::string(); });
    ctor.BindFunc("sel_flavor", [this](Rml::Variant& v) { const auto* e = model_.selected_expedition_row(); v = e ? e->path_flavor : std::string(); });
    ctor.BindFunc("launch_reason", [](Rml::Variant& v) { v = std::string(PresentationModel::launch_unavailable_reason()); });
    ctor.BindFunc("area", [this](Rml::Variant& v) { v = model_.area(); },
                  [this](const Rml::Variant& v) { model_.set_area(v.Get<Rml::String>()); });
    ctor.BindFunc("weapon", [this](Rml::Variant& v) { v = model_.weapon(); },
                  [this](const Rml::Variant& v) { model_.set_weapon(v.Get<Rml::String>()); });
    ctor.BindFunc("time_of_day", [this](Rml::Variant& v) { v = model_.time_of_day(); },
                  [this](const Rml::Variant& v) { model_.set_time(v.Get<Rml::String>()); });
    ctor.BindEventCallback("back", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { handle_back(); });
    ctor.BindEventCallback("select_expedition", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
        if (!args.empty()) model_.select_expedition(args[0].Get<Rml::String>());
    });
    ctor.BindEventCallback("view_hunter", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
        if (!args.empty()) model_.view_hunter(args[0].Get<Rml::String>());
    });
    ctor.BindEventCallback("preview", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        model_.open_preview();
    });
    console_model_ = ctor.GetModelHandle();
}

void Screens::bind_preview() {
    auto ctor = context_.CreateDataModel("preview");
    ctor.RegisterArray<std::vector<std::string>>();
    ctor.Bind("notes", &notes_);
    auto field = [&](const char* name, std::string PreviewSummary::*member) {
        ctor.BindFunc(name, [this, member](Rml::Variant& v) { v = model_.preview_summary().*member; });
    };
    field("expedition_label", &PreviewSummary::expedition_label);
    field("expedition_id", &PreviewSummary::expedition_id);
    field("expedition_path", &PreviewSummary::expedition_path);
    field("hunter_label", &PreviewSummary::hunter_label);
    field("area", &PreviewSummary::area);
    field("weapon", &PreviewSummary::weapon);
    field("time_of_day", &PreviewSummary::time_of_day);
    field("data_source", &PreviewSummary::data_source);
    ctor.BindEventCallback("close", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        model_.close_preview();
    });
    preview_model_ = ctor.GetModelHandle();
}

void Screens::bind_setup() {
    auto ctor = context_.CreateDataModel("setup");
    // Text fields are two-way but are only re-pushed to the widget when the
    // model changes them (screen entry, folder dialog), never on every sync,
    // so the caret and selection survive typing.
    ctor.BindFunc("draft_name", [this](Rml::Variant& v) { v = model_.draft().values.hunter_name; },
                  [this](const Rml::Variant& v) { model_.set_draft_name(v.Get<Rml::String>()); });
    ctor.BindFunc("draft_folder", [this](Rml::Variant& v) { v = model_.draft().values.content_folder; },
                  [this](const Rml::Variant& v) { model_.set_draft_folder(v.Get<Rml::String>()); });
    ctor.BindFunc("name_message", [this](Rml::Variant& v) { v = model_.draft().name_message; });
    ctor.BindFunc("folder_message", [this](Rml::Variant& v) { v = model_.draft().folder_message; });
    ctor.BindFunc("folder_error", [this](Rml::Variant& v) { v = model_.draft().folder_status == FolderStatus::error; });
    ctor.BindFunc("folder_ok", [this](Rml::Variant& v) { v = model_.draft().folder_status == FolderStatus::ok; });
    ctor.BindFunc("dialog_pending", [this](Rml::Variant& v) { v = dialog_request_ != 0; });
    ctor.BindFunc("dialog_error", [this](Rml::Variant& v) { v = dialog_error_; });
    ctor.BindFunc("confirmed_name", [this](Rml::Variant& v) { v = model_.confirmed_settings().hunter_name; });
    ctor.BindFunc("confirmed_folder", [this](Rml::Variant& v) { v = model_.confirmed_settings().content_folder; });
    ctor.BindEventCallback("browse", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        if (dialog_request_ == 0 && callbacks_.open_folder_dialog) callbacks_.open_folder_dialog();
    });
    ctor.BindEventCallback("confirm", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { on_confirm_setup(); });
    ctor.BindEventCallback("cancel", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        model_.cancel_setup();
    });
    setup_model_ = ctor.GetModelHandle();
}

void Screens::on_confirm_setup() {
    const auto result = model_.confirm_setup();
    if (result == ConfirmResult::checking_folder && callbacks_.check_folder)
        callbacks_.check_folder(model_.draft().folder_request, model_.draft().values.content_folder);
}

// --- view refresh ----------------------------------------------------------------
void Screens::refresh_views() {
    std::vector<ExpeditionView> expeditions;
    std::vector<HunterView> hunters;
    if (const auto* s = model_.snapshot()) {
        for (const auto& e : s->expeditions)
            expeditions.push_back({e.id, "exp-" + e.id, expedition_label(e), e.mode, e.path_flavor, short_id(e.id), false});
        for (const auto& h : s->hunters) {
            std::string suffix;
            if (h.archived) suffix += " (archived)";
            if (s->active_hunter && *s->active_hunter == h.id) suffix += " (active in store)";
            hunters.push_back({h.id, "hunter-" + h.id, h.name, short_id(h.id), suffix, false});
        }
    }
    auto same = [](const auto& a, const auto& b) {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i)
            if (a[i].id != b[i].id || a[i].dom_id != b[i].dom_id) return false;
        return true;
    };
    // Rebuilding a data-for list destroys its elements (and any focus inside),
    // so only membership changes dirty the arrays; selection is a class toggle.
    if (!same(expeditions, expeditions_)) {
        expeditions_ = std::move(expeditions);
        console_model_.DirtyVariable("expeditions");
    }
    if (!same(hunters, hunters_)) {
        hunters_ = std::move(hunters);
        console_model_.DirtyVariable("hunters");
    }
    notes_ = model_.preview_summary().notes;
    preview_model_.DirtyVariable("notes");
}

void Screens::dirty_all() {
    for (const char* n : {"hunter_line", "source_line", "status_line", "art_missing"}) lodge_model_.DirtyVariable(n);
    for (const char* n : {"source_line", "expedition_count", "state_loading", "state_error", "state_empty", "state_no_manifest",
                          "load_error", "has_selection", "sel_label", "sel_id", "sel_path", "sel_mode", "sel_flavor",
                          "launch_reason", "area", "weapon", "time_of_day"})
        console_model_.DirtyVariable(n);
    for (const char* n : {"expedition_label", "expedition_id", "expedition_path", "hunter_label", "area", "weapon",
                          "time_of_day", "data_source"})
        preview_model_.DirtyVariable(n);
    for (const char* n : {"name_message", "folder_message", "folder_error", "folder_ok", "dialog_pending", "confirmed_name",
                          "confirmed_folder"})
        setup_model_.DirtyVariable(n);
}

void Screens::apply_selection_classes() {
    if (!console_) return;
    const auto& selected = model_.selected_expedition();
    for (auto& e : expeditions_) {
        e.selected = selected && *selected == e.id;
        if (auto* row = console_->GetElementById(e.dom_id)) row->SetClass("selected", e.selected);
    }
    const auto viewed = model_.viewed_hunter();
    for (auto& h : hunters_) {
        h.selected = viewed && *viewed == h.id;
        if (auto* row = console_->GetElementById(h.dom_id)) row->SetClass("selected", h.selected);
    }
}

// --- sync -------------------------------------------------------------------------------
void Screens::sync() {
    if (!lodge_ || !console_ || !setup_ || !preview_ || syncing_) return;
    struct Guard { bool& flag; ~Guard() { flag = false; } } guard{syncing_};
    syncing_ = true;
    const bool changed = model_.version() != synced_version_;
    synced_version_ = model_.version();
    if (changed) {
        refresh_views();
        dirty_all();
    }
    show_screen(model_.screen(), false);
    // Modal preview dialog: remember and restore the focus around it.
    if (model_.preview_open() && !preview_shown_ && !review_shown_) {
        preview_return_focus_ = focused_id();
        if (preview_return_focus_.empty()) preview_return_focus_ = "preview-button";
        preview_shown_ = true;
        preview_->Show(Rml::ModalFlag::Modal, Rml::FocusFlag::Document);
        focus_by_id(preview_, "preview-close");
    } else if (!model_.preview_open() && preview_shown_) {
        preview_shown_ = false;
        preview_->Hide();
        if (!focus_by_id(console_, preview_return_focus_)) focus_console_default();
    }
    sync_loop();
    apply_selection_classes();
    // A rebuilt list may have destroyed the focused row; land somewhere sensible.
    if (model_.screen() == Screen::console && !preview_shown_ && !review_shown_) {
        auto* focus = context_.GetFocusElement();
        if (!focus || focus == console_ || focus->GetOwnerDocument() != console_) focus_console_default();
    }
}

void Screens::show_screen(Screen screen, bool force) {
    if (!force && shown_ && *shown_ == screen) return;
    const bool entering_setup = screen == Screen::setup;
    shown_ = screen;
    for (auto* doc : {lodge_, console_, setup_}) doc->Hide();
    switch (screen) {
    case Screen::lodge: {
        lodge_->Show(Rml::ModalFlag::None, Rml::FocusFlag::Document);
        const auto dest = model_.lodge_return_focus().value_or(LodgeDestination::expeditions);
        focus_by_id(lodge_, destination_element(dest));
        break;
    }
    case Screen::console:
        console_->Show(Rml::ModalFlag::None, Rml::FocusFlag::Document);
        focus_console_default();
        break;
    case Screen::setup:
        setup_->Show(Rml::ModalFlag::None, Rml::FocusFlag::Document);
        focus_by_id(setup_, "name-input");
        break;
    }
    if (entering_setup) {
        setup_model_.DirtyVariable("draft_name");
        setup_model_.DirtyVariable("draft_folder");
    }
}

void Screens::focus_console_default() {
    if (const auto& sel = model_.selected_expedition())
        if (focus_by_id(console_, "exp-" + *sel)) return;
    if (!expeditions_.empty() && focus_by_id(console_, expeditions_.front().dom_id)) return;
    focus_by_id(console_, "console-back");
}

bool Screens::focus_by_id(Rml::ElementDocument* doc, const std::string& id) {
    if (!doc || id.empty()) return false;
    auto* element = doc->GetElementById(id);
    if (!element) return false;
    element->Focus(true);
    element->ScrollIntoView(Rml::ScrollIntoViewOptions{Rml::ScrollAlignment::Nearest, Rml::ScrollAlignment::Nearest});
    return true;
}

std::string Screens::focused_id() const {
    auto* focus = context_.GetFocusElement();
    return focus ? focus->GetId() : std::string();
}

// --- back, dropdowns ----------------------------------------------------------------------
bool Screens::dismiss_open_dropdown() {
    auto* focus = context_.GetFocusElement();
    for (auto* e = focus; e; e = e->GetParentNode()) {
        if (e->GetTagName() != "select") continue;
        // WidgetDropDown marks its selectbox/value/arrow children :checked while open.
        bool open = false;
        for (int i = 0; i < e->GetNumChildren(true); ++i)
            if (auto* child = e->GetChild(i); child && child->GetTagName() == "selectbox" && child->IsPseudoClassSet("checked")) open = true;
        if (!open) return false;
        e->Click();   // the same toggle the mouse performs; closes the box, keeps focus
        return true;
    }
    return false;
}

bool Screens::handle_back() {
    if (dismiss_open_dropdown()) return true;
    if (review_open_) { review_open_ = false; sync(); return true; }
    if (model_.preview_open()) {
        model_.close_preview();
        sync();
        return true;
    }
    if (model_.back()) {
        sync();
        return true;
    }
    return false;
}

// --- cross-pane navigation ---------------------------------------------------------------
bool Screens::cross_pane_right() {
    if (model_.screen() != Screen::console || preview_shown_ || review_shown_) return false;
    auto* focus = context_.GetFocusElement();
    if (!focus || !focus->IsClassSet("row")) return false;
    for (const char* id : {"association-select", "review-button"})
        if (auto* target = console_->GetElementById(id); target && target->IsVisible(true)) {
            target->Focus(true);
            target->ScrollIntoView(Rml::ScrollIntoViewOptions{Rml::ScrollAlignment::Nearest, Rml::ScrollAlignment::Nearest});
            return true;
        }
    return false;
}

bool Screens::cross_pane_left() {
    if (model_.screen() != Screen::console || preview_shown_ || review_shown_) return false;
    auto* focus = context_.GetFocusElement();
    if (!focus || focus->GetOwnerDocument() != console_ || focus->IsClassSet("row")) return false;
    if (focus->GetTagName() == "input") return false;   // caret keys stay with text fields
    bool in_details = false;
    for (auto* e = focus; e; e = e->GetParentNode())
        if (e->IsClassSet("details-pane")) in_details = true;
    if (!in_details) return false;
    focus_console_default();
    return true;
}

// --- lodge aspect fit -----------------------------------------------------------------------
void Screens::relayout_lodge() {
    if (!lodge_) return;
    auto* stage = lodge_->GetElementById("lodge-stage");
    if (!stage) return;
    const auto dims = context_.GetDimensions();
    const float dp = context_.GetDensityIndependentPixelRatio() > 0.f ? context_.GetDensityIndependentPixelRatio() : 1.f;
    const float w = static_cast<float>(dims.x) / dp, h = static_cast<float>(dims.y) / dp;
    float sw = w, sh = h;
    if (w * 9.f > h * 16.f) sw = h * 16.f / 9.f;   // wider than 16:9: pillarbox
    else sh = w * 9.f / 16.f;                       // taller: letterbox
    stage_dp_ = {sw, sh};
    stage->SetProperty(Rml::PropertyId::Width, Rml::Property(sw, Rml::Unit::DP));
    stage->SetProperty(Rml::PropertyId::Height, Rml::Property(sh, Rml::Unit::DP));
}

bool Screens::artwork_missing(const char* slot) const {
    return std::string(slot) == "lodge" ? lodge_art_missing_ : preview_art_missing_;
}

// --- folder dialog --------------------------------------------------------------------------
void Screens::folder_dialog_started(RequestId id) {
    dialog_request_ = id;
    dialog_error_.clear();
    setup_model_.DirtyVariable("dialog_pending");
    setup_model_.DirtyVariable("dialog_error");
}

void Screens::folder_dialog_finished(RequestId id, std::optional<std::string> folder, std::string error) {
    if (id == 0 || id != dialog_request_) return;   // stale or unknown: ignored
    dialog_request_ = 0;
    setup_model_.DirtyVariable("dialog_pending");
    if (model_.screen() != Screen::setup) return;   // user left setup meanwhile: nothing to apply
    if (folder) {
        model_.set_draft_folder(*folder);
        setup_model_.DirtyVariable("draft_folder");
        const auto request = model_.request_folder_check();
        if (request && callbacks_.check_folder) callbacks_.check_folder(request, *folder);
    } else if (!error.empty()) {
        dialog_error_ = "Folder dialog failed: " + error;   // draft text is kept unchanged
        setup_model_.DirtyVariable("dialog_error");
    }
    sync();
}
} // namespace c2::frontend::gui::app
