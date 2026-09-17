#pragma once
#include "Core/GameTypes.h"
#include "Core/RenderTypes.h"
#include "Core/AudioTypes.h"
#include "../../Shared/LegacyResource.h"
#include <cstring>
#include <limits>

namespace EngineResource {
static_assert((std::numeric_limits<int>::max)()>=INT32_MAX &&
              (std::numeric_limits<int>::min)()<=INT32_MIN,
              "Runtime resource integers must hold legacy int32 values");
static_assert(sizeof(float)==4 && std::numeric_limits<float>::is_iec559,
              "Resource floats require IEEE-754 binary32");
inline void SetFloat(float& out, std::uint32_t bits) { std::memcpy(&out,&bits,4); }
inline void ToRuntime(const LegacyResource::ObjectInfo& v, TObjInfo& out)
{
    out.Radius=v.radius; out.YLo=v.yLo; out.YHi=v.yHi;
    out.linelenght=v.lineLength; out.lintensity=v.lineIntensity;
    out.circlerad=v.circleRadius; out.cintensity=v.circleIntensity;
    out.flags=v.flags; out.GrRad=v.groundRadius; out.DefLight=v.defaultLight;
    out.LastAniTime=v.lastAnimationTime; SetFloat(out.BoundR,v.boundRadius);
    for(unsigned i=0;i<16;++i) out.res[i]=v.reserved[i];
}
inline void ToRuntime(const LegacyResource::Fog& v, TFogEntity& out)
{
    out.fogRGB=v.rgb; SetFloat(out.YBegin,v.yBegin); out.Mortal=v.mortal;
    SetFloat(out.Transp,v.transparency); SetFloat(out.FLimit,v.limit);
}
inline void ToRuntime(const LegacyResource::RandomEffect& v, TRD& out)
{
    out.RNumber=v.number; out.RVolume=v.volume; out.RFreq=v.frequency;
    out.REnvir=v.environment; out.Flags=v.flags;
}
inline void ToRuntime(const LegacyResource::Water& v, TWaterEntity& out)
{
    out.tindex=v.texture; out.wlevel=v.level; SetFloat(out.transp,v.transparency); out.fogRGB=v.rgb;
}
inline void ToRuntime(const LegacyResource::ColorTable& v, int (&out)[3][3])
{ for(unsigned i=0;i<9;++i) out[i/3][i%3]=v[i]; }
}
