#pragma once

#include "materials/material.h"
#include "scene_interface.h"
#include "spectrum.h"

#include <gsl/gsl>

namespace Restir {

[[nodiscard]] SampledSpectrum SampleDirectLighting(
    const ShadingPoint&      surface,
    gsl::span<ILight* const> lights,
    const IScene&            scene,
    Rng&                     rng);

}  // namespace Restir
