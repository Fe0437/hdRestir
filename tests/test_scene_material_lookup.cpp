#include "default_material.h"
#include "ggx.h"
#include "materials/material.h"
#include "pxr/base/gf/vec3f.h"
#include "rng.h"
#include "scene_interface.h"

#include <cassert>
#include <map>
#include <mutex>

PXR_NAMESPACE_USING_DIRECTIVE

namespace
{

    // Minimal concrete material for testing — just needs to compile.
    class StubMaterial final : public Restir::IMaterial
    {
      public:
        [[nodiscard]] Restir::BSDFClosure GetClosure(const Restir::HitRecord &) const override
        {
            return {};
        }
        [[nodiscard]] std::unique_ptr<Restir::IBSDF> CreateBSDF(Restir::BSDFClosure &&c) const override
        {
            return std::make_unique<Restir::GGXBsdf>(std::move(c));
        }
        [[nodiscard]] Restir::BounceSampleResult SampleBounce(const Restir::ShadingPoint &,
                                                              const Restir::BounceConfig &, Restir::BounceState &,
                                                              Restir::Rng &) const override
        {
            return Restir::BounceSampleError::MaxReflectionBouncesReached;
        }
    };

    // Minimal concrete IScene: holds one material at id 0.
    class StubScene final : public Restir::IScene
    {
      public:
        std::recursive_mutex &GetSceneLock() override
        {
            return _sceneLock;
        }
        void BuildRenderState(const Restir::SceneBuildRenderStateConfig &, const Restir::IRenderJob &) override {}
        [[nodiscard]] const Restir::IMaterial &GetMaterial(int matId) const override
        {
            if (matId == 0)
                return _mat;
            return Restir::DefaultMaterial::Instance();
        }
        [[nodiscard]] const Restir::IEnvironment *GetEnvironment() const override
        {
            return nullptr;
        }
        [[nodiscard]] gsl::span<Restir::ILight *const> GetLights() const override
        {
            return {};
        }
        [[nodiscard]] const Restir::ILight *GetSkyLight() const noexcept override
        {
            return nullptr;
        }
        [[nodiscard]] const Restir::ILight *GetLightAtHit(const Restir::HitRecord &) const override
        {
            return nullptr;
        }
        [[nodiscard]] std::optional<Restir::HitRecord> IntersectScene(const GfVec3f &, const GfVec3f &) const override
        {
            return std::nullopt;
        }
        [[nodiscard]] const Restir::ImageTextureSamplerFactory *GetTextureSamplerFactory() const override
        {
            return nullptr;
        }

      private:
        std::recursive_mutex _sceneLock{};
        StubMaterial         _mat;
    };

    void TestLookupFound()
    {
        StubScene scene;
        assert(&scene.GetMaterial(0) != &Restir::DefaultMaterial::Instance());
    }

    void TestLookupMiss()
    {
        StubScene scene;
        assert(&scene.GetMaterial(-1) == &Restir::DefaultMaterial::Instance());
        assert(&scene.GetMaterial(99) == &Restir::DefaultMaterial::Instance());
    }

} // namespace

int main()
{
    TestLookupFound();
    TestLookupMiss();
    return 0;
}
