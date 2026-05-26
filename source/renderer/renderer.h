#ifndef HD_RESTIR_RENDERER_H
#define HD_RESTIR_RENDERER_H

#include "pxr/pxr.h"
#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/tf/token.h"
#include "pxr/base/vt/value.h"
#include "render_job.h"
#include "pxr/base/gf/rect2i.h"
#include "denoiser.h"
#include "renderer_pipeline_state.h"
#include "path_trace_pass.h"
#include "post_process.h"
#include "restir_render_settings.h"
#include "rendererInterface/frame_buffer_target.h"
#include "scene/camera.h"
#include <vector>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "rng.h"
#include "scene_interface.h"

#include <gsl/gsl>
#include <optional>
#include <string_view>

PXR_NAMESPACE_USING_DIRECTIVE

namespace Restir {

class Renderer final {
public:
    struct RenderOptionSpec {
        std::string_view Label;
        TfToken          Token;
        VtValue          DefaultValue;
        bool             Hidden{false};
    };

    Renderer();
    ~Renderer();

    void SetCamera(const GfMatrix4d& viewMatrix, const GfMatrix4d& projMatrix);
    void SetDataWindow(const GfRect2i& dataWindow);
    void SetRequestedOutputNames(std::vector<std::string> outputNames) { _requestedOutputNames = std::move(outputNames); }
    [[nodiscard]] gsl::span<const std::string> GetRequestedOutputNames() const { return _requestedOutputNames; }
    [[nodiscard]] static std::vector<std::string> CollectRequestedOutputNames(
        gsl::span<const AovBinding> aovBindings);

    void Render(const IRenderJob& job,
                IScene& scene);
    void Clear();
    void ClearTargets(gsl::span<const AovBinding> aovBindings);
    void MarkAovBuffersUnconverged(gsl::span<const AovBinding> aovBindings);
    void ResolveTargets(gsl::span<const AovBinding> aovBindings);
    bool IsConverged() const;

    [[nodiscard]] static gsl::span<const RenderOptionSpec> GetRenderOptionSpecs();
    [[nodiscard]] VtValue GetRenderSetting(const TfToken& key) const;
    bool SetRenderSetting(const TfToken& key, const VtValue& value);

private:

    static GfVec4f _getClearColor(VtValue const& clearValue);
    std::unique_ptr<RendererPipelineState> _pipelineState;

    std::vector<std::string> _requestedOutputNames{std::string{kColorOutputName}};

    Rng _rng{};
    std::unordered_map<TfToken, VtValue, TfToken::HashFunctor> _renderSettings;
    Camera _camera{};

    std::mutex _resolvedOutputsMutex;
    std::unordered_map<std::string, FrameBuffer> _resolvedOutputs;
};

}  // namespace Restir

#endif // HD_RESTIR_RENDERER_H
