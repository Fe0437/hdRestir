#pragma once

#include "path_tracer_pipeline.h"
#include "pxr/base/tf/token.h"
#include "pxr/base/vt/value.h"
#include "render_context.h"
#include "render_pipeline.h"
#include "ris_pipeline.h"
#include "split_screen.h"

#include <gsl/gsl>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace Restir
{

    struct RendererPipelineSettings
    {
        std::unordered_map<TfToken, VtValue, TfToken::HashFunctor> Values{};
        TfToken                                                    PrimaryPipeline{};
        TfToken                                                    SplitScreenRightPipeline{};
        bool                                                       EnableSplitScreen{false};
        std::vector<std::string>                                   OutputNames{};
    };

    class RendererPipelineState final
    {
      public:
        explicit RendererPipelineState(const RendererPipelineSettings &settings);

        void               Execute(RenderContext &ctx);
        void               ClearPersistentBuffers();
        [[nodiscard]] bool IsConverged(int frameCount, int singleTarget) const noexcept;

      private:
        std::unique_ptr<RenderPipeline>        _singlePipeline{};
        std::unique_ptr<SplitScreenCompositor> _splitScreen{};
    };

    [[nodiscard]] RendererPipelineSettings
    MakeRendererPipelineSettings(const std::unordered_map<TfToken, VtValue, TfToken::HashFunctor> &renderSettings,
                                 gsl::span<const std::string> requestedOutputNames);

    [[nodiscard]] TfToken GetPathTracerPipelineToken();
    [[nodiscard]] TfToken GetPathTracerPostProcessPipelineToken();
    [[nodiscard]] TfToken GetRISPipelineToken();

} // namespace Restir