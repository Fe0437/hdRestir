#pragma once

#include "pxr/base/gf/vec2f.h"
#include "pxr/base/gf/vec3f.h"

#include <limits>

PXR_NAMESPACE_USING_DIRECTIVE

namespace Restir
{

    // Public hit record used by Epic 1+ passes. Intentionally smaller than the
    // renderer's internal hit struct: only the fields the G-Buffer needs.
    struct HitRecord
    {
        GfVec3f Position{};
        GfVec3f Normal{};
        GfVec3f SmoothNormal{};
        GfVec3f Dpdu{1.0f, 0.0f, 0.0f};
        GfVec3f Dpdv{0.0f, 1.0f, 0.0f};
        GfVec2f Uv{};
        GfVec3f Albedo{};
        float   Depth{std::numeric_limits<float>::max()};
        int     PrimId{-1};
        int     MatId{-1}; // set by IntersectScene; keys IScene::GetMaterial
    };

} // namespace Restir
