#pragma once

#include "pxr/base/gf/vec2f.h"

PXR_NAMESPACE_USING_DIRECTIVE

namespace Restir
{

    // Concept: a texture sampler that can be called with a UV coordinate.
    // Return type is unconstrained — use BoundTextureSampler<T, Linear> for the concrete form.
    template <typename S>
    concept AnyTextureSampler = requires(const S &s, GfVec2f uv) { s.Sample(uv); };

} // namespace Restir
