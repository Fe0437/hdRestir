#pragma once

#include "buffer_provider.h"
#include "buffer_user.h"
#include "call_index.h"
#include "ray_intersection.h"
#include "rng.h"
#include "scene_interface.h"
#include "spectrum.h"

namespace Restir
{

    class IIntegrator
    {
      public:
        virtual ~IIntegrator() = default;

        [[nodiscard]] virtual SampledSpectrum Li(const RayIntersection &isect, const IScene &scene, Rng &rng,
                                                 const SampledWavelengths &lambda, IBufferProvider &provider,
                                                 CallIndex callId) const = 0;

        // Returns the buffer stager for this integrator, if any.
        // Called once per frame before the pixel loop to declare persistent buffers.
        [[nodiscard]] virtual IBufferStager *GetBufferStager()
        {
            return nullptr;
        }
    };

} // namespace Restir
