#pragma once

#include "hit_record.h"
#include "math/pdf.h"
#include "pxr/base/gf/vec3f.h"
#include "ray.h"
#include "spectrum.h"

#include <optional>
#include <variant>

PXR_NAMESPACE_USING_DIRECTIVE

namespace Restir
{

    // Per-pixel material parameters resolved from the scene.
    struct BSDFClosure
    {
        GfVec3f BaseColor{0.18f, 0.18f, 0.18f};
        GfVec3f Emission{0.0f, 0.0f, 0.0f};
        GfVec3f Normal{0.0f, 0.0f, 1.0f};
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

    // Interface for evaluating a BSDF for direct lighting.
    // Stateful: constructed with the BSDFClosure for the current surface point.
    class IBSDF
    {
      public:
        virtual ~IBSDF() = default;

        [[nodiscard]] virtual GfVec3f Eval(const GfVec3f &shadingNormal, const GfVec3f &wo,
                                           const GfVec3f &wi) const noexcept = 0;

        [[nodiscard]] virtual float Pdf(const GfVec3f &shadingNormal, const GfVec3f &wo,
                                        const GfVec3f &wi) const noexcept = 0;
    };

    // Result of a bounce sample — next ray plus throughput multiplier.
    struct BsdfBounceSample
    {
        Ray             NextRay{};
        SampledSpectrum ThroughputMul{1.0f};
        SampledSpectrum ThroughputIntegrandMul{0.0f};
        Pdf             BsdfPdf{0.f, PdfSpace::SolidAngle};
        bool            ImpossibleNEEConnection{false};
        bool            SkipRoulette{false};
    };

    enum class BounceSampleError
    {
        MaxReflectionBouncesReached,
        MaxRefractionBouncesReached,
    };

    using BounceSampleResult = std::variant<BsdfBounceSample, BounceSampleError>;

    // Surface shading state at an intersection point.
    // hit and rayDir are intentionally absent — they live in RayIntersection and
    // are passed explicitly to SampleBounce to avoid duplication.
    struct ShadingPoint
    {
        const IBSDF              &bsdf;
        const BSDFClosure        &c;
        const GfVec3f            &shadingNormal;
        const SampledWavelengths &lambda;
        bool                      isInside;
    };

    // BSDF bounce result augmented with the scene intersection of the sampled ray.
    struct BsdfBounceConnection
    {
        BsdfBounceSample         Bounce{};
        std::optional<HitRecord> Hit{};
    };

    // Bounce-loop constants.
    struct BounceConfig
    {
        int effectiveMaxRefl;
        int effectiveMaxRefr;
    };

    // Per-path mutable bounce counters.
    struct BounceState
    {
        int reflectionBounces{0};
        int refractionBounces{0};
    };

} // namespace Restir
