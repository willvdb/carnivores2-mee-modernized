#include "c2/frontend/demo_play_loop.hpp"
#include "c2/frontend/gui/loop.hpp"
#include "check.hpp"
#include <fstream>
#include <map>
#include <thread>
#include <chrono>
#include "c2/frontend/gui/worker.hpp"
using namespace c2::frontend;
using namespace c2::frontend::gui;
std::map<std::string,std::string> bytes(const std::filesystem::path& root) {
    std::map<std::string,std::string> out;
    for (const auto& e : std::filesystem::recursive_directory_iterator(root)) if(e.is_regular_file()) {
        std::ifstream f(e.path(),std::ios::binary);
        out[e.path().lexically_relative(root).string()]={std::istreambuf_iterator<char>(f),{}};
    }
    return out;
}
int main(int argc, char** argv) {
    if(argc!=3) return 2;
    auto demo=play_loop::create_demo(argv[1],argv[2]);
    struct Cleanup { std::filesystem::path path; ~Cleanup(){std::filesystem::remove_all(path);} } cleanup{demo.owned_root};
    try {
        auto a=play_loop::associations(Store(demo.directory).read()).front();
        auto selection=planning::Selection::hunt(U"areas:0",{U"licenses:0"},{U"weapons:0"},{"1"});
        auto protected_bytes=bytes(demo.owned_root/"authored-content");
        demo.client.plan(a.id,selection);
        auto s1=demo.client.prepare(a.id,selection,demo.authorization);
        auto stale=demo.client.prepare(a.id,selection,demo.authorization);
        std::atomic<bool> cancel{false};
        auto returned=demo.client.run(s1.id,demo.authorization,cancel);
        CHECK(returned.state==U"returned");
        auto candidate=demo.client.reconcile(s1.id); CHECK(candidate.state==U"candidate");
        CHECK(candidate.before.has_value()); CHECK(candidate.after.has_value());
        CHECK(candidate.before->size()==2); CHECK(candidate.after->size()==2);
        CHECK(candidate.changed_members.has_value());
        CHECK(candidate.changed_members->size()==1);
        CHECK(demo.client.inspect(s1.id).state==U"candidate");
        auto preview=demo.client.preview(s1.id,*s1.generation); CHECK(preview.allowed);
        CHECK(!preview.candidate_sha256.empty());
        auto receipt=demo.client.accept(preview); CHECK(receipt.result==U"accepted");
        auto s2=demo.client.prepare(a.id,selection,demo.authorization);
        CHECK(s2.id!=s1.id); CHECK(s2.generation==receipt.current_generation);
        auto failed=demo.client.run(stale.id,demo.authorization,cancel); CHECK(failed.state==U"failed");
        CHECK(!demo.client.preview(stale.id,*stale.generation).allowed);
        // A cancelled owned child remains reviewable, never accepted automatically.
        cancel=true;
        try { demo.client.run(s2.id,demo.authorization,cancel); CHECK(false); }
        catch (const std::exception&) {}
        CHECK(demo.client.inspect(s2.id).state==U"prepared");
        cancel=false;
        std::thread canceller([&] {
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            while(std::chrono::steady_clock::now()<deadline) {
                if(demo.client.inspect(s2.id).state==U"running") {cancel=true; return;}
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            cancel=true;
        });
        auto cancelled=demo.client.run(s2.id,demo.authorization,cancel);
        canceller.join();
        CHECK(cancelled.state==U"returned");
        auto quarantine=demo.client.reconcile(s2.id); CHECK(quarantine.state==U"quarantined");
        CHECK(demo.client.inspect(s2.id).state==U"quarantined");
        CHECK(!demo.client.preview(s2.id,*s2.generation).allowed);
        // Joined shutdown during a real core run, with the completion discarded.
        auto s3=demo.client.prepare(a.id,selection,demo.authorization);
        Worker worker; std::atomic<bool> stop{false}, started{false};
        bool completion=false;
        worker.submit([&] { started=true; try { demo.client.run(s3.id,demo.authorization,stop); } catch(const std::exception&) {} },
                      [&] { completion=true; });
        while(!started) std::this_thread::yield();
        auto shutdown_start=std::chrono::steady_clock::now(); stop=true; worker.shutdown();
        CHECK(std::chrono::steady_clock::now()-shutdown_start<std::chrono::seconds(10));
        CHECK(!completion); CHECK(worker.pending_completions()==0);
        auto before=bytes(demo.directory);
        play_loop::Client ro(Store(demo.directory),std::filesystem::path(argv[1]));
        ro.inspect(s1.id); ro.preview(s1.id,*s1.generation);
        try { ro.accept(preview); CHECK(false); } catch(const StoreError&) {}
        CHECK(bytes(demo.directory)==before);
        CHECK(bytes(demo.owned_root/"authored-content")==protected_bytes);
        // Digest-less allowed values still cannot cross the public boundary.
        preview.candidate_sha256.clear();
        try { demo.client.accept(preview); CHECK(false); } catch(const StoreError&) {}
        CHECK(demo.client.recover_acceptance(s2.id).result==U"not-committed");
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
    return test::failures ? 1 : 0;
}
