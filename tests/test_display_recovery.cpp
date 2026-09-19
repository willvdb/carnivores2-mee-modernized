#include "../Hunt/Game/DisplayRecovery.h"
#include "../Hunt/Platform/WindowCoordinates.h"
#include <gtest/gtest.h>
#include <limits>
using namespace GameDisplay;
namespace {
Platform::DisplayCatalog Catalog()
{
    Platform::Display a,b;
    a.bounds = {{{0,0},{1920,1080}}}; b.bounds = {{{-1080,-420},{1080,1920}}};
    a.modes = b.modes = {{{800,600},32,{120,1}}};
    b.identity = Platform::DisplayIdentity{1,"linux-x11-edid-serial","1234"};
    return {{a,b},0};
}
Configuration Config()
{
    Configuration config;
    config.size={800,600}; config.refresh={120,1};
    config.monitor={MonitorPreferenceKind::Identity,0,*Catalog().displays[1].identity};
    return config;
}
Platform::Event Topology(bool removed=false)
{
    Platform::Event event;event.type=Platform::EventType::DisplayChanged;event.occupiedDisplayRemoved=removed;return event;
}
Platform::WindowState Live() { return {{800,600},{800,600},false,true}; }
}
TEST(DisplayRecovery, BurstQuietPeriodDeadlineAndClockWrap)
{
    DisplayRecovery r;r.Observe(Topology(),100);
    EXPECT_FALSE(r.Ready(249));EXPECT_TRUE(r.Ready(250));
    r.Observe(Topology(),240);EXPECT_FALSE(r.Ready(300));EXPECT_TRUE(r.Ready(390));
    for (unsigned t=300;t<=590;t+=10) r.Observe(Topology(),t);
    EXPECT_FALSE(r.Ready(599));EXPECT_TRUE(r.Ready(600));
    r.Resolve(Config(),Catalog(),Live());EXPECT_FALSE(r.Ready(1000));
    r.Observe(Topology(),0xfffffff0);EXPECT_FALSE(r.Ready(50));EXPECT_TRUE(r.Ready(150));
}
TEST(DisplayRecovery, WindowAndScaleEventsNeverReapplyFullscreen)
{
    DisplayRecovery r;
    for(auto type:{Platform::EventType::WindowChanged,Platform::EventType::FocusChanged,Platform::EventType::KeyDown}) {
        Platform::Event event;event.type=type;r.Observe(event,0);
        EXPECT_FALSE(r.Ready(1000));
    }
}
TEST(DisplayRecovery, OccupiedOutputLossLatchesAcrossDuplicateEvents)
{
    DisplayRecovery r;r.Observe(Topology(true),0);r.Observe(Topology(),10);
    EXPECT_TRUE(r.Resolve(Config(),Catalog(),Live()));
    EXPECT_FALSE(r.Resolve(Config(),Catalog(),Live()));
}
TEST(DisplayRecovery, LostOutputRecoversEvenAfterCompositorMovedWindowToPrimary)
{
    auto config=Config();auto catalog=Catalog();auto r=DisplayRecovery{};
    r.Applied(config,SelectMonitor(catalog,config.monitor,config.size,config.refresh));
    catalog.displays.pop_back();r.Observe(Topology(true),0);
    EXPECT_TRUE(r.Resolve(config,catalog,Live()));
    const auto fallback=SelectMonitor(catalog,config.monitor,config.size,config.refresh);
    EXPECT_EQ(fallback.index,0u);EXPECT_FALSE(fallback.exclusiveMode);
    r.Applied(config,fallback);
    r.Observe(Topology(),400);EXPECT_FALSE(r.Resolve(config,Catalog(),Live()));
    // Reconnection is deliberately lazy; next explicit request resolves again.
    EXPECT_TRUE(SelectMonitor(Catalog(),config.monitor,config.size,config.refresh).exclusiveMode);
    EXPECT_EQ(config.size.width,800);EXPECT_EQ(config.refresh.numerator,120u);
    EXPECT_TRUE(Platform::EqualDisplayIdentity(config.monitor.identity,*Catalog().displays[1].identity));
}
TEST(DisplayRecovery, InvalidatedAppliedIdentityFallsBackOnceWithoutStealingOnReturn)
{
    for (bool duplicate:{false,true}) {
        auto config=Config();auto catalog=Catalog();DisplayRecovery r;
        r.Applied(config,SelectMonitor(catalog,config.monitor,config.size));
        if(duplicate) catalog.displays[0].identity=catalog.displays[1].identity;
        else catalog.displays[1].identity.reset();
        EXPECT_TRUE(r.Resolve(config,catalog,Live()));
        EXPECT_FALSE(r.Resolve(config,catalog,Live()));
        EXPECT_FALSE(r.Resolve(config,Catalog(),Live()));
    }
}
TEST(DisplayRecovery, RearrangementPrimaryReplacementAndUserMoveDoNotStealReachableWindow)
{
    auto config=Config();auto catalog=Catalog();DisplayRecovery r;
    r.Applied(config,SelectMonitor(catalog,config.monitor,config.size));
    std::swap(catalog.displays[0],catalog.displays[1]);catalog.primaryDisplay=1;
    catalog.displays[0].bounds={{{-1920,-1080},{1920,1080}}};
    EXPECT_FALSE(r.Resolve(config,catalog,Live()));
    auto stranded=Live();stranded.reachable=false;
    EXPECT_TRUE(r.Resolve(config,catalog,stranded));
}
TEST(DisplayRecovery, SessionIndicesAreFreshOrdinalsAndFallbackNeverBorrowsRate)
{
    auto config=Config();config.monitor={MonitorPreferenceKind::SessionIndex,1,{}};
    auto catalog=Catalog();DisplayRecovery r;r.Applied(config,SelectMonitor(catalog,config.monitor,config.size));
    std::swap(catalog.displays[0],catalog.displays[1]);catalog.primaryDisplay=1;
    EXPECT_FALSE(r.Resolve(config,catalog,Live()));
    auto explicitRequest=SelectMonitor(catalog,config.monitor,config.size,config.refresh);
    ASSERT_TRUE(explicitRequest.target);EXPECT_EQ(explicitRequest.target->bounds.origin.x,0);
    catalog.displays.pop_back();catalog.primaryDisplay=0;
    EXPECT_FALSE(SelectMonitor(catalog,config.monitor,config.size,config.refresh).exclusiveMode);
}
TEST(DisplayRecovery, DrawableConstraintsRejectZeroTransientAndOversizedDimensions)
{
    for(auto size:{Platform::Size{0,600},{800,0},{-1,600},{1,1},{8193,100},{8192,8192},
                  {(std::numeric_limits<int>::max)(),(std::numeric_limits<int>::max)()}})
        EXPECT_FALSE(UsableDrawable(size));
    for(auto size:{Platform::Size{800,600},{3840,2160},{2160,3840},{4096,4096}}) EXPECT_TRUE(UsableDrawable(size));
}
TEST(DisplayCoordinates, FractionalPixelScaleAndInverseWarpUseActualDimensions)
{
    const Platform::Size logical{800,600},pixels{1000,750};
    const auto physical=Platform::Coordinates::Convert({400,300},logical,pixels);
    EXPECT_FLOAT_EQ(physical.x,500);EXPECT_FLOAT_EQ(physical.y,375);
    const auto logicalAgain=Platform::Coordinates::Convert(physical,pixels,logical);
    EXPECT_FLOAT_EQ(logicalAgain.x,400);EXPECT_FLOAT_EQ(logicalAgain.y,300);
    const auto motion=Platform::Coordinates::Convert({.25f,-.5f},logical,pixels);
    EXPECT_FLOAT_EQ(motion.x,.3125f);EXPECT_FLOAT_EQ(motion.y,-.625f);
    const auto rotated=Platform::Coordinates::Convert({120,240},{1080,1920},{1350,2400});
    EXPECT_FLOAT_EQ(rotated.x,150);EXPECT_FLOAT_EQ(rotated.y,300);
}
TEST(DisplayCoordinates, InvalidMetricsDrainWithoutDivisionOrStaleMotion)
{
    const auto result=Platform::Coordinates::Convert({1,2},{0,0},{800,600});
    EXPECT_FLOAT_EQ(result.x,0);EXPECT_FLOAT_EQ(result.y,0);
    EXPECT_EQ(Platform::Coordinates::Integer(1e30f),(std::numeric_limits<int>::max)());
    EXPECT_FLOAT_EQ(Platform::Coordinates::Scale(std::numeric_limits<float>::infinity(),1,1),0);
}

TEST(DisplayRecovery, RejectedRecoveryAndDuplicateRemovalDoNotLoopOnSameTopology)
{
    DisplayRecovery r;auto config=Config();auto catalog=Catalog();auto stranded=Live();stranded.reachable=false;
    r.Observe(Topology(true),0);EXPECT_TRUE(r.Resolve(config,catalog,stranded));
    r.Applied(config,SelectMonitor(catalog,config.monitor,config.size));r.Recovered(catalog);
    for(unsigned t=200;t<2000;t+=200) {
        r.Observe(Topology(true),t);EXPECT_FALSE(r.Resolve(config,catalog,stranded));
    }
    catalog.displays[0].bounds->origin.x=200;
    EXPECT_TRUE(r.Resolve(config,catalog,stranded));
    r.Applied(config,SelectMonitor(catalog,config.monitor,config.size));
    EXPECT_TRUE(r.Resolve(config,catalog,stranded));
}

TEST(DisplayRecovery, ZeroDrawableFromLostOutputDoesNotBlockTopologyDecision)
{
    DisplayRecovery r;auto catalog=Catalog();auto config=Config();
    auto zero=Live();zero.pixels={0,0};zero.reachable=false;
    r.Observe(Topology(true),0);EXPECT_TRUE(r.Ready(150));
    EXPECT_TRUE(r.Resolve(config,catalog,zero));
}

TEST(DisplayRecovery, ValidPixelsWithTransientZeroLogicalSizeRemainSuspended)
{
    auto window=Live();EXPECT_TRUE(UsableWindow(window));
    window.logical={0,600};EXPECT_FALSE(UsableWindow(window));
    window.logical={800,600};window.minimized=true;EXPECT_FALSE(UsableWindow(window));
}
