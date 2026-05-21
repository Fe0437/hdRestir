#ifndef HD_RESTIR_RENDER_PASS_H
#define HD_RESTIR_RENDER_PASS_H

#include "pxr/pxr.h"
#include "pxr/imaging/hd/renderPass.h"
#include "pxr/imaging/hd/renderThread.h"
#include "renderer.h"
#include "hd_restir_render_buffer.h"

#include <atomic>
#include <string>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

class HdRestirRenderPass final : public HdRenderPass {
public:
    HdRestirRenderPass(HdRenderIndex *index,
                       HdRprimCollection const &collection,
                       HdRenderThread *renderThread,
                       Restir::Renderer *renderer,
                       std::atomic<int> *sceneVersion);
    virtual ~HdRestirRenderPass();

    virtual bool IsConverged() const override;

protected:
    virtual void _Execute(HdRenderPassStateSharedPtr const& renderPassState,
                          TfTokenVector const &renderTags) override;

    virtual void _MarkCollectionDirty() override {}

private:
    HdRenderThread* _renderThread{};
    Restir::Renderer* _renderer{};
    std::atomic<int>* _sceneVersion{};
    bool _hasBoundAovs{false};
    int _lastSceneVersion{0};
    GfMatrix4d _viewMatrix{1.0};
    GfMatrix4d _projMatrix{1.0};
    GfRect2i _dataWindow{GfVec2i{0}, 0, 0};
    std::vector<std::string> _requestedOutputNames;
    HdRestirRenderBuffer _colorBuffer{SdfPath::EmptyPath()};
    HdRestirRenderBuffer _depthBuffer{SdfPath::EmptyPath()};
};

#endif // HD_RESTIR_RENDER_PASS_H
