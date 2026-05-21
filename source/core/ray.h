#pragma once

#include "pxr/base/gf/vec3f.h"

PXR_NAMESPACE_USING_DIRECTIVE

namespace Restir {

struct Ray {
    GfVec3f Origin{};
    GfVec3f Dir{};
};

}  // namespace Restir
