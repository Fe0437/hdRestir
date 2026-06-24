#pragma once

#include "hit_record.h"
#include "ray.h"
#include "shading_types.h"

#include <optional>

namespace Restir
{

    // A ray together with its optional scene intersection and optional pre-computed
    // surface shading state. When shadingPoint is absent the integrator derives it
    // from hit + scene material; when it is present the integrator uses it directly,
    // avoiding a redundant material evaluation.
    struct RayIntersection
    {
        Ray                         ray{};
        std::optional<HitRecord>    hit{};
        std::optional<ShadingPoint> shadingPoint{};
    };

} // namespace Restir
