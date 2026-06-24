#pragma once

#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/rect2i.h"

PXR_NAMESPACE_USING_DIRECTIVE

namespace Restir
{

    struct Camera
    {
        GfRect2i   DataWindow{GfVec2i{0}, 0, 0};
        GfMatrix4d ViewMatrix{1.0};
        GfMatrix4d ProjMatrix{1.0};
        GfMatrix4d InverseViewMatrix{1.0};
        GfMatrix4d InverseProjMatrix{1.0};
        int        FrameCount{0};
    };

} // namespace Restir