#pragma once

#include "debug.h"
#include "render_context.h"

#include <concepts>
#include <string>
#include <vector>

namespace Restir
{

    class RenderPass
    {
      public:
        explicit RenderPass(std::string name) noexcept;

        virtual ~RenderPass()                         = default;
        RenderPass(const RenderPass &)                = delete;
        RenderPass &operator=(const RenderPass &)     = delete;
        RenderPass(RenderPass &&) noexcept            = default;
        RenderPass &operator=(RenderPass &&) noexcept = default;

        void Execute(RenderContext &ctx);

        [[nodiscard]] std::string_view name() const noexcept
        {
            return _name;
        }
        [[nodiscard]] virtual std::vector<std::string> Inputs() const  = 0;
        [[nodiscard]] virtual std::vector<std::string> Outputs() const = 0;

      protected:
        virtual void _execute(RenderContext &ctx) = 0;

        // Extension point: Epic 6 — full render-graph compile().
        // virtual void Setup(RenderGraphBuilder&) {}
        // [[nodiscard]] virtual bool HasSideEffect() const noexcept { return false; }

      protected:
        std::string _name;
    };

    template <typename PassT, typename... Args>
    concept HasStaticPassIo = std::derived_from<PassT, RenderPass> && requires {
        { PassT::StaticInputs() } -> std::same_as<std::vector<std::string>>;
        { PassT::StaticOutputs() } -> std::same_as<std::vector<std::string>>;
    };

} // namespace Restir
