#pragma once

#include "light_interface.h"
#include "light_params.h"

namespace Restir {

// Handles point and sphere lights, including spot-light cone shaping.
class PointLight final : public ILight {
public:
    explicit PointLight(const LightParams& params) noexcept : _params{params} {}

    void SetParams(const LightParams& params) override { _params = params; }

    [[nodiscard]] std::optional<LightSample> SampleLight(
        const GfVec3f& hitPos, Rng& rng) const override;

private:
    LightParams _params;
};

}  // namespace Restir
