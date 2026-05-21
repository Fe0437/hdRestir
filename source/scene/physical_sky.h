#pragma once

#include "environment.h"
#include "light_interface.h"

#include "pxr/base/gf/vec3f.h"

#include <optional>

PXR_NAMESPACE_USING_DIRECTIVE

namespace Restir {

// Analytical Rayleigh/Mie sky with a single sun.
// Implements IEnvironment for sky-miss sampling and ILight for sun direct-lighting.
class PhysicalSky final : public IEnvironment, public ILight {
public:
    // Sun direction and radiance color for one direct-lighting sample.
    struct SunSample {
        GfVec3f Dir{};
        GfVec3f Color{};
    };

    explicit PhysicalSky(const GfVec3f& sunDir) noexcept : _sunDir{sunDir} {}

    void SetParams(const LightParams& /*params*/) override {}

    // IEnvironment
    [[nodiscard]] GfVec3f Sample(const GfVec3f& dir) const override;

    // ILight — sun is a delta light, MIS weight = 1, Pdf = 1.
    [[nodiscard]] bool IsDeltaLight() const noexcept override { return true; }
    [[nodiscard]] std::optional<LightSample> SampleLight(
        const GfVec3f& hitPos, Rng& rng) const override;

    [[nodiscard]] GfVec3f                    GetSunTransmittance() const;
    [[nodiscard]] std::optional<SunSample>   SampleDirect()        const;
    [[nodiscard]] const GfVec3f&             GetSunDir()           const noexcept { return _sunDir; }

private:
    GfVec3f _sunDir;
};

}  // namespace Restir
