#pragma once

#include "render_job.h"

#include "pxr/imaging/hd/renderThread.h"

PXR_NAMESPACE_USING_DIRECTIVE

namespace Restir::Hydra {

// Adapter that wraps HdRenderThread and implements IRenderJob.
// This keeps HdRenderThread usage confined to the Hydra integration layer.
class RenderJob final : public IRenderJob {
public:
    explicit RenderJob(HdRenderThread* renderThread) noexcept : _renderThread{renderThread} {}

    [[nodiscard]] bool IsCancelled() const noexcept override {
        return _renderThread && _renderThread->IsStopRequested();
    }

private:
    HdRenderThread* _renderThread;
};

}  // namespace Restir::Hydra
