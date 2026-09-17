#pragma once
#include "Core/ModelTypes.h"
#include "../../Shared/LegacyModel.h"

namespace EngineModel {
static_assert((std::numeric_limits<int>::max)() >= INT32_MAX &&
              (std::numeric_limits<int>::min)() <= INT32_MIN,
              "Runtime model integers must hold legacy int32 values");
static_assert((std::numeric_limits<short>::max)() >= INT16_MAX &&
              (std::numeric_limits<short>::min)() <= INT16_MIN,
              "Runtime model samples must hold legacy int16 values");
inline void ToRuntime(const LegacyModel::Vertex& v, TPoint3d& out)
{
    LegacyModel::SetFloat(out.x,v.x); LegacyModel::SetFloat(out.y,v.y); LegacyModel::SetFloat(out.z,v.z);
    out.owner=v.owner; out.hide=v.hide;
}
inline void ToRuntime(const LegacyModel::Face& v, TFace& out)
{
    out.v1=v.indices[0]; out.v2=v.indices[1]; out.v3=v.indices[2];
    // CorrectModel still owns the renderer conversion. Supply exactly the
    // legacy integer bits, not a numeric float UV conversion at this boundary.
#ifdef _soft
    out.tax=v.uv[0]; out.tbx=v.uv[1]; out.tcx=v.uv[2];
    out.tay=v.uv[3]; out.tby=v.uv[4]; out.tcy=v.uv[5];
#else
    LegacyModel::SetFloat(out.tax,static_cast<std::uint32_t>(v.uv[0]));
    LegacyModel::SetFloat(out.tbx,static_cast<std::uint32_t>(v.uv[1]));
    LegacyModel::SetFloat(out.tcx,static_cast<std::uint32_t>(v.uv[2]));
    LegacyModel::SetFloat(out.tay,static_cast<std::uint32_t>(v.uv[3]));
    LegacyModel::SetFloat(out.tby,static_cast<std::uint32_t>(v.uv[4]));
    LegacyModel::SetFloat(out.tcy,static_cast<std::uint32_t>(v.uv[5]));
#endif
    out.Flags=v.flags; out.DMask=v.mask; out.Distant=v.distant; out.Next=v.next; out.group=v.group;
    std::memcpy(out.reserv,v.reserved.data(),12);
}
inline void ToRuntime(const LegacyModel::Object& v, TObj& out)
{
    std::memcpy(out.OName,v.name.data(),32);
    LegacyModel::SetFloat(out.ox,v.origin.x); LegacyModel::SetFloat(out.oy,v.origin.y);
    LegacyModel::SetFloat(out.oz,v.origin.z); out.owner=v.origin.owner; out.hide=v.origin.hide;
}
}
