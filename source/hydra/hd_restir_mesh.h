#ifndef HD_RESTIR_MESH_H
#define HD_RESTIR_MESH_H

#include "pxr/base/gf/matrix4f.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/vec3i.h"
#include "pxr/base/vt/array.h"
#include "pxr/imaging/hd/mesh.h"
#include "pxr/pxr.h"
#include "scene/scene.h"
#include "sceneInterface/mesh.h"

#include <memory>
#include <string>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

class HdRestirMesh final : public HdMesh, public Restir::IMesh
{
  public:
    using Subset = Restir::MeshSubset;

    HdRestirMesh(SdfPath const &id);
    virtual ~HdRestirMesh() = default;

    virtual HdDirtyBits GetInitialDirtyBitsMask() const override;

    virtual void Sync(HdSceneDelegate *sceneDelegate, HdRenderParam *renderParam, HdDirtyBits *dirtyBits,
                      TfToken const &reprToken) override;

    virtual void Finalize(HdRenderParam *renderParam) override;

    const VtVec3fArray &GetPoints() const override
    {
        return _points;
    }
    const GfMatrix4f &GetTransform() const override
    {
        return _transform;
    }
    bool IsVisible() const override
    {
        return _visible;
    }
    const VtVec3fArray &GetColors() const override
    {
        return _colors;
    }
    const VtVec2fArray &GetUVs() const override
    {
        return _uvs;
    }
    const VtVec3fArray &GetNormals() const override
    {
        return _normals;
    }

    const std::vector<Subset> &GetSubsets() const override
    {
        return _subsets;
    }
    const SdfPath &GetInstancerId() const override
    {
        return _instancerId;
    }
    const SdfPath &GetId() const override
    {
        return HdMesh::GetId();
    }

  protected:
    virtual void _InitRepr(TfToken const &reprToken, HdDirtyBits *dirtyBits) override;

    virtual HdDirtyBits _PropagateDirtyBits(HdDirtyBits bits) const override;

  private:
    TfTokenVector _updateComputedPrimvarSources(HdSceneDelegate *sceneDelegate, HdDirtyBits dirtyBits);
    void          _syncSceneState(HdSceneDelegate *sceneDelegate, HdDirtyBits dirtyBits, Restir::Scene &scene);
    void          _syncMeshState(HdSceneDelegate *sceneDelegate, HdDirtyBits dirtyBits);
    void          _rebuildSubsetsIfNeeded(HdSceneDelegate *sceneDelegate, HdDirtyBits dirtyBits);

    VtVec3fArray        _points{};
    GfMatrix4f          _transform{1.0f};
    SdfPath             _instancerId{};
    std::vector<Subset> _subsets{};
    bool                _visible{true};
    bool                _subsetsDirty{true};

    VtVec3fArray _colors{};
    VtVec2fArray _uvs{};
    VtVec3fArray _normals{};

    HdRestirMesh(const HdRestirMesh &)            = delete;
    HdRestirMesh &operator=(const HdRestirMesh &) = delete;
};

#endif // HD_RESTIR_MESH_H
