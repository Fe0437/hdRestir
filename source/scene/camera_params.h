#pragma once

#include <algorithm>

namespace Restir {

struct CameraParams {
    bool  enableDoF      = false;
    bool  enableExposure = false;
    float focalLength    = 50.0f;
    float fStop          = 5.6f;
    float focusDistance  = 10.0f;
    int   bokehBlades    = 0;
    float lensDistortion = 0.0f;
    float iso            = 100.0f;
    float shutterSpeed   = 0.02f;
};

[[nodiscard]] inline float GetExposureMultiplier(const CameraParams& params) noexcept
{
    if (!params.enableExposure) {
        return 1.0f;
    }

    const float safeFStop{std::max(params.fStop, 0.1f)};
    return (params.iso / 100.0f) * params.shutterSpeed / (safeFStop * safeFStop) * 100.0f;
}

}  // namespace Restir
