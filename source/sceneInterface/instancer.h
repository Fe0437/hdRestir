#pragma once

#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/vt/array.h"
#include "pxr/usd/sdf/path.h"

PXR_NAMESPACE_USING_DIRECTIVE

namespace Restir {

class IInstancer {
public:
    virtual ~IInstancer() = default;

    [[nodiscard]] virtual VtMatrix4dArray ComputeInstanceTransforms(
        SdfPath const& prototypeId) = 0;
};

}  // namespace Restir