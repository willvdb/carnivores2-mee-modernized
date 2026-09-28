#include "c2/frontend/gui/presentation.hpp"
#include <algorithm>
#include <cctype>

namespace c2::frontend::gui {
namespace {
std::string trimmed(const std::string& s) {
    auto b = s.begin(), e = s.end();
    while (b != e && std::isspace(static_cast<unsigned char>(*b))) ++b;
    while (e != b && std::isspace(static_cast<unsigned char>(*(e - 1)))) --e;
    return std::string(b, e);
}
const DemoOption* find_option(const std::vector<DemoOption>& options, const std::string& id) {
    auto it = std::find_if(options.begin(), options.end(), [&](const DemoOption& o) { return o.id == id; });
    return it == options.end() ? nullptr : &*it;
}
std::string option_label(const std::vector<DemoOption>& options, const std::string& id) {
    const auto* o = find_option(options, id);
    return o ? o->label : std::string("(none)");
}
std::string short_id(const std::string& id) {
    return id.size() > 8 ? id.substr(id.size() - 8) : id;
}
} // namespace

std::string DataSource::label() const {
    if (kind == Kind::demo)
        return "Demo data: disposable authored fixture store, not your lodge (" + directory + ")";
    return "Read-only store: " + directory + " (never modified by this evaluation)";
}

PresentationModel::PresentationModel(DataSource source) : source_(std::move(source)) {
    area_ = demo_areas().front().id;
    weapon_ = demo_weapons().front().id;
    time_ = demo_times().front().id;
    confirmed_.hunter_name = "Evaluation Hunter";
    draft_ = SetupDraft{};
}

// --- loading -----------------------------------------------------------------
RequestId PresentationModel::begin_load() {
    load_request_ = next_request_++;
    load_state_ = LoadState::loading;
    load_error_.clear();
    touch();
    return load_request_;
}

bool PresentationModel::complete_load(RequestId id, SnapshotResult result) {
    if (id != load_request_ || load_state_ != LoadState::loading) return false;
    if (result.ok()) {
        snapshot_ = std::move(result.snapshot);
        load_state_ = LoadState::ready;
        load_error_.clear();
        // Selections are identities; drop any that the fresh observation lacks.
        if (viewed_hunter_ && !viewed_hunter_row()) viewed_hunter_.reset();
        if (selected_expedition_ && !selected_expedition_row()) {
            selected_expedition_.reset();
            preview_open_ = false;
        }
    } else {
        snapshot_.reset();
        selected_expedition_.reset();
        viewed_hunter_.reset();
        preview_open_ = false;
        load_state_ = LoadState::error;
        load_error_ = result.error.empty() ? "store read failed" : result.error;
    }
    touch();
    return true;
}

// --- routing -----------------------------------------------------------------
bool PresentationModel::navigate(LodgeDestination destination) {
    if (screen_ != Screen::lodge || preview_open_) return false;
    lodge_return_focus_ = destination;
    switch (destination) {
    case LodgeDestination::expeditions: screen_ = Screen::console; break;
    case LodgeDestination::profile:
        screen_ = Screen::setup;
        draft_ = SetupDraft{};
        draft_.values = confirmed_;
        confirm_after_check_ = false;
        break;
    case LodgeDestination::exit: exit_requested_ = true; break;
    }
    touch();
    return true;
}

bool PresentationModel::back() {
    if (preview_open_) {
        close_preview();
        return true;
    }
    if (screen_ == Screen::setup) {
        cancel_setup();
        return true;
    }
    if (screen_ == Screen::console) {
        screen_ = Screen::lodge;
        touch();
        return true;
    }
    return false;
}

// --- hunters -----------------------------------------------------------------
bool PresentationModel::view_hunter(const std::string& id) {
    if (!snapshot_) return false;
    const bool known = std::any_of(snapshot_->hunters.begin(), snapshot_->hunters.end(),
                                   [&](const HunterRow& h) { return h.id == id; });
    if (!known) return false;
    viewed_hunter_ = id;
    touch();
    return true;
}

std::optional<std::string> PresentationModel::viewed_hunter() const {
    if (viewed_hunter_) return viewed_hunter_;
    if (snapshot_) return snapshot_->active_hunter;
    return std::nullopt;
}

const HunterRow* PresentationModel::viewed_hunter_row() const {
    if (!snapshot_) return nullptr;
    const auto id = viewed_hunter();
    if (!id) return nullptr;
    auto it = std::find_if(snapshot_->hunters.begin(), snapshot_->hunters.end(),
                           [&](const HunterRow& h) { return h.id == *id; });
    return it == snapshot_->hunters.end() ? nullptr : &*it;
}

// --- expeditions ---------------------------------------------------------------
bool PresentationModel::select_expedition(const std::string& id) {
    if (!snapshot_) return false;
    const bool known = std::any_of(snapshot_->expeditions.begin(), snapshot_->expeditions.end(),
                                   [&](const ExpeditionRow& e) { return e.id == id; });
    if (!known) return false;
    selected_expedition_ = id;
    touch();
    return true;
}

void PresentationModel::clear_expedition() {
    if (!selected_expedition_) return;
    selected_expedition_.reset();
    touch();
}

const ExpeditionRow* PresentationModel::selected_expedition_row() const {
    if (!snapshot_ || !selected_expedition_) return nullptr;
    auto it = std::find_if(snapshot_->expeditions.begin(), snapshot_->expeditions.end(),
                           [&](const ExpeditionRow& e) { return e.id == *selected_expedition_; });
    return it == snapshot_->expeditions.end() ? nullptr : &*it;
}

// --- demo loadout ---------------------------------------------------------------
const std::vector<DemoOption>& PresentationModel::demo_areas() {
    static const std::vector<DemoOption> v{{"demo-area-1", "Demo area 1 (sample)"},
                                           {"demo-area-2", "Demo area 2 (sample)"},
                                           {"demo-area-3", "Demo area 3 (sample, long label to exercise dropdown width)"}};
    return v;
}
const std::vector<DemoOption>& PresentationModel::demo_weapons() {
    static const std::vector<DemoOption> v{{"demo-weapon-1", "Demo weapon 1 (sample)"},
                                           {"demo-weapon-2", "Demo weapon 2 (sample)"}};
    return v;
}
const std::vector<DemoOption>& PresentationModel::demo_times() {
    static const std::vector<DemoOption> v{{"demo-time-0", "Morning (sample)"},
                                           {"demo-time-1", "Day (sample)"},
                                           {"demo-time-2", "Evening (sample)"}};
    return v;
}
bool PresentationModel::set_area(const std::string& id) {
    if (!find_option(demo_areas(), id)) return false;
    area_ = id; touch(); return true;
}
bool PresentationModel::set_weapon(const std::string& id) {
    if (!find_option(demo_weapons(), id)) return false;
    weapon_ = id; touch(); return true;
}
bool PresentationModel::set_time(const std::string& id) {
    if (!find_option(demo_times(), id)) return false;
    time_ = id; touch(); return true;
}

// --- preview -----------------------------------------------------------------------
bool PresentationModel::can_open_preview() const {
    return screen_ == Screen::console && selected_expedition_row() != nullptr;
}
bool PresentationModel::open_preview() {
    if (!can_open_preview() || preview_open_) return false;
    preview_open_ = true;
    touch();
    return true;
}
void PresentationModel::close_preview() {
    if (!preview_open_) return;
    preview_open_ = false;
    touch();
}
PreviewSummary PresentationModel::preview_summary() const {
    PreviewSummary s;
    if (const auto* e = selected_expedition_row()) {
        s.expedition_label = expedition_label(*e);
        s.expedition_id = e->id;
        s.expedition_path = e->path;
    } else {
        s.expedition_label = "(no expedition selected)";
    }
    if (const auto* h = viewed_hunter_row()) {
        s.hunter_label = h->name + " [" + short_id(h->id) + "]" + (h->archived ? " (archived)" : "");
        s.hunter_id = h->id;
    } else {
        s.hunter_label = "(no hunter)";
    }
    s.area = option_label(demo_areas(), area_);
    s.weapon = option_label(demo_weapons(), weapon_);
    s.time_of_day = option_label(demo_times(), time_);
    s.data_source = source_.label();
    s.notes.push_back("Demo preview only: the loadout values are labeled samples, not catalog data.");
    s.notes.push_back("Nothing is validated by backend policy, prepared, launched or written.");
    s.notes.push_back(std::string("Evaluation settings: hunter name \"") + confirmed_.hunter_name + "\" (in-memory only).");
    return s;
}
const char* PresentationModel::launch_unavailable_reason() {
    return "Launch is outside this evaluation: no session is prepared and no engine is started.";
}

// --- setup ----------------------------------------------------------------------------
void PresentationModel::set_draft_name(std::string name) {
    draft_.values.hunter_name = std::move(name);
    draft_.name_message.clear();
    touch();
}
void PresentationModel::set_draft_folder(std::string folder) {
    if (draft_.values.content_folder == folder && draft_.folder_status != FolderStatus::unchecked) {
        return;
    }
    draft_.values.content_folder = std::move(folder);
    draft_.folder_status = FolderStatus::unchecked;
    draft_.folder_message.clear();
    draft_.folder_request = 0;
    confirm_after_check_ = false;
    touch();
}
RequestId PresentationModel::request_folder_check() {
    if (trimmed(draft_.values.content_folder).empty()) {
        draft_.folder_status = FolderStatus::unchecked;
        draft_.folder_message.clear();
        draft_.folder_request = 0;
        touch();
        return 0;
    }
    draft_.folder_request = next_request_++;
    draft_.folder_status = FolderStatus::checking;
    draft_.folder_message = "Checking folder...";
    touch();
    return draft_.folder_request;
}
bool PresentationModel::complete_folder_check(RequestId id, bool is_directory, std::string message) {
    if (screen_ != Screen::setup || id == 0 || id != draft_.folder_request ||
        draft_.folder_status != FolderStatus::checking)
        return false;
    draft_.folder_status = is_directory ? FolderStatus::ok : FolderStatus::error;
    draft_.folder_message = is_directory
        ? (message.empty() ? "Folder exists (not registered, imported or scanned)." : message)
        : (message.empty() ? "Not an existing folder." : message);
    if (is_directory && confirm_after_check_) {
        confirm_after_check_ = false;
        commit_draft();
    } else {
        confirm_after_check_ = false;
        touch();
    }
    return true;
}
ConfirmResult PresentationModel::confirm_setup() {
    if (screen_ != Screen::setup) return ConfirmResult::invalid;
    bool valid = true;
    if (trimmed(draft_.values.hunter_name).empty()) {
        draft_.name_message = "Hunter name must not be blank.";
        valid = false;
    } else {
        draft_.name_message.clear();
    }
    const bool folder_given = !trimmed(draft_.values.content_folder).empty();
    if (valid && folder_given) {
        if (draft_.folder_status == FolderStatus::ok) {
            commit_draft();
            return ConfirmResult::committed;
        }
        if (draft_.folder_status == FolderStatus::error) {
            touch();
            return ConfirmResult::invalid;
        }
        confirm_after_check_ = true;
        if (draft_.folder_status != FolderStatus::checking) request_folder_check();
        touch();
        return ConfirmResult::checking_folder;
    }
    if (!valid) {
        touch();
        return ConfirmResult::invalid;
    }
    commit_draft();
    return ConfirmResult::committed;
}
void PresentationModel::commit_draft() {
    confirmed_.hunter_name = trimmed(draft_.values.hunter_name);
    confirmed_.content_folder = trimmed(draft_.values.content_folder);
    draft_ = SetupDraft{};
    confirm_after_check_ = false;
    screen_ = Screen::lodge;
    touch();
}
void PresentationModel::cancel_setup() {
    if (screen_ != Screen::setup) return;
    draft_ = SetupDraft{};
    confirm_after_check_ = false;
    screen_ = Screen::lodge;
    touch();
}
} // namespace c2::frontend::gui
