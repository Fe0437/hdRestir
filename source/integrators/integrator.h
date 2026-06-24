#pragma once

#include "camera_ray.h"
#include "hit_record.h"
#include "rng.h"
#include "scene_interface.h"
#include "spectrum.h"

#include <optional>

namespace Restir
{

    struct RayIntersection
    {
        Ray                      ray{};
        std::optional<HitRecord> hit{};
    };

    class IIntegrator
    {
      public:
        virtual ~IIntegrator() = default;

        [[nodiscard]] virtual SampledSpectrum Li(const RayIntersection &isect, const IScene &scene, Rng &rng,
                                                 const SampledWavelengths &lambda) const = 0;

        [[nodiscard]] virtual SampledSpectrum Li(const ShadingPoint &surface, const IScene &scene, Rng &rng) const = 0;
    };

} // namespace Restir
