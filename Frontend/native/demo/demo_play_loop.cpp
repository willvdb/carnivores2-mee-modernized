#include "c2/frontend/demo_play_loop.hpp"
#include "c2/frontend/core.hpp"
#include "play_loop_internal.hpp"
#include "planning_internal.hpp"
#include "store_ops.hpp"
#include "store_write.hpp"
#include <fstream>
#include <iterator>
namespace c2::frontend::play_loop {
namespace {
void write(const std::filesystem::path& path, const std::string& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path,std::ios::binary); out.write(bytes.data(),bytes.size()); out.close();
    if (!out) throw StoreError("cannot write owned demo fixture");
}
void word(std::string& s, std::size_t offset, unsigned v) {
    for (unsigned i=0;i<4;++i) s[offset+i]=static_cast<char>((v>>(8*i))&255);
}
}
Demo create_demo(const std::filesystem::path& probe, const std::filesystem::path& fixture) {
    namespace fs = std::filesystem;
    using namespace planning_internal;
    const auto root = fs::temp_directory_path() / ("c2-frontend-gui-demo-" + store_write::new_id());
    if (!fs::create_directory(root)) throw StoreError("cannot create owned demo directory");
    try {
        const auto game = root / "authored-content";
        write(game/"HUNTDAT/_RES.TXT", "weapons {\n}\ncharacters {\n{\n name = 'Synthetic animal'\n ai = 10\n}\n}\n");
        write(game/"HUNTDAT/_MENU.TXT", "weapons {\n{\n name = 'Synthetic weapon'\n}\n}\ncharacters {\n{\n name = 'Synthetic group'\n ai = 10\n}\n}\nprices {\n area = 5\n dino = 10\n weapon = 20\n}\n");
        fs::create_directories(game/"HUNTDAT/MENU/TXT"); fs::create_directories(game/"HUNTDAT/MENU/PICS");
        write(game/"HUNTDAT/AREAS/AREA1.MAP","synthetic map evidence");
        write(game/"HUNTDAT/AREAS/AREA1.RSC","synthetic resource evidence");
        write(game/"CARN2.EXE","synthetic executable evidence - never run");
        std::string sav(1660,'\0'), sab(7176,'\0');
        for (std::size_t i=0;i<sav.size();++i) sav[i]=static_cast<char>((i*73+19)%256);
        sav.replace(0,12,"Test hunter\0",12); word(sav,128,0); word(sav,132,100); word(sav,136,1000);
        for (std::size_t i=0;i<sab.size();++i) sab[i]=static_cast<char>((i*37)%256);
        write(game/"trophy00.sav",sav); write(game/"trophy00.sab",sab);
        const auto second_game=root/"authored-second-expedition";
        const auto long_game=root/"An Extraordinarily Long Authored Expedition Name For Narrow Console Lists";
        fs::copy(game,second_game,fs::copy_options::recursive);
        fs::copy(game,long_game,fs::copy_options::recursive);
        // The executable lives outside content so it does not change content evidence.
        fs::create_directory(root/"engine");
        auto engine = root / "engine" / fixture.filename(); fs::copy_file(fixture,engine);
        fs::permissions(engine,fs::status(fixture).permissions());
        std::ifstream input(engine,std::ios::binary);
        std::string bytes{std::istreambuf_iterator<char>(input),{}};
        auto hash=sha256(bytes);
        Store store(root/"lodge"); std::u32string hunter, instance;
        store_write::transaction(store,[&](compat::Value& data) {
            hunter=store_ops::hunter(data,U"create",std::nullopt,U"Fixture Hunter").at(U"id").string;
            store_ops::hunter(data,U"create",std::nullopt,U"Fixture Hunter");
            store_ops::hunter(data,U"create",std::nullopt,U"Bj\u00f6rn \u00d8deg\u00e5rd");
            auto archived=store_ops::hunter(data,U"create",std::nullopt,U"Retired Hunter").at(U"id").string;
            store_ops::hunter(data,U"archive",archived,std::nullopt);
            store_ops::hunter(data,U"select",hunter,std::nullopt);
            for(const auto& extra:{second_game,long_game})
                store_ops::register_instance(data,extra,U"registered",U"mee-newer",std::nullopt,std::nullopt,std::nullopt);
            instance=store_ops::register_instance(data,game,U"registered",U"mee-newer",std::nullopt,std::nullopt,std::nullopt).at(U"id").string;
        });
        store_write::transaction(store,[&](compat::Value& data) {
            store_ops::associate(store,data,hunter,instance,U"trophy00",U"personal",U"managed",probe);
        });
        store_ops::upgrade_store(store);
        Client client(store,probe,true);
        session_policy::Policies policies;
        fs::create_directory(root/"queries");
        policies.query_parent=root/"queries";
        policies.hunt=[](const compat::Value&,const catalog::Projection&,const compat::Value& slot,
                         const compat::Value& selection,const compat::Value&) {
            auto argv=array_value();
            for (const auto& a : {"reg="+slot.integer,std::string("prj=huntdat/areas/area1"),std::string("din=1"),
                 std::string("wep=1"),"dtm="+selection.at(U"time_of_day").integer,
                 std::string("smod=0.85,0.70,0.80,1.0,1.25,1.0")}) argv.array.push_back(ascii_value(a));
            auto result=object_value();
            result.object={{U"adapter",string_value(std::u32string(planning::HUNT_POLICY_ID))},
                {U"fixture_only",ascii_value("authored disposable state, NOT Genesis")},
                {U"selection",selection},{U"candidate_argv",argv}};
            return result;
        };
        Access::policies(client,std::move(policies));
        return {root,store.directory(),std::move(client),{engine,std::u32string(hash.begin(),hash.end()),true}};
    } catch (...) { fs::remove_all(root); throw; }
}
}
