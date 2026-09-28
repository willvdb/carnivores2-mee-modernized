// Headless presentation-model tests. No filesystem, threads, SDL or RmlUi.
#include "c2/frontend/gui/presentation.hpp"
#include "check.hpp"

using namespace c2::frontend::gui;

namespace {
SnapshotResult fixture_snapshot() {
    StoreSnapshot s;
    s.directory = "/fixture";
    s.schema_version = 1;
    s.active_hunter = "h-1";
    s.hunters = {{"h-1", "Anne Trapper", false}, {"h-2", "Anne Trapper", false}, {"h-3", "Retired", true}};
    s.expeditions = {{"e-a", "registered", "posix", "/opt/games/mee"},
                     {"e-b", "registered", "nt", "C:\\Games\\Classic\\"},
                     {"e-c", "registered", "posix", "/"}};
    SnapshotResult r;
    r.snapshot = std::move(s);
    return r;
}
PresentationModel loaded_model() {
    PresentationModel m(DataSource{DataSource::Kind::demo, "/fixture"});
    const auto id = m.begin_load();
    CHECK(m.complete_load(id, fixture_snapshot()));
    CHECK(m.load_state() == LoadState::ready);
    return m;
}

void routes_and_back() {
    auto m = loaded_model();
    CHECK(m.screen() == Screen::lodge);
    CHECK(!m.back());                      // nothing to dismiss on the lodge
    CHECK(!m.lodge_return_focus());
    CHECK(m.navigate(LodgeDestination::expeditions));
    CHECK(m.screen() == Screen::console);
    CHECK(!m.navigate(LodgeDestination::profile));   // only from the lodge
    CHECK(m.back());
    CHECK(m.screen() == Screen::lodge);
    CHECK(m.lodge_return_focus() == LodgeDestination::expeditions);
    CHECK(m.navigate(LodgeDestination::profile));
    CHECK(m.screen() == Screen::setup);
    CHECK(m.back());                       // back from setup is cancel
    CHECK(m.screen() == Screen::lodge);
    CHECK(m.lodge_return_focus() == LodgeDestination::profile);
    CHECK(!m.exit_requested());
    CHECK(m.navigate(LodgeDestination::exit));
    CHECK(m.exit_requested());
    CHECK(m.screen() == Screen::lodge);
}

void stable_selection_ids_and_duplicate_names() {
    auto m = loaded_model();
    CHECK(m.viewed_hunter() == std::string("h-1"));   // defaults to the active hunter
    CHECK(m.view_hunter("h-2"));                        // same display name, distinct identity
    CHECK(m.viewed_hunter() == std::string("h-2"));
    CHECK(m.viewed_hunter_row() && m.viewed_hunter_row()->name == "Anne Trapper");
    CHECK(!m.view_hunter("Anne Trapper"));              // names are not identities
    CHECK(!m.view_hunter("h-missing"));
    CHECK(m.viewed_hunter() == std::string("h-2"));
    CHECK(m.view_hunter("h-3") && m.viewed_hunter_row()->archived);

    CHECK(!m.selected_expedition());
    CHECK(!m.select_expedition("/opt/games/mee"));      // paths are locators, not identities
    CHECK(m.select_expedition("e-b"));
    CHECK(m.selected_expedition_row() && m.selected_expedition_row()->path_flavor == "nt");
    CHECK(expedition_label(*m.selected_expedition_row()) == "Classic");
    CHECK(m.select_expedition("e-c"));
    CHECK(expedition_label(*m.selected_expedition_row()) == "/");   // no component: full path
    m.clear_expedition();
    CHECK(!m.selected_expedition());
    CHECK(!m.selected_expedition_row());
}

void preview_and_launch() {
    auto m = loaded_model();
    CHECK(!m.can_open_preview());          // wrong screen
    CHECK(m.navigate(LodgeDestination::expeditions));
    CHECK(!m.can_open_preview());          // empty selection
    CHECK(!m.open_preview());
    CHECK(m.select_expedition("e-a"));
    CHECK(m.can_open_preview());
    CHECK(m.set_area("demo-area-2") && m.set_weapon("demo-weapon-2") && m.set_time("demo-time-2"));
    CHECK(!m.set_area("not-an-option"));
    CHECK(m.area() == "demo-area-2");
    CHECK(m.open_preview());
    CHECK(m.preview_open());
    const auto s = m.preview_summary();
    CHECK(s.expedition_id == "e-a" && s.expedition_label == "mee");
    CHECK(s.hunter_id == "h-1");
    CHECK(s.area.find("Demo area 2") == 0);
    CHECK(!s.notes.empty());
    CHECK(!m.navigate(LodgeDestination::expeditions));   // modal blocks navigation
    CHECK(m.back());                       // dismisses the dialog first...
    CHECK(!m.preview_open());
    CHECK(m.screen() == Screen::console);  // ...and stays on the console
    CHECK(m.selected_expedition() == std::string("e-a"));   // dismissal changed nothing
    CHECK(m.back());
    CHECK(m.screen() == Screen::lodge);
    CHECK(std::string(PresentationModel::launch_unavailable_reason()).find("outside this evaluation") != std::string::npos);
}

void loading_states_and_stale_completion() {
    PresentationModel m(DataSource{DataSource::Kind::supplied, "/nowhere"});
    CHECK(m.load_state() == LoadState::idle);
    CHECK(!m.snapshot());
    const auto first = m.begin_load();
    CHECK(m.load_state() == LoadState::loading);
    const auto second = m.begin_load();      // newer request supersedes
    CHECK(second != first);
    CHECK(!m.complete_load(first, fixture_snapshot()));   // stale: ignored
    CHECK(m.load_state() == LoadState::loading);
    CHECK(!m.snapshot());
    SnapshotResult failed;
    failed.error = "store cannot be read: missing manifest";
    CHECK(m.complete_load(second, failed));
    CHECK(m.load_state() == LoadState::error);
    CHECK(m.load_error() == failed.error);
    CHECK(!m.snapshot());
    CHECK(!m.complete_load(second, fixture_snapshot()));  // already completed: ignored
    CHECK(m.load_state() == LoadState::error);

    // Success after error, and an empty store is a distinct honest state.
    const auto third = m.begin_load();
    SnapshotResult empty;
    empty.snapshot = StoreSnapshot{};
    CHECK(m.complete_load(third, empty));
    CHECK(m.load_state() == LoadState::ready);
    CHECK(m.snapshot() && m.snapshot()->hunters.empty() && m.snapshot()->expeditions.empty());
    CHECK(!m.viewed_hunter());
    CHECK(!m.view_hunter("h-1"));

    // Selections that the fresh observation lacks are dropped, not kept stale.
    auto n = loaded_model();
    CHECK(n.select_expedition("e-a") && n.view_hunter("h-2"));
    const auto reload = n.begin_load();
    CHECK(n.complete_load(reload, empty));
    CHECK(!n.selected_expedition());
    CHECK(!n.viewed_hunter());
    // A failed reload never leaves a convincing stale snapshot behind.
    const auto again = n.begin_load();
    CHECK(n.complete_load(again, failed));
    CHECK(!n.snapshot() && n.load_state() == LoadState::error);
}

void setup_confirm_and_cancel() {
    auto m = loaded_model();
    const auto before = m.confirmed_settings();
    CHECK(before.hunter_name == "Evaluation Hunter" && before.content_folder.empty());

    // Cancel discards every edit.
    CHECK(m.navigate(LodgeDestination::profile));
    CHECK(m.draft().values.hunter_name == before.hunter_name);
    m.set_draft_name("Edited Name");
    m.set_draft_folder("/some/folder");
    m.cancel_setup();
    CHECK(m.screen() == Screen::lodge);
    CHECK(m.confirmed_settings().hunter_name == before.hunter_name);
    CHECK(m.confirmed_settings().content_folder.empty());
    CHECK(m.navigate(LodgeDestination::profile));
    CHECK(m.draft().values.hunter_name == before.hunter_name);   // draft restarts from confirmed
    CHECK(m.draft().values.content_folder.empty());

    // Blank name is invalid and confirms nothing.
    m.set_draft_name("   ");
    CHECK(m.confirm_setup() == ConfirmResult::invalid);
    CHECK(!m.draft().name_message.empty());
    CHECK(m.screen() == Screen::setup);
    CHECK(m.confirmed_settings().hunter_name == before.hunter_name);

    // Name only: committed immediately, trimmed, returns to the lodge.
    m.set_draft_name("  Ranger Björn  ");
    CHECK(m.confirm_setup() == ConfirmResult::committed);
    CHECK(m.screen() == Screen::lodge);
    CHECK(m.confirmed_settings().hunter_name == "Ranger Björn");
    CHECK(m.preview_summary().notes.back().find("Ranger Björn") != std::string::npos);

    // Folder given: confirm waits for the asynchronous check, then commits.
    CHECK(m.navigate(LodgeDestination::profile));
    m.set_draft_folder("/content/root");
    CHECK(m.draft().folder_status == FolderStatus::unchecked);
    CHECK(m.confirm_setup() == ConfirmResult::checking_folder);
    CHECK(m.draft().folder_status == FolderStatus::checking);
    const auto request = m.draft().folder_request;
    CHECK(request != 0);
    CHECK(!m.complete_folder_check(request + 100, true, ""));   // stale/unknown: ignored
    CHECK(m.screen() == Screen::setup);
    CHECK(m.complete_folder_check(request, true, ""));
    CHECK(m.screen() == Screen::lodge);
    CHECK(m.confirmed_settings().content_folder == "/content/root");
    CHECK(m.confirmed_settings().hunter_name == "Ranger Björn");

    // Folder check failure keeps the screen and the previous confirmed values.
    CHECK(m.navigate(LodgeDestination::profile));
    m.set_draft_folder("/missing");
    CHECK(m.confirm_setup() == ConfirmResult::checking_folder);
    CHECK(m.complete_folder_check(m.draft().folder_request, false, "Not an existing folder."));
    CHECK(m.screen() == Screen::setup);
    CHECK(m.draft().folder_status == FolderStatus::error);
    CHECK(m.confirm_setup() == ConfirmResult::invalid);
    CHECK(m.confirmed_settings().content_folder == "/content/root");
    // Editing the path resets the check; a completion for the old request is ignored.
    const auto old = m.draft().folder_request;
    m.set_draft_folder("/other");
    CHECK(m.draft().folder_status == FolderStatus::unchecked);
    CHECK(!m.complete_folder_check(old, true, ""));
    const auto fresh = m.request_folder_check();
    CHECK(fresh != 0 && fresh != old);
    // Cancel while checking: the late completion must not commit anything.
    m.cancel_setup();
    CHECK(!m.complete_folder_check(fresh, true, ""));
    CHECK(m.screen() == Screen::lodge);
    CHECK(m.confirmed_settings().content_folder == "/content/root");
    // Clearing the folder is allowed and needs no check.
    CHECK(m.navigate(LodgeDestination::profile));
    m.set_draft_folder("");
    CHECK(m.request_folder_check() == 0);
    CHECK(m.confirm_setup() == ConfirmResult::committed);
    CHECK(m.confirmed_settings().content_folder.empty());
}

void version_tracks_mutation() {
    auto m = loaded_model();
    const auto v = m.version();
    CHECK(!m.view_hunter("nope"));
    CHECK(m.version() == v);               // rejected calls change nothing
    CHECK(m.navigate(LodgeDestination::expeditions));
    CHECK(m.version() > v);
}

void data_source_labels() {
    const DataSource demo{DataSource::Kind::demo, "/tmp/x"};
    const DataSource supplied{DataSource::Kind::supplied, "/lodge"};
    CHECK(demo.label().find("Demo data") == 0);
    CHECK(demo.label().find("not your lodge") != std::string::npos);
    CHECK(supplied.label().find("Read-only store: /lodge") == 0);
}
} // namespace

int main() {
    routes_and_back();
    stable_selection_ids_and_duplicate_names();
    preview_and_launch();
    loading_states_and_stale_completion();
    setup_confirm_and_cancel();
    version_tracks_mutation();
    data_source_labels();
    if (test::failures) {
        std::cerr << test::failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "gui model tests passed\n";
    return 0;
}
