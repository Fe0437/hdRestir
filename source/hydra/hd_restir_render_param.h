#ifndef HD_RESTIR_RENDER_PARAM_H
#define HD_RESTIR_RENDER_PARAM_H

#include "renderer.h"
#include "scene/scene.h"

#include "pxr/pxr.h"
#include "pxr/imaging/hd/renderDelegate.h"
#include "pxr/imaging/hd/renderThread.h"

#include <atomic>

PXR_NAMESPACE_USING_DIRECTIVE

class HdRestirRenderParam final : public HdRenderParam
{
public:
    HdRestirRenderParam(HdRenderThread *renderThread,
                        Restir::Renderer *renderer,
                        Restir::Scene *scene,
                        std::atomic<int> *sceneVersion)
        : _renderThread(renderThread)
        , _renderer(renderer)
        , _scene(scene)
        , _sceneVersion(sceneVersion)
    {}

    void AcquireSceneForEdit() {
        _renderThread->StopRender();
        (*_sceneVersion)++;
    }

    Restir::Renderer* GetRenderer() { return _renderer; }
    Restir::Scene* GetScene() { return _scene; }
    [[nodiscard]] std::recursive_mutex& GetSceneLock() { return _scene->GetSceneLock(); }

private:
    HdRenderThread* _renderThread{};
    Restir::Renderer* _renderer{};
    Restir::Scene* _scene{};
    std::atomic<int>* _sceneVersion{};
};

#endif // HD_RESTIR_RENDER_PARAM_H
