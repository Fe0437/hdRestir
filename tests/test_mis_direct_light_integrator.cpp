#include "light_sampler.h"
#include "material.h"
#include "mis_direct_light_integrator.h"
#include "scene_interface.h"

#include <cassert>
#include <cmath>
#include <memory>
#include <mutex>

namespace
{

    class DummyBSDF final : public Restir::IBSDF
    {
      public:
        [[nodiscard]] GfVec3f Eval(const GfVec3f & /*shadingNormal*/, const GfVec3f & /*wo*/,
                                   const GfVec3f & /*wi*/) const noexcept override
        {
            return GfVec3f{1.0f, 1.0f, 1.0f};
        }

        [[nodiscard]] float Pdf(const GfVec3f & /*shadingNormal*/, const GfVec3f & /*wo*/,
                                const GfVec3f & /*wi*/) const noexcept override
        {
            return 0.5f;
        }
    };

    class DummyMaterial final : public Restir::IMaterial
    {
      public:
        explicit DummyMaterial(const GfVec3f &emission) : _emission{emission} {}

        [[nodiscard]] Restir::BSDFClosure GetClosure(const Restir::HitRecord &hit) const override
        {
            Restir::BSDFClosure closure{};
            closure.Normal   = hit.Normal;
            closure.Emission = _emission;
            closure.Opacity  = 1.0f;
            return closure;
        }

        [[nodiscard]] std::unique_ptr<Restir::IBSDF> CreateBSDF(Restir::BSDFClosure && /*c*/) const override
        {
            return std::make_unique<DummyBSDF>();
        }

        [[nodiscard]] Restir::BounceSampleResult SampleBounce(const Restir::ShadingPoint & /*surface*/,
                                                              const Restir::BounceConfig & /*config*/,
                                                              Restir::BounceState & /*state*/,
                                                              Restir::Rng & /*rng*/) const override
        {
            return Restir::BounceSampleError::MaxReflectionBouncesReached;
        }

      private:
        GfVec3f _emission{};
    };

    class NoLightSampler final : public Restir::ILightSampler
    {
      public:
        [[nodiscard]] std::unique_ptr<Restir::ILightSampler> CloneAs() const override
        {
            return std::make_unique<NoLightSampler>();
        }

        [[nodiscard]] std::optional<Restir::LightCandidate> ProposeCandidate(const GfVec3f & /*hitPos*/,
                                                                             Restir::Rng & /*rng*/) const override
        {
            return std::nullopt;
        }

        [[nodiscard]] bool IsConsideringSkyLight() const noexcept override
        {
            return true;
        }
    };

    class DummyScene final : public Restir::IScene
    {
      public:
        explicit DummyScene(const Restir::IMaterial *material) : _material{material} {}

        std::recursive_mutex &GetSceneLock() override
        {
            return _sceneLock;
        }

        void BuildRenderState(const Restir::SceneBuildRenderStateConfig & /*config*/,
                              const Restir::IRenderJob & /*job*/) override
        {
        }

        [[nodiscard]] const Restir::IMaterial &GetMaterial(int /*matId*/) const override
        {
            return *_material;
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

        [[nodiscard]] const Restir::ILight *GetLightAtHit(const Restir::HitRecord & /*hit*/) const override
        {
            return nullptr;
        }

        [[nodiscard]] std::optional<Restir::HitRecord> IntersectScene(const GfVec3f & /*rayOrigin*/,
                                                                      const GfVec3f & /*rayDir*/) const override
        {
            return std::nullopt;
        }

        [[nodiscard]] const Restir::ImageTextureSamplerFactory *GetTextureSamplerFactory() const override
        {
            return nullptr;
        }

      private:
        const Restir::IMaterial     *_material{nullptr};
        mutable std::recursive_mutex _sceneLock{};
    };

    void TestPendingConnectionIsConsumedAndReset()
    {
        Restir::MisDirectLightIntegrator integrator{
            Restir::NotNullUniquePtr<Restir::ILightSampler>{std::make_unique<NoLightSampler>()}};

        DummyMaterial material{GfVec3f{2.0f, 2.0f, 2.0f}};
        DummyScene    scene{&material};

        Restir::HitRecord hit{};
        hit.Position = GfVec3f{0.0f, 0.0f, 0.0f};
        hit.Normal   = GfVec3f{0.0f, 0.0f, 1.0f};
        hit.MatId    = 0;

        Restir::BSDFClosure closure{};
        closure.Normal = hit.Normal;

        DummyBSDF                  bsdf{};
        const SampledWavelengths   lambda{SampledWavelengths::SampleUniform(0.37f)};
        const GfVec3f              rayDir{0.0f, 0.0f, -1.0f};
        const GfVec3f              shadingNormal{0.0f, 0.0f, 1.0f};
        const Restir::ShadingPoint surface{hit, bsdf, closure, shadingNormal, rayDir, lambda, false};

        Restir::HitRecord lightHit{};
        lightHit.Position = GfVec3f{0.0f, 0.0f, 1.0f};
        lightHit.Normal   = GfVec3f{0.0f, 0.0f, -1.0f};
        lightHit.MatId    = 0;

        const std::optional<Restir::BsdfBounceConnection> connection{Restir::BsdfBounceConnection{
            .Bounce =
                Restir::BsdfBounceSample{
                    .NextRay                 = Restir::Ray{hit.Position, GfVec3f{0.0f, 0.0f, 1.0f}},
                    .ThroughputMul           = SampledSpectrum{1.0f},
                    .BsdfPdf                 = Restir::Pdf{0.25f, Restir::PdfSpace::SolidAngle},
                    .ImpossibleNEEConnection = false,
                },
            .Hit = lightHit,
        }};

        Restir::Rng           rng{123u};
        const SampledSpectrum firstEval{integrator.Li(surface, scene, rng, connection)};
        assert(!firstEval.IsBlack());

        // Stateless call without a connection should not include BSDF-hit contribution.
        const SampledSpectrum secondEval{integrator.Li(surface, scene, rng, std::nullopt)};
        assert(secondEval.IsBlack());
    }

    void TestBsdfConnectionThroughputScalesContribution()
    {
        Restir::MisDirectLightIntegrator integrator{
            Restir::NotNullUniquePtr<Restir::ILightSampler>{std::make_unique<NoLightSampler>()}};

        DummyMaterial material{GfVec3f{4.0f, 1.0f, 0.5f}};
        DummyScene    scene{&material};

        Restir::HitRecord hit{};
        hit.Position = GfVec3f{0.0f, 0.0f, 0.0f};
        hit.Normal   = GfVec3f{0.0f, 0.0f, 1.0f};
        hit.MatId    = 0;

        Restir::BSDFClosure closure{};
        closure.Normal = hit.Normal;

        DummyBSDF                  bsdf{};
        const SampledWavelengths   lambda{SampledWavelengths::SampleUniform(0.63f)};
        const GfVec3f              rayDir{0.0f, 0.0f, -1.0f};
        const GfVec3f              shadingNormal{0.0f, 0.0f, 1.0f};
        const Restir::ShadingPoint surface{hit, bsdf, closure, shadingNormal, rayDir, lambda, false};

        Restir::HitRecord lightHit{};
        lightHit.Position = GfVec3f{0.0f, 0.0f, 1.0f};
        lightHit.Normal   = GfVec3f{0.0f, 0.0f, -1.0f};
        lightHit.MatId    = 0;

        auto evalWithThroughput = [&](float throughputScale)
        {
            const std::optional<Restir::BsdfBounceConnection> connection{Restir::BsdfBounceConnection{
                .Bounce =
                    Restir::BsdfBounceSample{
                        .NextRay                 = Restir::Ray{hit.Position, GfVec3f{0.0f, 0.0f, 1.0f}},
                        .ThroughputMul           = SampledSpectrum{throughputScale},
                        .BsdfPdf                 = Restir::Pdf{0.25f, Restir::PdfSpace::SolidAngle},
                        .ImpossibleNEEConnection = false,
                    },
                .Hit = lightHit,
            }};
            Restir::Rng                                       rng{777u};
            return integrator.Li(surface, scene, rng, connection).Average();
        };

        const float fullContribution{evalWithThroughput(1.0f)};
        const float halfContribution{evalWithThroughput(0.5f)};

        assert(fullContribution > 0.0f);
        assert(std::fabs(halfContribution - 0.5f * fullContribution) < 1e-4f * fullContribution);
    }

} // namespace

int main()
{
    TestPendingConnectionIsConsumedAndReset();
    TestBsdfConnectionThroughputScalesContribution();
    return 0;
}
