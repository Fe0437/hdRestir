#pragma once

#include "light_interface.h"
#include "light_params.h"

#include <memory>

namespace Restir
{

    enum class LightType
    {
        Distant,
        Dome,
        Rect,
        Point
    };

    [[nodiscard]] std::unique_ptr<ILight> MakeLight(LightType type, const LightParams &params);

} // namespace Restir
