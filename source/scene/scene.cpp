#include "scene.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace Restir
{

    namespace
    {

        bool IntersectAABB(const GfVec3f &rayOrigin, const GfVec3f &rayDir, const GfRange3f &range, float &tMinHit)
        {
            if (range.IsEmpty())
                return false;
            const GfVec3f &min = range.GetMin();
            const GfVec3f &max = range.GetMax();

            float tmin = -1e30f;
            float tmax = 1e30f;

            for (int i = 0; i < 3; ++i)
            {
                if (std::abs(rayDir[i]) < 1e-8f)
                {
                    if (rayOrigin[i] < min[i] || rayOrigin[i] > max[i])
                        return false;
                }
                else
                {
                    const float invDir = 1.0f / rayDir[i];
                    float       t1     = (min[i] - rayOrigin[i]) * invDir;
                    float       t2     = (max[i] - rayOrigin[i]) * invDir;
                    if (t1 > t2)
                        std::swap(t1, t2);
                    tmin = std::max(tmin, t1);
                    tmax = std::min(tmax, t2);
                    if (tmin > tmax)
                        return false;
                }
            }

            tMinHit = tmin;
            return tmax > 0.0f && tmax > 1e-4f;
        }

    } // namespace

    void Scene::AddMesh(const SdfPath &id, IMesh *mesh)
    {
        std::lock_guard<std::recursive_mutex> lock(_sceneLock);
        _meshes[id] = mesh;
    }

    void Scene::RemoveMesh(const SdfPath &id)
    {
        std::lock_guard<std::recursive_mutex> lock(_sceneLock);
        _meshes.erase(id);
    }

    void Scene::AddInstancer(const SdfPath &id, IInstancer *instancer)
    {
        std::lock_guard<std::recursive_mutex> lock(_sceneLock);
        _instancers[id] = instancer;
    }

    void Scene::RemoveInstancer(const SdfPath &id)
    {
        std::lock_guard<std::recursive_mutex> lock(_sceneLock);
        _instancers.erase(id);
    }

    void Scene::SetMaterialParams(const SdfPath &id, const PreviewSurfaceParams &params)
    {
        std::lock_guard<std::recursive_mutex> lock(_sceneLock);
        _materialParams[id] = params;
    }

    void Scene::RemoveMaterialParams(const SdfPath &id)
    {
        std::lock_guard<std::recursive_mutex> lock(_sceneLock);
        _materialParams.erase(id);
    }

    void Scene::SetLightFactoryInput(const SdfPath &id, const LightFactoryInput &input)
    {
        std::lock_guard<std::recursive_mutex> lock(_sceneLock);
        _lightInputs[id] = input;
    }

    void Scene::RemoveLightFactoryInput(const SdfPath &id)
    {
        std::lock_guard<std::recursive_mutex> lock(_sceneLock);
        _lightInputs.erase(id);
    }

    GfRange3f Scene::_TransformBounds(const GfRange3f &bounds, const GfMatrix4f &matrix)
    {
        if (bounds.IsEmpty())
            return bounds;

        const GfVec3f min{bounds.GetMin()};
        const GfVec3f max{bounds.GetMax()};
        const GfVec3f corners[8]{GfVec3f(min[0], min[1], min[2]), GfVec3f(max[0], min[1], min[2]),
                                 GfVec3f(min[0], max[1], min[2]), GfVec3f(max[0], max[1], min[2]),
                                 GfVec3f(min[0], min[1], max[2]), GfVec3f(max[0], min[1], max[2]),
                                 GfVec3f(min[0], max[1], max[2]), GfVec3f(max[0], max[1], max[2])};

        GfRange3f result;
        for (const GfVec3f &corner : corners)
        {
            result.ExtendBy(matrix.Transform(corner));
        }
        return result;
    }

    void Scene::BuildRenderState(const SceneBuildRenderStateConfig &config, const IRenderJob &job)
    {
        std::lock_guard<std::recursive_mutex> lock(_sceneLock);

        if (!_renderState.has_value())
        {
            _renderState.emplace();
        }

        auto &state{*_renderState};
        state.Instances.clear();
        state.ActiveLights.clear();
        state.ActiveEnvironment = nullptr;
        state.TlasNodes.clear();
        state.TlasInstanceIndices.clear();

        {
            std::unordered_set<SdfPath, SdfPath::Hash> currentLightIds;
            for (const auto &[id, _] : _lightInputs)
                currentLightIds.insert(id);
            for (auto it = state.LightMap.begin(); it != state.LightMap.end();)
            {
                if (currentLightIds.find(it->first) == currentLightIds.end())
                {
                    it = state.LightMap.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }

        for (auto it = state.MaterialShaders.begin(); it != state.MaterialShaders.end();)
        {
            if (_materialParams.find(it->first) == _materialParams.end())
            {
                it = state.MaterialShaders.erase(it);
            }
            else
            {
                ++it;
            }
        }

        for (const auto &[id, input] : _lightInputs)
        {
            if (state.LightMap.find(id) == state.LightMap.end())
            {
                auto light{MakeLight(input.Type, input.Params)};
                light->Prepare();
                state.LightMap.emplace(id, std::move(light));
            }
            else
            {
                state.LightMap[id]->SetParams(input.Params);
                state.LightMap[id]->Prepare();
            }
            state.ActiveLights.push_back(state.LightMap[id].get());
        }

        auto appendMeshInstance = [&](IMesh *mesh, const MeshSubset &subset, const GfMatrix4f &transform)
        {
            MeshInstance instance{};
            instance.mesh = mesh;
            instance.bvh  = subset.bvh;

            const auto materialIt{_materialParams.find(subset.materialId)};
            if (materialIt != _materialParams.end())
            {
                instance.materialParams = &materialIt->second;
                auto [shaderIt, inserted]{state.MaterialShaders.try_emplace(subset.materialId)};
                if (inserted)
                {
                    shaderIt->second = std::make_unique<PreviewSurfaceMaterial>(
                        *instance.materialParams,
                        gsl::not_null<const ImageTextureSamplerFactory *>{state.TextureSamplerFactory.get()});
                }
                else
                {
                    shaderIt->second->SetParams(*instance.materialParams);
                }
                instance.shader = shaderIt->second.get();
            }
            else
            {
                instance.shader = &DefaultMaterial::Instance();
            }

            instance.transform    = transform;
            instance.invTransform = instance.transform.GetInverse();
            instance.bounds       = _TransformBounds(subset.range, instance.transform);
            instance.centroid     = (instance.bounds.GetMin() + instance.bounds.GetMax()) * 0.5f;
            state.Instances.push_back(instance);
        };

        for (const auto &[_, mesh] : _meshes)
        {
            if (job.IsCancelled())
                return;
            if (!mesh->IsVisible())
                continue;

            if (!mesh->GetInstancerId().IsEmpty())
            {
                const auto instancerIt{_instancers.find(mesh->GetInstancerId())};
                if (instancerIt != _instancers.end() && instancerIt->second != nullptr)
                {
                    const VtMatrix4dArray transforms{instancerIt->second->ComputeInstanceTransforms(mesh->GetId())};
                    for (const auto &transform : transforms)
                    {
                        for (const auto &subset : mesh->GetSubsets())
                        {
                            appendMeshInstance(mesh, subset, GfMatrix4f(transform) * mesh->GetTransform());
                        }
                    }
                }
                continue;
            }

            for (const auto &subset : mesh->GetSubsets())
            {
                appendMeshInstance(mesh, subset, mesh->GetTransform());
            }
        }

        _BuildTLAS(state, job);
        if (job.IsCancelled())
            return;

        if (config.EnablePhysicalSky)
        {
            const float   sunAngle{(config.PhysicalSkyTime - 12.0f) / 12.0f * float(M_PI)};
            const GfVec3f sunDir{
                GfVec3f{std::sin(sunAngle) * std::cos(0.5f), std::cos(sunAngle), std::sin(sunAngle) * std::sin(0.5f)}
                    .GetNormalized()};
            state.PhysicalSkyLight  = std::make_unique<PhysicalSky>(sunDir);
            state.ActiveEnvironment = state.PhysicalSkyLight.get();
        }
        else
        {
            state.PhysicalSkyLight.reset();
            state.ActiveEnvironment = nullptr;
            for (auto &[_, light] : state.LightMap)
            {
                IEnvironment *environment{dynamic_cast<IEnvironment *>(light.get())};
                if (environment != nullptr)
                {
                    state.ActiveEnvironment = environment;
                    break;
                }
            }
        }
    }

    const IMaterial &Scene::GetMaterial(int matId) const
    {
        if (!_renderState.has_value())
            return DefaultMaterial::Instance();
        const auto &instances{_renderState->Instances};
        if (matId < 0 || matId >= static_cast<int>(instances.size()))
            return DefaultMaterial::Instance();
        return instances[matId].shader != nullptr ? *instances[matId].shader : DefaultMaterial::Instance();
    }

    const IEnvironment *Scene::GetEnvironment() const
    {
        return _renderState.has_value() ? _renderState->ActiveEnvironment : nullptr;
    }

    gsl::span<ILight *const> Scene::GetLights() const
    {
        if (!_renderState.has_value())
            return {};
        return gsl::span<ILight *const>{_renderState->ActiveLights};
    }

    const ILight *Scene::GetSkyLight() const noexcept
    {
        return _renderState.has_value() ? _renderState->PhysicalSkyLight.get() : nullptr;
    }

    const ImageTextureSamplerFactory *Scene::GetTextureSamplerFactory() const
    {
        return _renderState.has_value() ? _renderState->TextureSamplerFactory.get() : nullptr;
    }

    const ILight *Scene::GetLightAtHit(const HitRecord &hit) const
    {
        if (!_renderState.has_value())
        {
            return nullptr;
        }

        const auto &state{*_renderState};
        if (hit.PrimId < 0 || hit.PrimId >= static_cast<int>(state.Instances.size()))
        {
            return nullptr;
        }

        const MeshInstance &instance{state.Instances[hit.PrimId]};
        if (instance.mesh == nullptr)
        {
            return nullptr;
        }

        const auto lightIt{state.LightMap.find(instance.mesh->GetId())};
        if (lightIt == state.LightMap.end())
        {
            return nullptr;
        }

        return lightIt->second.get();
    }

    const PhysicalSky *Scene::GetPhysicalSky() const noexcept
    {
        return _renderState.has_value() ? _renderState->PhysicalSkyLight.get() : nullptr;
    }

    void Scene::_BuildTLAS(RenderState &state, const IRenderJob &job)
    {
        state.TlasNodes.clear();
        state.TlasInstanceIndices.clear();
        if (state.Instances.empty() || job.IsCancelled())
            return;

        state.TlasInstanceIndices.resize(state.Instances.size());
        for (size_t i = 0; i < state.Instances.size(); ++i)
        {
            state.TlasInstanceIndices[i] = static_cast<int>(i);
        }

        state.TlasNodes.reserve(state.Instances.size() * 2);
        state.TlasNodes.push_back(TLASNode{});
        _SubdivideTLAS(state, 0, 0, static_cast<int>(state.Instances.size()), job);
    }

    void Scene::_SubdivideTLAS(RenderState &state, int nodeIdx, int start, int end, const IRenderJob &job)
    {
        if (job.IsCancelled())
            return;

        const int leftChildIdx{static_cast<int>(state.TlasNodes.size())};
        state.TlasNodes.push_back(TLASNode{});
        state.TlasNodes.push_back(TLASNode{});
        state.TlasNodes[nodeIdx].leftChild     = leftChildIdx;
        state.TlasNodes[nodeIdx].instanceCount = 0;

        state.TlasNodes[nodeIdx].bounds.SetEmpty();
        for (int i = start; i < end; ++i)
        {
            const auto &bounds{state.Instances[state.TlasInstanceIndices[i]].bounds};
            state.TlasNodes[nodeIdx].bounds.ExtendBy(bounds.GetMin());
            state.TlasNodes[nodeIdx].bounds.ExtendBy(bounds.GetMax());
        }

        const int count{end - start};
        if (count <= 2)
        {
            state.TlasNodes[nodeIdx].leftChild     = -start - 1;
            state.TlasNodes[nodeIdx].instanceCount = count;
            return;
        }

        const GfVec3f size{state.TlasNodes[nodeIdx].bounds.GetSize()};
        int           axis{0};
        if (size[1] > size[0])
            axis = 1;
        if (size[2] > size[axis])
            axis = 2;

        const float splitPos{state.TlasNodes[nodeIdx].bounds.GetMin()[axis] + size[axis] * 0.5f};

        int i{start};
        int j{end - 1};
        while (i <= j)
        {
            if (state.Instances[state.TlasInstanceIndices[i]].centroid[axis] < splitPos)
            {
                ++i;
            }
            else
            {
                std::swap(state.TlasInstanceIndices[i], state.TlasInstanceIndices[j]);
                --j;
            }
        }

        if (i == start || i == end)
            i = start + count / 2;

        _SubdivideTLAS(state, leftChildIdx, start, i, job);
        _SubdivideTLAS(state, leftChildIdx + 1, i, end, job);
    }

    bool Scene::Intersect(const GfVec3f &rayOrigin, const GfVec3f &rayDir, SurfaceHit &hit, const IRenderJob &job) const
    {
        if (!_renderState.has_value() || _renderState->TlasNodes.empty() || job.IsCancelled())
            return false;

        int stack[64];
        int stackPtr      = 0;
        stack[stackPtr++] = 0;

        bool wasHit = false;

        while (stackPtr > 0)
        {
            if (job.IsCancelled())
                return false;

            const int   nodeIdx{stack[--stackPtr]};
            const auto &node{_renderState->TlasNodes[nodeIdx]};
            float       tAabb = 0.0f;
            if (!IntersectAABB(rayOrigin, rayDir, node.bounds, tAabb) || tAabb > hit.t)
                continue;

            if (node.leftChild < 0)
            {
                const int start{-node.leftChild - 1};
                for (int i = 0; i < node.instanceCount; ++i)
                {
                    const int     instanceIdx{_renderState->TlasInstanceIndices[start + i]};
                    const auto   &instance{_renderState->Instances[instanceIdx]};
                    const GfVec3f objRayOrigin{instance.invTransform.Transform(rayOrigin)};
                    const GfVec3f objRayDir{instance.invTransform.TransformDir(rayDir)};
                    float         instT{hit.t};
                    GfVec3f       instNormal{};
                    GfVec2f       instUv{};
                    GfVec3f       instSmoothNormal{};
                    GfVec3f       instDpdu{};
                    GfVec3f       instDpdv{};
                    GfVec3f       instSmoothColor{};
                    int           matIdx{-1};
                    if (!instance.bvh.Intersect(objRayOrigin, objRayDir, instT, instNormal, instUv, instSmoothNormal,
                                                instDpdu, instDpdv, instSmoothColor, matIdx))
                    {
                        continue;
                    }

                    if (instT >= hit.t)
                        continue;

                    hit.instanceIdx  = instanceIdx;
                    hit.shader       = instance.shader;
                    hit.t            = instT;
                    hit.normal       = instance.transform.TransformDir(instNormal).GetNormalized();
                    hit.smoothNormal = instance.transform.TransformDir(instSmoothNormal).GetNormalized();
                    hit.dpdu         = instance.transform.TransformDir(instDpdu).GetNormalized();
                    hit.dpdv         = instance.transform.TransformDir(instDpdv).GetNormalized();
                    hit.uv           = instUv;
                    hit.baseColor    = instSmoothColor;

                    if (instance.materialParams)
                    {
                        const auto &material{*instance.materialParams};
                        hit.baseColor            = material.DiffuseColor;
                        hit.metallic             = material.Metallic;
                        hit.roughness            = material.Roughness;
                        hit.specularColor        = material.SpecularColor;
                        hit.specular             = material.Specular;
                        hit.opacity              = material.Opacity;
                        hit.ior                  = material.Ior;
                        hit.transmission         = material.Transmission;
                        hit.transmissionColor    = material.TransmissionColor;
                        hit.emission             = material.EmissionColor * material.Emission;
                        hit.diffuseTexture       = material.DiffuseTexture;
                        hit.normalTexture        = material.NormalTexture;
                        hit.metallicTexture      = material.MetallicTexture;
                        hit.roughnessTexture     = material.RoughnessTexture;
                        hit.coat                 = material.Coat;
                        hit.coatColor            = material.CoatColor;
                        hit.coatRoughness        = material.CoatRoughness;
                        hit.coatIor              = material.CoatIor;
                        hit.transmissionDepth    = material.TransmissionDepth;
                        hit.transmissionScatter  = material.TransmissionScatter;
                        hit.sheen                = material.Sheen;
                        hit.sheenColor           = material.SheenColor;
                        hit.sheenRoughness       = material.SheenRoughness;
                        hit.subsurface           = material.Subsurface;
                        hit.subsurfaceColor      = material.SubsurfaceColor;
                        hit.subsurfaceRadius     = material.SubsurfaceRadius;
                        hit.subsurfaceScale      = material.SubsurfaceScale;
                        hit.subsurfaceAnisotropy = material.SubsurfaceAnisotropy;
                        hit.thinWalled           = material.ThinWalled;
                        hit.diffuseRoughness     = material.DiffuseRoughness;
                    }

                    wasHit = true;
                }
            }
            else
            {
                stack[stackPtr++] = node.leftChild + 1;
                stack[stackPtr++] = node.leftChild;
            }
        }

        return wasHit;
    }

    std::optional<HitRecord> Scene::IntersectScene(const GfVec3f &rayOrigin, const GfVec3f &rayDir) const
    {
        SurfaceHit hit{};
        if (!Intersect(rayOrigin, rayDir, hit, GetMainThreadRenderJob()))
        {
            return std::nullopt;
        }

        return HitRecord{
            .Position     = rayOrigin + hit.t * rayDir,
            .Normal       = hit.normal,
            .SmoothNormal = hit.smoothNormal,
            .Dpdu         = hit.dpdu,
            .Dpdv         = hit.dpdv,
            .Uv           = hit.uv,
            .Albedo       = hit.baseColor,
            .Depth        = hit.t,
            .PrimId       = hit.instanceIdx,
            .MatId        = hit.instanceIdx,
        };
    }

} // namespace Restir