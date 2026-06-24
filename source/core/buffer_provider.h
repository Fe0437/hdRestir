#pragma once

#include "frame_buffer.h"

#include <cstddef>
#include <string_view>

namespace Restir
{

    // Generic buffer-map accessor passed through the Li() call chain.
    // Implementations wrap FrameBuffersMap and capture the pixel count.
    class IBufferProvider
    {
      public:
        virtual ~IBufferProvider() = default;

        [[nodiscard]] virtual bool Has(std::string_view name) const = 0;

        // Returns an existing buffer by name. Safe to call from parallel Li() execution.
        [[nodiscard]] virtual FrameBuffer &GetChecked(std::string_view name) = 0;

        // Persistent across frames (survives ExtractPersistentTo / InjectFrom).
        // Creation is not thread-safe; call only during single-threaded preparation.
        [[nodiscard]] virtual void *GetOrCreatePersistent(std::string_view name, std::size_t stride) = 0;

        // Transient — recreated every frame. Call only during single-threaded setup.
        [[nodiscard]] virtual void *Add(std::string_view name, std::size_t stride) = 0;
    };

} // namespace Restir
