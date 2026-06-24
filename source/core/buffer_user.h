#pragma once

#include "buffer_provider.h"
#include "scene_interface.h"

namespace Restir
{

    // Optional extension for render components that declare frame buffers before
    // the parallel pixel loop.
    class IBufferStager
    {
      public:
        virtual ~IBufferStager() = default;

        virtual void PrepareBuffers(IBufferProvider &provider, const IScene &scene) = 0;
    };

} // namespace Restir
