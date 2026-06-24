#ifndef HD_RESTIR_INSTANCER_H
#define HD_RESTIR_INSTANCER_H

#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/vt/array.h"
#include "pxr/imaging/hd/instancer.h"
#include "pxr/pxr.h"
#include "sceneInterface/instancer.h"

#include <map>
#include <mutex>

PXR_NAMESPACE_USING_DIRECTIVE

class HdRestirInstancer final : public HdInstancer, public Restir::IInstancer
{
  public:
    HdRestirInstancer(HdSceneDelegate *delegate, SdfPath const &id);
    virtual ~HdRestirInstancer() = default;

    virtual void Sync(HdSceneDelegate *sceneDelegate, HdRenderParam *renderParam, HdDirtyBits *dirtyBits) override;

    VtMatrix4dArray ComputeInstanceTransforms(SdfPath const &prototypeId) override;

  private:
    std::map<SdfPath, VtMatrix4dArray> _cachedTransforms{};
    std::mutex                         _cacheMutex;
};

#endif // HD_RESTIR_INSTANCER_H
