#pragma once

#include "bvh.h"
#include "pxr/base/gf/matrix4f.h"
#include "pxr/base/gf/range3f.h"
#include "pxr/base/gf/vec2f.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/vec3i.h"
#include "pxr/base/vt/array.h"
#include "pxr/usd/sdf/path.h"

#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace Restir
{

    struct MeshSubset
    {
        SdfPath      materialId;
        VtVec3iArray indices;
        BVH          bvh;
        GfRange3f    range;
    };

    class IMesh
    {
      public:
        virtual ~IMesh() = default;

        [[nodiscard]] virtual const VtVec3fArray            &GetPoints() const      = 0;
        [[nodiscard]] virtual const GfMatrix4f              &GetTransform() const   = 0;
        [[nodiscard]] virtual bool                           IsVisible() const      = 0;
        [[nodiscard]] virtual const VtVec3fArray            &GetColors() const      = 0;
        [[nodiscard]] virtual const VtVec2fArray            &GetUVs() const         = 0;
        [[nodiscard]] virtual const VtVec3fArray            &GetNormals() const     = 0;
        [[nodiscard]] virtual const std::vector<MeshSubset> &GetSubsets() const     = 0;
        [[nodiscard]] virtual const SdfPath                 &GetInstancerId() const = 0;
        [[nodiscard]] virtual const SdfPath                 &GetId() const          = 0;
    };

} // namespace Restir