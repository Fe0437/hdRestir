#pragma once

#include "pxr/base/gf/vec3f.h"

PXR_NAMESPACE_USING_DIRECTIVE

namespace Restir {

class IEnvironment {
public:
    virtual ~IEnvironment() = default;

    // Radiance for a miss direction (normalised). Extension point: Epic 2.4 importance-sampling.
    [[nodiscard]] virtual GfVec3f Sample(const GfVec3f& dir) const = 0;
};

}  // namespace Restir
