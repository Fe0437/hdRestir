#pragma once

#include "light_params.h"
#include "rng.h"

#include "pxr/base/gf/vec3f.h"

#include <optional>

PXR_NAMESPACE_USING_DIRECTIVE

namespace Restir {

struct LightSample {
    GfVec3f Dir{};
    GfVec3f Color{};
    float   Dist{1e30f};
    float   Pdf{1.0f};
};

class ILight {
public:
    virtual ~ILight() = default;

    virtual void SetParams(const LightParams& params) = 0;

    // Called once when the light is added or its parameters change.
    // Expensive work (texture loading, CDF building) goes here.
    // Default is a no-op; only DomeLight overrides.
    virtual void Prepare() {}

    // True for delta lights (directional, point with zero radius) — MIS weight = 1.
    [[nodiscard]] virtual bool IsDeltaLight() const noexcept { return false; }

    [[nodiscard]] virtual std::optional<LightSample> SampleLight(
        const GfVec3f& hitPos, Rng& rng) const = 0;
};

}  // namespace Restir
