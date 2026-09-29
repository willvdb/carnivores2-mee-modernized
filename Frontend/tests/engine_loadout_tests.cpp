// Compile actual argument, award and refill bodies with only state/services doubled.
#define main legacy_argument_probe_main
#include "launch_argument_probe.cpp"
#undef main
#include <cassert>
namespace Platform {
struct Time { int year=2026,month=9,day=29,hour=12,minute=0; };
Time LocalTime() { return {}; }
}
struct Vec {};
Vec PlayerPos;
Vec SubVectors(Vec,Vec) { return {}; }
float VectorLength(Vec) { return 64; }
struct Character { int CType=0,tempScore=0,tempDate=0,tempTime=0; float tempRange=0; Vec pos; } Characters[1];
struct Dino { float BaseScore=100; } DinoInfo[1];
// SubmitDinoScore uses more of TrophyRoom than the argument-only double.
struct ScoreRoom { int Score=0; struct { int success=0; } Last; } AwardRoom;
int ScoreDispTime=0,ScoreDisp=0;
#define TrophyRoom AwardRoom
#include "loadout_score.inc"
#undef TrophyRoom
int TotalW=8,TargetWeapon=-1;
struct WeaponInfo { bool fullauto=false; int Shots=10,Reload=0,rldAnim=1,pmpAnim=1; } WeapInfo[10];
int FiringMode[10]{},ShotsLeft[10]{},AmmoMag[10]{},MagShotsLeft[10]{},Chambered[10]{};
#include "loadout_refill.inc"
int main() {
    const int expected[]={100,85,70,59,80,68,56,47};
    for(unsigned bits=0;bits<16;++bits) {
        CamoMode=RadarMode=ScentMode=DoubleAmmo=false;
        Platform::arguments={"probe","din=511","wep=255","dtm=2","smod=0.85,0.70,0.80,1.0,1.25,1.0"};
        const char* flags[]={"-camo","-radar","-scent","-double"};
        for(unsigned i=0;i<4;++i) if(bits&(1u<<i)) Platform::arguments.push_back(flags[i]);
        ProcessCommandLine();
        assert(TargetDino==511*1024 && WeaponPres==255 && OptDayNight==2);
        assert(CamoMode==bool(bits&1) && RadarMode==bool(bits&2) && ScentMode==bool(bits&4) && DoubleAmmo==bool(bits&8));
        assert(!ObservMode && !Tranq && !NightVisionMode);
        AwardRoom.Score=0; SubmitDinoScore(0);
        assert(AwardRoom.Score==expected[bits&7]); // double ammo has no award effect
    }
    for(int mask:{0,1,129,255,1023}) {
        Platform::arguments={"probe","din="+std::to_string(mask),"wep="+std::to_string(mask)};
        ProcessCommandLine(); assert(TargetDino==mask*1024 && WeaponPres==mask);
    }
    Platform::arguments={"probe","din=1024","wep=-1"};
    ProcessCommandLine(); assert(TargetDino==1023*1024 && WeaponPres==1023); // refused, unchanged
    WeaponPres=129; TargetWeapon=-1; DoubleAmmo=true;
    WeapInfo[7].Reload=2;
    refillWeapons(true);
    assert(TargetWeapon==0 && ShotsLeft[0]==9 && AmmoMag[0]==1 && MagShotsLeft[0]==10);
    assert(ShotsLeft[7]==18 && Chambered[7]==2);
    for(int i=1;i<7;++i) assert(ShotsLeft[i]==0 && Chambered[i]==0 && AmmoMag[i]==0);
}
