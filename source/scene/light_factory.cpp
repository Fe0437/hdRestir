#include "light_factory.h"

#include "distant_light.h"
#include "dome_light.h"
#include "point_light.h"
#include "rect_light.h"

namespace Restir
{

    std::unique_ptr<ILight> MakeLight(LightType type, const LightParams &params)
    {
        switch (type)
        {
            case LightType::Distant:
                return std::make_unique<DistantLight>(params);
            case LightType::Dome:
                return std::make_unique<DomeLight>(params);
            case LightType::Rect:
                return std::make_unique<RectLight>(params);
            case LightType::Point:
                return std::make_unique<PointLight>(params);
        }
        return std::make_unique<PointLight>(params);
    }

} // namespace Restir
