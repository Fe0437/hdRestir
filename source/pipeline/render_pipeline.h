#pragma once

#include "frame_buffer_map.h"
#include "render_pass.h"

#include <gsl/gsl>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Restir
{

    class RenderPipeline final
    {
      public:
        explicit RenderPipeline(std::string &&name) noexcept;

        RenderPipeline &Add(std::unique_ptr<RenderPass> pass);

        // Injects persistent buffers, runs all passes, then extracts persistent buffers back.
        void Execute(RenderContext &ctx);

        // Clears the persistent buffer store, forcing a fresh accumulation start.
        void ClearPersistentBuffers();

        [[nodiscard]] std::string_view Name() const noexcept;
        [[nodiscard]] bool             Empty() const noexcept
        {
            return _passes.empty();
        }
        [[nodiscard]] std::size_t Size() const noexcept
        {
            return _passes.size();
        }

      private:
        std::string                              _name;
        std::vector<std::unique_ptr<RenderPass>> _passes;
        FrameBuffersMap                          _persistentStore;
    };

} // namespace Restir
