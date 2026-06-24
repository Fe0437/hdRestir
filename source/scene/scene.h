#pragma once

#include "default_material.h"
#include "environment.h"
#include "image_texture_sampler.h"
#include "light_factory.h"
#include "light_interface.h"
#include "physical_sky.h"
#include "preview_surface.h"
#include "preview_surface_params.h"
#include "pxr/base/gf/matrix4f.h"
#include "pxr/base/gf/range3f.h"
#include "pxr/base/gf/vec2f.h"
#include "pxr/base/gf/vec3f.h"
#include "sceneInterface/instancer.h"
#include "sceneInterface/mesh.h"
#include "sceneInterface/scene_interface.h"

#include <gsl/gsl>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace Restir
{

    class Scene final : public IScene
    {
      public:
        struct SurfaceHit
        {
            float        t{1e30f};
            GfVec3f      normal{};
            GfVec3f      smoothNormal{};
            GfVec3f      dpdu{1.0f, 0.0f, 0.0f};
            GfVec3f      dpdv{0.0f, 1.0f, 0.0f};
            GfVec2f      uv{0.0f};
            GfVec3f      baseColor{1.0f};
            float        metallic{0.0f};
            float        roughness{0.5f};
            GfVec3f      specularColor{1.0f};
            float        specular{1.0f};
            float        opacity{1.0f};
            float        ior{1.5f};
            float        transmission{0.0f};
            GfVec3f      transmissionColor{1.0f};
            GfVec3f      emission{0.0f};
            SdfAssetPath diffuseTexture{};
            SdfAssetPath normalTexture{};
            SdfAssetPath metallicTexture{};
            SdfAssetPath roughnessTexture{};
            int          instanceIdx{-1};
            IMaterial   *shader{nullptr};
            float        coat{0.0f};
            GfVec3f      coatColor{1.0f};
            float        coatRoughness{0.1f};
            float        coatIor{1.5f};
            float        transmissionDepth{0.0f};
            GfVec3f      transmissionScatter{0.0f};
            float        sheen{0.0f};
            GfVec3f      sheenColor{1.0f};
            float        sheenRoughness{0.3f};
            float        subsurface{0.0f};
            GfVec3f      subsurfaceColor{1.0f};
            GfVec3f      subsurfaceRadius{1.0f};
            float        subsurfaceScale{1.0f};
            float        subsurfaceAnisotropy{0.0f};
            bool         thinWalled{false};
            float        diffuseRoughness{0.0f};
        };

        Scene()           = default;
        ~Scene() override = default;

        void AddMesh(const SdfPath &id, IMesh *mesh);
        void RemoveMesh(const SdfPath &id);

        void AddInstancer(const SdfPath &id, IInstancer *instancer);
        void RemoveInstancer(const SdfPath &id);

        void SetMaterialParams(const SdfPath &id, const PreviewSurfaceParams &params);
        void RemoveMaterialParams(const SdfPath &id);

        void SetLightFactoryInput(const SdfPath &id, const LightFactoryInput &input);
        void RemoveLightFactoryInput(const SdfPath &id);

        [[nodiscard]] std::recursive_mutex &GetSceneLock() override
        {
            return _sceneLock;
        }

        void BuildRenderState(const SceneBuildRenderStateConfig &config, const IRenderJob &job) override;

        [[nodiscard]] const IMaterial                  &GetMaterial(int matId) const override;
        [[nodiscard]] const IEnvironment               *GetEnvironment() const override;
        [[nodiscard]] gsl::span<ILight *const>          GetLights() const override;
        [[nodiscard]] const ILight                     *GetSkyLight() const noexcept override;
        [[nodiscard]] const ILight                     *GetLightAtHit(const HitRecord &hit) const override;
        [[nodiscard]] std::optional<HitRecord>          IntersectScene(const GfVec3f &rayOrigin,
                                                                       const GfVec3f &rayDir) const override;
        [[nodiscard]] const ImageTextureSamplerFactory *GetTextureSamplerFactory() const override;

        [[nodiscard]] bool               Intersect(const GfVec3f &rayOrigin, const GfVec3f &rayDir, SurfaceHit &hit,
                                                   const IRenderJob &job) const;
        [[nodiscard]] const PhysicalSky *GetPhysicalSky() const noexcept;

      private:
        struct MeshInstance
        {
            IMesh                      *mesh{nullptr};
            const PreviewSurfaceParams *materialParams{nullptr};
            IMaterial                  *shader{nullptr};
            BVH                         bvh;
            GfMatrix4f                  transform;
            GfMatrix4f                  invTransform;
            GfRange3f                   bounds;
            GfVec3f                     centroid;
        };

        struct TLASNode
        {
            GfRange3f bounds;
            int       leftChild{0};
            int       instanceCount{0};
        };

        struct RenderState
        {
            std::vector<MeshInstance>                                           Instances;
            std::unordered_map<SdfPath, std::unique_ptr<ILight>, SdfPath::Hash> LightMap;
            std::vector<ILight *>                                               ActiveLights;
            IEnvironment                                                       *ActiveEnvironment{nullptr};
            std::vector<TLASNode>                                               TlasNodes;
            std::vector<int>                                                    TlasInstanceIndices;
            std::unordered_map<SdfPath, std::unique_ptr<PreviewSurfaceMaterial>, SdfPath::Hash> MaterialShaders;
            std::unique_ptr<PhysicalSky>                                                        PhysicalSkyLight;
            std::unique_ptr<ImageTextureSamplerFactory>                                         TextureSamplerFactory{
                std::make_unique<ImageTextureSamplerFactory>()};
        };

        void _BuildTLAS(RenderState &state, const IRenderJob &job);
        void _SubdivideTLAS(RenderState &state, int nodeIdx, int start, int end, const IRenderJob &job);
        [[nodiscard]] static GfRange3f _TransformBounds(const GfRange3f &bounds, const GfMatrix4f &matrix);

        std::map<SdfPath, IMesh *>              _meshes;
        std::map<SdfPath, IInstancer *>         _instancers;
        std::map<SdfPath, PreviewSurfaceParams> _materialParams;
        std::map<SdfPath, LightFactoryInput>    _lightInputs;
        mutable std::recursive_mutex            _sceneLock;
        std::optional<RenderState>              _renderState;
    };

} // namespace Restir