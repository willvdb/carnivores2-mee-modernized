// Asset-free protocol server: tests the production reader over a real, private
// Wayland connection without a host compositor, GPU, profile or SDL private ABI.
#include "../Hunt/Platform/DisplayIdentityLinuxNative.h"
#include "../Hunt/Platform/DisplayIdentityLinux.h"
#include "wlr-output-management-server.h"
#include "xdg-output-server.h"
#include <wayland-server.h>
#include <wayland-client.h>
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <sys/socket.h>
#include <unistd.h>

namespace {
struct Server {
    wl_display* server = nullptr;
    wl_display* client = nullptr;
    wl_registry* registry = nullptr;
    wl_output* output = nullptr;
    std::thread worker;
    std::atomic<bool> running{false}, paused{false};
    std::atomic<int> objects{0}, writes{0};
    std::string serial = "SN42";
    bool duplicate = false, pauseAfterManager = false, omitDone = false;
    int version = 3;
    struct NativeOutput { Server* server; int index; } native[2]{{this,0},{this,1}};
    static void Destroy(wl_client*, wl_resource* resource) { wl_resource_destroy(resource); }
    static void Gone(wl_resource* resource) { static_cast<Server*>(wl_resource_get_user_data(resource))->objects--; }
    wl_resource* Resource(wl_client* peer, const wl_interface* interface, int resourceVersion, std::uint32_t id, const void* impl) {
        auto* resource=wl_resource_create(peer,interface,resourceVersion,id);
        objects++; wl_resource_set_implementation(resource,impl,this,Gone); return resource;
    }
    static void BindOutput(wl_client* peer, void* data, std::uint32_t version, std::uint32_t id) {
        auto& native=*static_cast<NativeOutput*>(data);
        static const struct wl_output_interface impl={Destroy};
        auto* resource=wl_resource_create(peer,&wl_output_interface,static_cast<int>(version),id);
        wl_resource_set_implementation(resource,&impl,&native,nullptr);
        wl_output_send_geometry(resource,native.index*800,0,300,200,0,"Maker","Model",0);
        wl_output_send_mode(resource,WL_OUTPUT_MODE_CURRENT|WL_OUTPUT_MODE_PREFERRED,800,600,60000);
        if(version>=2) { wl_output_send_scale(resource,1); wl_output_send_done(resource); }
    }
    static void BindXdg(wl_client* peer, void* data, std::uint32_t version, std::uint32_t id) {
        auto& self=*static_cast<Server*>(data);
        static const struct zxdg_output_manager_v1_interface impl={Destroy,
            [](wl_client* peer,wl_resource* manager,std::uint32_t id,wl_resource* output) {
                auto& self=*static_cast<Server*>(wl_resource_get_user_data(manager));
                auto& native=*static_cast<NativeOutput*>(wl_resource_get_user_data(output));
                static const struct zxdg_output_v1_interface impl={Destroy};
                auto* resource=self.Resource(peer,&zxdg_output_v1_interface,2,id,&impl);
                zxdg_output_v1_send_logical_position(resource,native.index*800,0);
                zxdg_output_v1_send_logical_size(resource,800,600);
                zxdg_output_v1_send_name(resource,native.index ? "DP-2" : "DP-1");
                zxdg_output_v1_send_done(resource);
            }};
        self.Resource(peer,&zxdg_output_manager_v1_interface,static_cast<int>(version),id,&impl);
    }
    static void BindManager(wl_client* peer, void* data, std::uint32_t version, std::uint32_t id) {
        auto& self=*static_cast<Server*>(data);
        static const struct zwlr_output_manager_v1_interface impl={
            [](wl_client*,wl_resource* resource,std::uint32_t,std::uint32_t) {
                static_cast<Server*>(wl_resource_get_user_data(resource))->writes++;
                wl_resource_post_error(resource,0,"Reader attempted display configuration");
            },
            [](wl_client*,wl_resource* resource) { zwlr_output_manager_v1_send_finished(resource); wl_resource_destroy(resource); }};
        auto* manager=self.Resource(peer,&zwlr_output_manager_v1_interface,static_cast<int>(version),id,&impl);
        static const struct zwlr_output_head_v1_interface headImpl={Destroy};
        for(int i=0;i<2;++i) {
            auto* head=self.Resource(peer,&zwlr_output_head_v1_interface,static_cast<int>(version),0,&headImpl);
            zwlr_output_manager_v1_send_head(manager,head);
            zwlr_output_head_v1_send_name(head,i ? "DP-2" : "DP-1");
            zwlr_output_head_v1_send_make(head,"Maker"); zwlr_output_head_v1_send_model(head,"Model");
            zwlr_output_head_v1_send_serial_number(head,(i && !self.duplicate) ? "SN84" : self.serial.c_str());
            static const struct zwlr_output_mode_v1_interface modeImpl={Destroy};
            auto* mode=self.Resource(peer,&zwlr_output_mode_v1_interface,3,0,&modeImpl);
            zwlr_output_head_v1_send_mode(head,mode);
            zwlr_output_mode_v1_send_size(mode,800,600);
            zwlr_output_mode_v1_send_refresh(mode,60000);
            zwlr_output_mode_v1_send_preferred(mode);
            zwlr_output_head_v1_send_enabled(head,i ? 0 : 1); // Disabled duplicate remains a witness.
        }
        if (!self.omitDone) zwlr_output_manager_v1_send_done(manager,1);
        if (self.pauseAfterManager) { self.pauseAfterManager=false; self.paused=true; }
    }
    void Start() {
        server=wl_display_create(); ASSERT_NE(server,nullptr);
        for(auto& item:native) ASSERT_NE(wl_global_create(server,&wl_output_interface,2,&item,BindOutput),nullptr);
        ASSERT_NE(wl_global_create(server,&zxdg_output_manager_v1_interface,2,this,BindXdg),nullptr);
        ASSERT_NE(wl_global_create(server,&zwlr_output_manager_v1_interface,version,this,BindManager),nullptr);
        int pair[2]; ASSERT_EQ(socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,pair),0);
        ASSERT_NE(wl_client_create(server,pair[0]),nullptr);
        client=wl_display_connect_to_fd(pair[1]); ASSERT_NE(client,nullptr);
        running=true;
        worker=std::thread([this] {
            while(running) {
                if(paused) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); continue; }
                wl_event_loop_dispatch(wl_display_get_event_loop(server),5);
                if (!paused) wl_display_flush_clients(server);
            }
        });
        registry=wl_display_get_registry(client);
        static const wl_registry_listener listener={
            [](void* data,wl_registry* registry,std::uint32_t name,const char* interface,std::uint32_t) {
                auto& self=*static_cast<Server*>(data);
                if(std::string(interface)=="wl_output" && !self.output)
                    self.output=static_cast<wl_output*>(wl_registry_bind(registry,name,&wl_output_interface,2));
            }, [](void*,wl_registry*,std::uint32_t) {}};
        wl_registry_add_listener(registry,&listener,this);
        ASSERT_GE(wl_display_roundtrip(client),0); ASSERT_NE(output,nullptr);
        // No listeners are installed on the borrowed output by production.
        ASSERT_GE(wl_display_roundtrip(client),0);
    }
    ~Server() {
        Platform::LinuxIdentity::ShutdownWaylandDiscovery();
        paused=false;
        if(output) wl_output_destroy(output);
        if(registry) wl_registry_destroy(registry);
        if(client) wl_display_disconnect(client);
        running=false;
        if(worker.joinable()) worker.join();
        if(server) { wl_display_destroy_clients(server); wl_display_destroy(server); }
    }
};
class WaylandIdentityProtocol : public ::testing::Test {
protected:
    SDL_DisplayID id=0;
    void SetUp() override {
        ASSERT_TRUE(SDL_SetHint(SDL_HINT_VIDEO_DRIVER,"dummy")); ASSERT_TRUE(SDL_Init(SDL_INIT_VIDEO));
        id=SDL_GetPrimaryDisplay(); ASSERT_NE(id,0u);
    }
    void TearDown() override { SDL_ClearProperty(SDL_GetGlobalProperties(),SDL_PROP_GLOBAL_VIDEO_WAYLAND_WL_DISPLAY_POINTER); SDL_Quit(); }
    Platform::DisplayCatalog Read(Server& server) {
        SDL_SetPointerProperty(SDL_GetGlobalProperties(),SDL_PROP_GLOBAL_VIDEO_WAYLAND_WL_DISPLAY_POINTER,server.client);
        SDL_SetPointerProperty(SDL_GetDisplayProperties(id),"SDL.display.wayland.wl_output",server.output);
        Platform::DisplayCatalog catalog; catalog.displays.resize(1);
        Platform::LinuxIdentity::DiscoverWayland(catalog,&id,1); return catalog;
    }
};
TEST_F(WaylandIdentityProtocol, ExactBorrowedOutputProducesOwnedSerialAndReleasesEverySnapshot) {
    Server server; server.Start(); ASSERT_NE(server.output,nullptr);
    std::optional<Platform::DisplayIdentity> retained;
    for(int i=0;i<20;++i) {
        auto catalog=Read(server); ASSERT_TRUE(catalog.displays[0].identity) << catalog.displays[0].identityStatus;
        retained=catalog.displays[0].identity;
        EXPECT_TRUE(Platform::EqualDisplayIdentity(*retained,*Platform::LinuxIdentity::WaylandSerial("Maker","Model","SN42")));
        ASSERT_GE(wl_display_roundtrip(server.client),0); EXPECT_EQ(server.objects,0);
    }
    EXPECT_EQ(server.writes,0); EXPECT_FALSE(retained->value.empty());
}
TEST_F(WaylandIdentityProtocol, DisabledDuplicateSerialRejectsEnabledTarget) {
    Server server; server.duplicate=true; server.Start(); ASSERT_NE(server.output,nullptr);
    auto catalog=Read(server); EXPECT_FALSE(catalog.displays[0].identity);
    EXPECT_EQ(catalog.displays[0].identityStatus,"duplicate native monitor serial metadata");
}
TEST_F(WaylandIdentityProtocol, MissingSerialAndUnsupportedProtocolVersionStayUnavailable) {
    for(int scenario=0;scenario<2;++scenario) {
        Server server; if(scenario==0) server.serial="Unknown"; else server.version=2; server.Start(); ASSERT_NE(server.output,nullptr);
        EXPECT_FALSE(Read(server).displays[0].identity); EXPECT_EQ(server.writes,0);
    }
}
TEST_F(WaylandIdentityProtocol, UnresponsiveMetadataServerReturnsWithinBoundAndRecovers) {
    Server server; server.Start(); ASSERT_NE(server.output,nullptr); server.paused=true;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const auto start=std::chrono::steady_clock::now();
    EXPECT_FALSE(Read(server).displays[0].identity);
    EXPECT_LT(std::chrono::steady_clock::now()-start,std::chrono::milliseconds(900));
    server.paused=false; ASSERT_GE(wl_display_roundtrip(server.client),0);
    EXPECT_TRUE(Read(server).displays[0].identity);
}
}

TEST_F(WaylandIdentityProtocol, TimeoutAfterBindingRetainsQueueUntilLateHeadsCanBeReleased) {
    Server server; server.pauseAfterManager=true; server.Start(); ASSERT_NE(server.output,nullptr);
    EXPECT_FALSE(Read(server).displays[0].identity);
    server.paused=false;
    ASSERT_GE(wl_display_roundtrip(server.client),0);
    EXPECT_TRUE(Read(server).displays[0].identity);
    ASSERT_GE(wl_display_roundtrip(server.client),0);
    EXPECT_EQ(server.objects,0);
    EXPECT_EQ(server.writes,0);
}

TEST_F(WaylandIdentityProtocol, IncompleteNativeTransactionIsNotPublished) {
    Server server; server.omitDone=true; server.Start(); ASSERT_NE(server.output,nullptr);
    EXPECT_FALSE(Read(server).displays[0].identity);
    ASSERT_GE(wl_display_roundtrip(server.client),0);
    EXPECT_EQ(server.objects,0);
}
