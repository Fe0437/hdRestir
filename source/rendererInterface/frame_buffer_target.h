#pragma once

#include "core/frame_buffer.h"
#include "pxr/base/gf/vec3i.h"
#include "pxr/base/gf/vec4f.h"
#include "pxr/base/tf/token.h"
#include "pxr/base/vt/value.h"

#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace Restir
{

    class IFrameBufferTarget
    {
      public:
        virtual ~IFrameBufferTarget() = default;

        virtual void              *Map()                                                                           = 0;
        virtual void               Unmap()                                                                         = 0;
        [[nodiscard]] virtual bool IsConverged() const                                                             = 0;
        virtual void               SetConverged(bool converged)                                                    = 0;
        virtual void               Resolve()                                                                       = 0;
        virtual void ResolveBucket(unsigned int startX, unsigned int startY, unsigned int endX, unsigned int endY) = 0;
        virtual void GetFloatBuffer(std::vector<float> &outFloats) const                                           = 0;
        virtual void CopyFromFrameBuffer(const FrameBuffer &source)                                                = 0;
        virtual void WriteSample(GfVec3i const &pixel, GfVec4f const &color)                                       = 0;
        virtual void Write(GfVec3i const &pixel, size_t numComponents, float const *value)                         = 0;
        virtual void Write(GfVec3i const &pixel, size_t numComponents, int const *value)                           = 0;
        virtual void Clear(size_t numComponents, float const *value)                                               = 0;
        virtual void Clear(size_t numComponents, int const *value)                                                 = 0;
    };

    struct AovBinding
    {
        TfToken             AovName;
        IFrameBufferTarget *Target{nullptr};
        VtValue             ClearValue{};
    };

    [[nodiscard]] inline bool operator==(const AovBinding &lhs, const AovBinding &rhs)
    {
        return lhs.AovName == rhs.AovName && lhs.Target == rhs.Target && lhs.ClearValue == rhs.ClearValue;
    }

} // namespace Restir