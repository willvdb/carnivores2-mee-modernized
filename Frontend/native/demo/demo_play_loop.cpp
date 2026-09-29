#include "c2/frontend/demo_play_loop.hpp"
#include "c2/frontend/core.hpp"
#include "play_loop_internal.hpp"
#include "planning_internal.hpp"
#include "store_ops.hpp"
#include "store_write.hpp"
#include <fstream>
#include <iterator>
#include <set>
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
        write(game/"HUNTDAT/_MENU.TXT", "weapons {\n{\n name = 'Synthetic weapon'\n}\n{\n name = 'Unaffordable fixture weapon'\n}\n{\n name = 'Second fixture weapon'\n}\n}\ncharacters {\n{\n name = 'Synthetic group'\n ai = 10\n}\n{\n name = 'Second fixture license'\n ai = 11\n}\n}\nprices {\n area = 5\n dino = 10\n dino = 5\n weapon = 20\n weapon = 200\n weapon = 10\n acces = 5\n acces = 10\n}\n");
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
        auto fixture_policy=[](const compat::Value&,const catalog::Projection& catalog,const compat::Value& slot,
                         const compat::Value& selection,const compat::Value& score, bool partial) {
            // Authored demo-only eligibility, never installed on a supplied store.
            if(selection.at(U"area").string!=U"areas:0") throw StoreError("unknown fixture area");
            int cost=5; unsigned din=0,wep=0;
            auto argv=array_value();
            for(const auto& pair:std::vector<std::pair<std::u32string,catalog::Group>>{
                {U"licenses",catalog::Group::licenses},{U"weapons",catalog::Group::weapons},{U"equipment",catalog::Group::equipment}}) {
                const auto& ids=selection.at(pair.first).array;
                if(!partial && pair.first!=U"equipment" && ids.empty()) throw StoreError("fixture requires a license and weapon");
                std::set<std::u32string> seen;
                for(const auto& id:ids) {
                    if(!seen.insert(id.string).second) throw StoreError("duplicate fixture ID");
                    bool found=false; unsigned ordinal=0;
                    for(const auto& entry:catalog.entries(pair.second)) {
                        if(entry.id()==id.string) {
                            found=true; cost+=std::stoi(entry.price()->decimal);
                            if(pair.first==U"licenses") din|=1u<<ordinal;
                            if(pair.first==U"weapons") wep|=1u<<ordinal;
                        }
                        ++ordinal;
                    }
                    if(!found) throw StoreError("unknown fixture selection");
                }
            }
            if(score.kind!=compat::Kind::integer || compare_decimal(score.integer,std::to_string(cost))<0)
                throw StoreError("selection exceeds the native score requirement");
            for (const auto& a : {"reg="+slot.integer,std::string("prj=huntdat/areas/area1"),"din="+std::to_string(din),
                 "wep="+std::to_string(wep),"dtm="+selection.at(U"time_of_day").integer,
                 std::string("smod=0.85,0.70,0.80,1.0,1.25,1.0")}) argv.array.push_back(ascii_value(a));
            for(const auto& id:selection.at(U"equipment").array)
                argv.array.push_back(ascii_value(id.string==U"equipment:0" ? "-camo" : "-radar"));
            auto result=object_value();
            result.object={{U"adapter",string_value(std::u32string(planning::EXPANDED_HUNT_POLICY_ID))},
                {U"fixture_only",ascii_value("authored disposable state, NOT Genesis")},
                {U"selection",selection},{U"candidate_argv",argv},{U"score_requirement",integer_value(std::to_string(cost))}};
            return result;
        };
        policies.hunt=[fixture_policy](const auto& r,const auto& c,const auto& sl,const auto& se,const auto& sc) {
            return fixture_policy(r,c,sl,se,sc,false);
        };
        policies.hunt_advice=[fixture_policy](const auto& r,const auto& c,const auto& sl,const auto& se,const auto& sc) {
            return fixture_policy(r,c,sl,se,sc,true);
        };
        Access::policies(client,std::move(policies));
        return {root,store.directory(),std::move(client),{engine,std::u32string(hash.begin(),hash.end()),true}};
    } catch (...) { fs::remove_all(root); throw; }
}
}
