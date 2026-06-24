#pragma once

#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/pxr.h"
#include "pxr/usd/sdf/assetPath.h"

#include <cmath>

PXR_NAMESPACE_USING_DIRECTIVE

namespace Restir
{

    struct LightParams
    {
        GfVec3f      Color{1.0f};
        float        Intensity{1.0f};
        float        Exposure{0.0f};
        GfMatrix4d   Transform{1.0};
        SdfAssetPath TextureFile{};
        float        Width{1.0f};
        float        Height{1.0f};
        float        ShapingConeAngle{180.0f};
        float        ShapingConeSoftness{0.0f};

        [[nodiscard]] float EffectiveIntensity() const
        {
            return Intensity * std::exp2(Exposure);
        }
    };

} // namespace Restir