#include "screens.hpp"
#include <RmlUi/Core/Elements/ElementFormControlSelect.h>
#include <RmlUi/Core/StringUtilities.h>
namespace c2::frontend::gui::app {
namespace {
const std::vector<std::pair<const char*,Operation>> operations{
    {"catalog",Operation::catalog},{"plan",Operation::plan},{"prepare",Operation::prepare},
    {"run",Operation::run},{"inspect",Operation::inspect},{"preview",Operation::preview},
    {"accept",Operation::accept},{"recover",Operation::recover},
    {"recover_acceptance",Operation::recover_acceptance},{"upgrade",Operation::upgrade}};
}
void Screens::bind_loop() {
    auto ctor=context_.GetDataModel("console");
    ctor.BindFunc("source",[this](Rml::Variant& v) {
        v=model_.source().kind==DataSource::Kind::demo
            ? std::string("Asset-free native I/O fixture: +7 score is authored test data. This is not a hunt, trophy or gameplay-success claim.")
            : model_.source().label();
    });
    ctor.BindFunc("busy",[this](Rml::Variant& v){v=model_.loop.busy();});
    ctor.BindFunc("schema_one",[this](Rml::Variant& v){v=model_.snapshot() && model_.snapshot()->schema_version==1;});
    ctor.BindFunc("loop_details",[this](Rml::Variant& v){v=model_.loop.details();});
    ctor.BindFunc("loop_error",[this](Rml::Variant& v){v=model_.loop.error();});
    ctor.BindFunc("loop_status",[this](Rml::Variant& v){
        static const char* names[]={"Select association and loadout","Validating intent","Validated intent only",
            "Preparing isolated session","Prepared","Running — logs remain in workspace","Returned — inspect candidate",
            "Inspecting","Review observations","Revalidating acceptance preview","Eligible for explicit acceptance",
            "Acceptance blocked — evidence retained","Accepting reviewed candidate","Accepted — prepare a fresh session",
            "Recovery / metadata operation","Declined — evidence retained","Operation blocked or failed — inspect diagnostics"};
        v=std::string(names[static_cast<unsigned>(model_.loop.state())]);
    });
    ctor.BindFunc("pins",[this](Rml::Variant& v){
        std::string out;
        if(const auto& a=model_.loop.association()) out="Association: "+to_utf8(a->id)+"\nHunter: "+to_utf8(a->hunter_id)+
            "\nExpedition: "+to_utf8(a->instance_id)+"\nOrigin: "+to_utf8(a->origin)+"; ownership: "+to_utf8(a->ownership)+
            "; authority: "+to_utf8(a->authority)+"\nCurrent generation: "+to_utf8(a->current_generation.value_or(U"unavailable — explicit upgrade may be required"));
        if(const auto& s=model_.loop.session()) out+="\nSession: "+to_utf8(s->id)+"\nSession association: "+to_utf8(s->association_id)+"\nState: "+to_utf8(s->state)+
            "\nPinned generation: "+to_utf8(s->generation.value_or(U"unavailable"))+
            "\nChanged members: "+(s->changed_members ? std::to_string(s->changed_members->size())+" observed" : "unavailable")+
            "\nLogs and evidence: "+model_.source().directory+"/sessions/"+to_utf8(s->id);
        if(const auto& s=model_.loop.session()) {
            auto observations=[&](const char* stage,const auto& members) {
                out+="\n"+std::string(stage)+": "+(members ? "observed" : "unavailable");
                if(members) for(const auto& m:*members) {
                    out+="\n  "+to_utf8(m.member);
                    if(m.score) out+=" — raw score "+m.score->decimal;
                    if(m.rank) out+="; raw rank "+m.rank->decimal;
                }
            };
            observations("Before",s->before); observations("Returned",s->after);
            for(const auto& d:s->diagnostics) out+="\n"+to_utf8(d.code)+": "+to_utf8(d.message);
        }
        if(const auto& p=model_.loop.preview()) for(const auto& d:p->diagnostics) out+="\n"+to_utf8(d.code)+": "+to_utf8(d.message);
        if(const auto& p=model_.loop.preview()) out+="\nPreview: "+to_utf8(p->status)+"\nExpected predecessor: "+
            to_utf8(p->expected_generation)+"\nCandidate digest: "+to_utf8(p->candidate_sha256);
        v=out;
    });
    ctor.BindFunc("association",[this](Rml::Variant& v){v=loop_association_;},[this](const Rml::Variant& v){
        const auto id=v.Get<Rml::String>();
        if(!model_.snapshot() || model_.loop.busy() || id==loop_association_) return;
        for(const auto& a:model_.snapshot()->associations) if(to_utf8(a.id)==id && model_.loop.select(a)) {
            loop_association_=id; catalog_association_.clear();
            model_.view_hunter(to_utf8(a.hunter_id)); model_.select_expedition(to_utf8(a.instance_id));
            if(callbacks_.loop_operation) callbacks_.loop_operation(Operation::catalog);
            break;
        }
    });
    auto field=[&](const char* name,std::string& target){
        auto* ptr=&target;
        ctor.BindFunc(name,[ptr](Rml::Variant& v){v=*ptr;},[this,ptr](const Rml::Variant& v){
            if(model_.loop.busy()) return;
            *ptr=v.Get<Rml::String>();
            model_.loop.loadout(planning::Selection::hunt(to_utf32(loop_area_),{to_utf32(loop_license_)},
                {to_utf32(loop_weapon_)},{loop_time_}));
        });
    };
    field("loop_area",loop_area_); field("loop_license",loop_license_); field("loop_weapon",loop_weapon_); field("loop_time",loop_time_);
    ctor.BindFunc("recovery_id",[this](Rml::Variant& v){v=recovery_id_;},[this](const Rml::Variant& v){recovery_id_=v.Get<Rml::String>();});
    for(const auto& pair:operations) {
        auto op=pair.second;
        ctor.BindFunc(std::string("can_")+pair.first,[this,op](Rml::Variant& v){v=model_.loop.can(op);});
    }
    ctor.BindEventCallback("operate",[this](Rml::DataModelHandle,Rml::Event&,const Rml::VariantList& args){
        if(args.empty()) return;
        const auto action=args[0].Get<Rml::String>();
        if(action=="cancel") {model_.loop.cancel(); return;}
        if(action=="review") {review_open_=true; return;}
        if(action=="close") {review_open_=false; return;}
        if(action=="decline") {model_.loop.decline(); review_open_=false; return;}
        if(action=="load_session") {
            if(model_.loop.inspect_session(to_utf32(recovery_id_))) {
                loop_association_.clear(); catalog_association_.clear();
                loop_area_.clear(); loop_license_.clear(); loop_weapon_.clear();
                if(callbacks_.loop_operation) callbacks_.loop_operation(Operation::inspect);
            }
            review_open_=true; return;
        }
        for(const auto& pair:operations) if(action==pair.first && callbacks_.loop_operation) callbacks_.loop_operation(pair.second);
    });
    loop_model_=ctor.GetModelHandle();
}
void Screens::sync_loop() {
    auto* select=rmlui_dynamic_cast<Rml::ElementFormControlSelect*>(console_->GetElementById("association-select"));
    std::vector<std::string> ids;
    if(model_.snapshot()) for(const auto& a:model_.snapshot()->associations) ids.push_back(to_utf8(a.id));
    if(select && ids!=association_ids_) {
        association_ids_=ids; select->RemoveAll(); select->Add("Select association","");
        if(model_.snapshot()) for(const auto& a:model_.snapshot()->associations) {
            std::string hunter=to_utf8(a.hunter_id), expedition=to_utf8(a.instance_id);
            for(const auto& h:model_.snapshot()->hunters) if(h.id==hunter) {hunter=h.name; break;}
            for(const auto& e:model_.snapshot()->expeditions) if(e.id==expedition) {expedition=expedition_label(e); break;}
            select->Add(Rml::StringUtilities::EncodeRml(hunter+" / "+expedition+" ["+to_utf8(a.id)+"]"),to_utf8(a.id));
        }
    }
    if(model_.loop.catalog() && catalog_association_!=loop_association_) {
        catalog_association_=loop_association_;
        auto fill=[&](const char* id,catalog::Group group,std::string& selected){
            auto* control=rmlui_dynamic_cast<Rml::ElementFormControlSelect*>(console_->GetElementById(id));
            if(!control) return;
            control->RemoveAll(); selected.clear();
            for(const auto& entry:model_.loop.catalog()->entries(group)) {
                auto eid=to_utf8(entry.id()); std::string label=eid;
                if(auto l=entry.label()) {
                    if(auto text=std::get_if<std::u32string>(&*l)) label=to_utf8(*text)+" ["+eid+"]";
                    else label=std::get<catalog::Integer>(*l).decimal+" ["+eid+"]";
                }
                control->Add(Rml::StringUtilities::EncodeRml(label),eid);
                if(selected.empty()) selected=eid;
            }
        };
        fill("loop-area",catalog::Group::areas,loop_area_);
        fill("loop-license",catalog::Group::licenses,loop_license_);
        fill("loop-weapon",catalog::Group::weapons,loop_weapon_);
        model_.loop.loadout(planning::Selection::hunt(to_utf32(loop_area_),{to_utf32(loop_license_)},{to_utf32(loop_weapon_)},{loop_time_}));
    }
    for(const char* name:{"source","busy","schema_one","loop_details","loop_error","loop_status","pins",
                          "association","loop_area","loop_license","loop_weapon","loop_time"}) loop_model_.DirtyVariable(name);
    for(const auto& pair:operations) loop_model_.DirtyVariable(std::string("can_")+pair.first);
    if(review_open_ && !review_shown_) {
        loop_return_focus_=focused_id(); review_shown_=true;
        review_->Show(Rml::ModalFlag::Modal,Rml::FocusFlag::Document); focus_by_id(review_,"review-close");
    } else if(!review_open_ && review_shown_) {
        review_shown_=false; review_->Hide();
        if(!focus_by_id(console_,loop_return_focus_)) focus_console_default();
    }
}
}
