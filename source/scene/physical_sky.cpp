#include "physical_sky.h"

#include "pxr/base/gf/vec2f.h"
#include "pxr/base/gf/vec3f.h"

#include <algorithm>
#include <cmath>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

static GfVec2f RaySphereIntersect(const GfVec3f& r0, const GfVec3f& rd, float sr) {
    float a = GfDot(rd, rd);
    float b = 2.0f * GfDot(rd, r0);
    float c = GfDot(r0, r0) - (sr * sr);
    float d = (b * b) - 4.0f * a * c;
    if (d < 0.0f) return GfVec2f(1e5f, -1e5f);
    return GfVec2f((-b - std::sqrt(d)) / (2.0f * a), (-b + std::sqrt(d)) / (2.0f * a));
}

}  // namespace

namespace Restir {

GfVec3f PhysicalSky::Sample(const GfVec3f& rayDir) const
{
    GfVec3f dir  = rayDir.GetNormalized();
    GfVec3f sDir = _sunDir.GetNormalized();

    float R_planet = 6371e3f;
    float R_atmos  = 6471e3f;

    GfVec3f rayOrigin(0.0f, R_planet + 100.0f, 0.0f);

    GfVec2f isect = RaySphereIntersect(rayOrigin, dir, R_atmos);
    if (isect[1] < 0.0f) return GfVec3f(0.0f);

    float tMin = std::max(0.0f, isect[0]);
    float tMax = isect[1];

    GfVec2f isectPlanet = RaySphereIntersect(rayOrigin, dir, R_planet);
    if (isectPlanet[0] >= 0.0f && isectPlanet[0] < tMax) {
        tMax = isectPlanet[0];
    }

    const int numSteps      = 16;
    const int numLightSteps = 8;
    float stepSize = (tMax - tMin) / float(numSteps);

    GfVec3f betaR(5.5e-6f, 13.0e-6f, 22.4e-6f);
    float betaM = 21e-6f;
    float hR    = 7994.0f;
    float hM    = 1200.0f;

    float currentT      = tMin;
    float opticalDepthR = 0.0f;
    float opticalDepthM = 0.0f;

    GfVec3f totalR(0.0f);
    GfVec3f totalM(0.0f);

    float mu     = GfDot(dir, sDir);
    float phaseR = 3.0f / (16.0f * float(M_PI)) * (1.0f + mu * mu);
    float g      = 0.76f;
    float phaseM = 3.0f / (8.0f * float(M_PI)) * ((1.0f - g * g) * (1.0f + mu * mu)) /
                   ((2.0f + g * g) * std::pow(1.0f + g * g - 2.0f * g * mu, 1.5f));

    for (int i = 0; i < numSteps; ++i) {
        float midT     = currentT + stepSize * 0.5f;
        GfVec3f samplePos = rayOrigin + dir * midT;
        float height   = samplePos.GetLength() - R_planet;

        if (height < 0.0f) break;

        float hr = std::exp(-height / hR) * stepSize;
        float hm = std::exp(-height / hM) * stepSize;
        opticalDepthR += hr;
        opticalDepthM += hm;

        GfVec2f isectSun  = RaySphereIntersect(samplePos, sDir, R_atmos);
        float sunTMax     = isectSun[1];
        float sunStepSize = sunTMax / float(numLightSteps);
        float sunCurrentT = 0.0f;
        float sunOpticalDepthR = 0.0f;
        float sunOpticalDepthM = 0.0f;

        bool inShadow = false;
        GfVec2f sunIsectPlanet = RaySphereIntersect(samplePos, sDir, R_planet);
        if (sunIsectPlanet[0] > 0.0f) {
            inShadow = true;
        } else {
            for (int j = 0; j < numLightSteps; ++j) {
                float sunMidT   = sunCurrentT + sunStepSize * 0.5f;
                GfVec3f sunSamplePos = samplePos + sDir * sunMidT;
                float sunHeight = sunSamplePos.GetLength() - R_planet;
                if (sunHeight < 0.0f) break;
                sunOpticalDepthR += std::exp(-sunHeight / hR) * sunStepSize;
                sunOpticalDepthM += std::exp(-sunHeight / hM) * sunStepSize;
                sunCurrentT += sunStepSize;
            }
        }

        if (!inShadow) {
            GfVec3f tau = betaR * (opticalDepthR + sunOpticalDepthR) +
                          GfVec3f(betaM * 1.1f) * (opticalDepthM + sunOpticalDepthM);
            GfVec3f attenuation(std::exp(-tau[0]), std::exp(-tau[1]), std::exp(-tau[2]));
            totalR += attenuation * hr;
            totalM += attenuation * hm;
        }
        currentT += stepSize;
    }

    GfVec3f sunIntensity(20.0f);
    GfVec3f color = GfCompMult(GfCompMult(totalR, betaR) * phaseR + totalM * betaM * phaseM,
                               sunIntensity);

    if (isectPlanet[0] >= 0.0f && isectPlanet[0] < tMax + stepSize) {
        color = color * 0.5f;
    }

    float sunAngularRadius = 0.00465f;
    if (std::acos(std::clamp(mu, -1.0f, 1.0f)) < sunAngularRadius && isectPlanet[0] < 0.0f) {
        GfVec3f tau = betaR * opticalDepthR + GfVec3f(betaM * 1.1f) * opticalDepthM;
        GfVec3f attenuation(std::exp(-tau[0]), std::exp(-tau[1]), std::exp(-tau[2]));
        color += GfCompMult(sunIntensity, attenuation) * 10.0f;
    }

    return color;
}

std::optional<PhysicalSky::SunSample> PhysicalSky::SampleDirect() const
{
    if (_sunDir[1] < -0.05f) return std::nullopt;
    GfVec3f color = GetSunTransmittance() * 20.0f;
    if (color[0] <= 0.0f && color[1] <= 0.0f && color[2] <= 0.0f) return std::nullopt;
    return SunSample{.Dir = _sunDir, .Color = color};
}

std::optional<LightSample> PhysicalSky::SampleLight(
    const GfVec3f& /*hitPos*/, Rng& /*rng*/) const
{
    const std::optional<SunSample> sun{SampleDirect()};
    if (!sun.has_value()) return std::nullopt;
    return LightSample{sun->Dir, sun->Color, -sun->Dir, 1e30f, Pdf{1.f, PdfSpace::SolidAngle}};
}

Pdf PhysicalSky::EvalPdf(const GfVec3f& /*hitPos*/, const GfVec3f& /*dir*/,
                          float /*dist*/, const GfVec3f& /*lightNormal*/) const
{
    return {0.f, PdfSpace::Area};
}

GfVec3f PhysicalSky::GetSunTransmittance() const
{
    if (_sunDir[1] < 0.0f) return GfVec3f(0.0f);
    float R_planet = 6371e3f;
    float R_atmos  = 6471e3f;
    GfVec3f rayOrigin(0.0f, R_planet + 100.0f, 0.0f);
    GfVec2f isectSun = RaySphereIntersect(rayOrigin, _sunDir, R_atmos);

    float sunTMax            = isectSun[1];
    const int numLightSteps  = 16;
    float sunStepSize        = sunTMax / float(numLightSteps);
    float sunCurrentT        = 0.0f;
    float sunOpticalDepthR   = 0.0f;
    float sunOpticalDepthM   = 0.0f;

    float hR = 7994.0f;
    float hM = 1200.0f;
    GfVec3f betaR(5.5e-6f, 13.0e-6f, 22.4e-6f);
    float betaM = 21e-6f;

    for (int j = 0; j < numLightSteps; ++j) {
        float sunMidT        = sunCurrentT + sunStepSize * 0.5f;
        GfVec3f sunSamplePos = rayOrigin + _sunDir * sunMidT;
        float sunHeight      = sunSamplePos.GetLength() - R_planet;
        if (sunHeight < 0.0f) break;
        sunOpticalDepthR += std::exp(-sunHeight / hR) * sunStepSize;
        sunOpticalDepthM += std::exp(-sunHeight / hM) * sunStepSize;
        sunCurrentT += sunStepSize;
    }

    GfVec3f tau = betaR * sunOpticalDepthR + GfVec3f(betaM * 1.1f) * sunOpticalDepthM;
    return GfVec3f(std::exp(-tau[0]), std::exp(-tau[1]), std::exp(-tau[2]));
}

}  // namespace Restir
