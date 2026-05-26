#pragma once

#include "pxr/base/gf/vec3f.h"

PXR_NAMESPACE_USING_DIRECTIVE

namespace Restir {

class IEnvironment {
public:
    virtual ~IEnvironment() = default;

    // Radiance for a miss direction (normalised).
    [[nodiscard]] virtual GfVec3f Sample(const GfVec3f& dir) const = 0;

    // Solid-angle pdf that NEE would assign to this direction (for MIS in the path loop).
    // Default is 0 (non-importance-sampled or delta environments).
    [[nodiscard]] virtual float EvalPdf(const GfVec3f& dir) const { return 0.f; }
};

}  // namespace Restir
