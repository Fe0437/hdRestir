#ifndef HD_RESTIR_IMPLICIT_SURFACE_SCENE_INDEX_PLUGIN_H
#define HD_RESTIR_IMPLICIT_SURFACE_SCENE_INDEX_PLUGIN_H

#include "pxr/imaging/hd/sceneIndexPlugin.h"
#include "pxr/pxr.h"

PXR_NAMESPACE_USING_DIRECTIVE

class HdRestir_ImplicitSurfaceSceneIndexPlugin : public HdSceneIndexPlugin
{
  public:
    HdRestir_ImplicitSurfaceSceneIndexPlugin();

  protected:
    HdSceneIndexBaseRefPtr _AppendSceneIndex(const HdSceneIndexBaseRefPtr      &inputScene,
                                             const HdContainerDataSourceHandle &inputArgs) override;
};

#endif // HD_RESTIR_IMPLICIT_SURFACE_SCENE_INDEX_PLUGIN_H
