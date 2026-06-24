#include "default_material.h"

#include "pxr/base/gf/vec3f.h"
#include "shading_helpers.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

PXR_NAMESPACE_USING_DIRECTIVE

namespace Restir
{

    namespace
    {

        constexpr float kPi{3.14159265358979323846f};

        struct ONB
        {
            GfVec3f n, t, b;

            [[nodiscard]] static ONB fromNormal(const GfVec3f &normal) noexcept
            {
                const float sign{(normal[2] >= 0.0f) ? 1.0f : -1.0f};
                const float a{-1.0f / (sign + normal[2])};
                const float bb{normal[0] * normal[1] * a};
                return {normal,
                        {1.0f + sign * normal[0] * normal[0] * a, sign * bb, -sign * normal[0]},
                        {bb, sign + normal[1] * normal[1] * a, -normal[1]}};
            }

            [[nodiscard]] GfVec3f toWorld(const GfVec3f &v) const noexcept
            {
                return v[0] * t + v[1] * b + v[2] * n;
            }
        };

        [[nodiscard]] GfVec3f sampleCosineHemisphere(float u1, float u2) noexcept
        {
            const float sinTheta{std::sqrt(u1)};
            const float phi{2.0f * kPi * u2};
            return {sinTheta * std::cos(phi), sinTheta * std::sin(phi), std::sqrt(std::max(0.0f, 1.0f - u1))};
        }

    } // namespace

    DefaultMaterial &DefaultMaterial::Instance() noexcept
    {
        static DefaultMaterial s_instance;
        return s_instance;
    }

    BSDFClosure DefaultMaterial::GetClosure(const HitRecord &hit) const
    {
        return BSDFClosure{
            .BaseColor = hit.Albedo,
            .Normal    = hit.Normal,
            .Metallic  = 0.0f,
            .Roughness = 1.0f,
        };
    }

    BounceSampleResult DefaultMaterial::SampleBounce(const ShadingPoint &surface, const GfVec3f &hitPos,
                                                     const GfVec3f & /*rayDir*/, const BounceConfig & /*config*/,
                                                     [[maybe_unused]] BounceState &state, Rng &rng) const
    {
        const ONB             onb{ONB::fromNormal(surface.shadingNormal)};
        const GfVec3f         wiL{sampleCosineHemisphere(rng.NextFloat(), rng.NextFloat())};
        const GfVec3f         wi{onb.toWorld(wiL)};
        const float           nDotL{wiL[2]};
        const float           rawPdf{nDotL / kPi};
        const float           safePdf{std::max(rawPdf, 1e-6f)};
        const float           throughputScale{rawPdf / safePdf};
        const SampledSpectrum throughputMul{RGBToSpectrum(surface.c.BaseColor, surface.lambda) * throughputScale};
        const SampledSpectrum integrandMul{RGBToSpectrum(surface.c.BaseColor, surface.lambda) * (nDotL / kPi)};

        return BsdfBounceSample{
            .NextRay                 = {hitPos + surface.shadingNormal * 1e-4f, wi},
            .ThroughputMul           = throughputMul,
            .ThroughputIntegrandMul  = integrandMul,
            .BsdfPdf                 = {safePdf, PdfSpace::SolidAngle},
            .ImpossibleNEEConnection = false,
        };
    }

} // namespace Restir
