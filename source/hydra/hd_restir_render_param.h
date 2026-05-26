#ifndef HD_RESTIR_RENDER_PARAM_H
#define HD_RESTIR_RENDER_PARAM_H

#include "scene/scene.h"

#include "pxr/pxr.h"
#include "pxr/imaging/hd/renderDelegate.h"

#include <atomic>
#include <utility>

PXR_NAMESPACE_USING_DIRECTIVE

class HdRestirRenderParam final : public HdRenderParam
{
public:
    HdRestirRenderParam(Restir::Scene *scene,
                        std::atomic<int> *sceneVersion)
        : _scene(scene)
        , _sceneVersion(sceneVersion)
    {}

    template <typename EditFn>
    void EditScene(EditFn&& editFn)
    {
        std::forward<EditFn>(editFn)(*_scene);
        _sceneVersion->fetch_add(1, std::memory_order_release);
    }

private:
    Restir::Scene* _scene{};
    std::atomic<int>* _sceneVersion{};
};

#endif // HD_RESTIR_RENDER_PARAM_H
