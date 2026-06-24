#pragma once

#include "hit_record.h"
#include "math/pdf.h"
#include "pxr/base/gf/vec3f.h"
#include "ray.h"
#include "rng.h"
#include "spectrum.h"

#include <cstdint>
#include <gsl/gsl>
#include <memory>
#include <variant>

PXR_NAMESPACE_USING_DIRECTIVE

namespace Restir
{

    // Per-pixel material parameters resolved from the scene. The closure is the
    // only thing the BRDF math kernels read; it is also the boundary between the
    // "material network" world (textures, network parsing) and the "shading" world.
    struct BSDFClosure
    {
        GfVec3f BaseColor{0.18f, 0.18f, 0.18f};
        GfVec3f Emission{0.0f, 0.0f, 0.0f};
        GfVec3f Normal{0.0f, 0.0f, 1.0f}; // shading normal (smooth, normal-mapped), world space
        GfVec3f SpecularColor{1.0f, 1.0f, 1.0f};
        GfVec3f CoatColor{1.0f, 1.0f, 1.0f};
        GfVec3f SheenColor{1.0f, 1.0f, 1.0f};
        GfVec3f SubsurfaceColor{0.7f, 0.2f, 0.2f};
        GfVec3f TransmissionColor{1.0f, 1.0f, 1.0f};
        float   Metallic{0.0f};
        float   Roughness{0.5f};
        float   Ior{1.5f};
        float   Specular{0.5f};
        float   Transmission{0.0f};
        float   TransmissionDepth{0.0f};
        float   Coat{0.0f};
        float   CoatRoughness{0.0f};
        float   CoatIor{1.5f};
        float   Sheen{0.0f};
        float   SheenRoughness{0.3f};
        float   Subsurface{0.0f};
        float   Opacity{1.0f};
        bool    ThinWalled{false};
    };

    // Result of a bounce sample — next ray plus throughput multiplier.
    struct BsdfBounceSample
    {
        Ray             NextRay{};
        SampledSpectrum ThroughputMul{1.0f};          // bsdf*cos/pdf — path continuation weight
        SampledSpectrum ThroughputIntegrandMul{0.0f}; // bsdf*cos — pure integrand, no pdf division
        Pdf             BsdfPdf{0.f, PdfSpace::SolidAngle};
        bool            ImpossibleNEEConnection{false}; // true for perfect mirror/glass/delta bounces
        bool            SkipRoulette{false};
    };

    enum class BounceSampleError
    {
        MaxReflectionBouncesReached,
        MaxRefractionBouncesReached,
    };

    using BounceSampleResult = std::variant<BsdfBounceSample, BounceSampleError>;

    // Interface for evaluating a BSDF for direct lighting (combined diffuse+specular).
    // Stateful: constructed with the BSDFClosure for the current surface point.
    // Separated from GGXBrdf so materials can plug in different implementations.
    class IBSDF
    {
      public:
        virtual ~IBSDF() = default;

        // Combined diffuse+specular BSDF value for the given surface point.
        // wo = outgoing (view) direction, wi = incident (light) direction, both in world space.
        [[nodiscard]] virtual GfVec3f Eval(const GfVec3f &shadingNormal, const GfVec3f &wo,
                                           const GfVec3f &wi) const noexcept = 0;

        // PDF of sampling wi given wo, used for MIS weight.
        [[nodiscard]] virtual float Pdf(const GfVec3f &shadingNormal, const GfVec3f &wo,
                                        const GfVec3f &wi) const noexcept = 0;
    };

    // Surface state at a shading point — shared between NEE and bounce sampling.
    struct ShadingPoint
    {
        const HitRecord          &hit;
        const IBSDF              &bsdf;
        const BSDFClosure        &c;
        const GfVec3f            &shadingNormal;
        const GfVec3f            &rayDir;
        const SampledWavelengths &lambda;
        bool                      isInside;
    };

    // Bounce-loop constants passed alongside ShadingPoint into SampleBounce.
    struct BounceConfig
    {
        int effectiveMaxRefl;
        int effectiveMaxRefr;
    };

    // Per-path mutable bounce counters threaded through each SampleBounce call.
    struct BounceState
    {
        int reflectionBounces{0};
        int refractionBounces{0};
    };

    class IMaterial
    {
      public:
        virtual ~IMaterial() = default;

        [[nodiscard]] virtual BSDFClosure GetClosure(const HitRecord &hit) const = 0;

        // Creates a stateful BSDF bound to the given closure, used for direct-lighting evaluation.
        [[nodiscard]] virtual std::unique_ptr<IBSDF> CreateBSDF(BSDFClosure &&c) const = 0;

        // Convenience: resolves the closure from hit, then creates the BSDF.
        [[nodiscard]] std::unique_ptr<IBSDF> CreateBSDF(const HitRecord &hit) const
        {
            return CreateBSDF(GetClosure(hit));
        }

        // shadingNormal is c.Normal already flipped for isInside by the caller.
        [[nodiscard]] virtual BounceSampleResult SampleBounce(const ShadingPoint &surface, const BounceConfig &config,
                                                              BounceState &state, Rng &rng) const = 0;
    };

} // namespace Restir
