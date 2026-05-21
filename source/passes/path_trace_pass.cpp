#include "path_trace_pass.h"

#include "camera_ray.h"
#include "debug.h"
#include "default_material.h"
#include "render_context.h"
#include "rng.h"
#include "radiance_computation/direct_lighting.h"
#include "shading_helpers.h"
#include "spectrum.h"

#include "pxr/base/gf/vec4f.h"

#include <gsl/gsl>
#include <optional>
#include <pxr/base/work/loops.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace Restir {

namespace {

[[nodiscard]] SampledSpectrum TracePath(
    const CameraRay&                cameraRay,
    const std::optional<HitRecord>& primaryHit,
    const IScene&                   scene,
    const IEnvironment*             env,
    const PathTracePassSettings&    settings,
    int                             maxDepth,
    Rng&                            rng,
    const SampledWavelengths&       lambda)
{
    SampledSpectrum throughput{1.0f};
    SampledSpectrum totalRadiance{0.0f};
    Ray currentRay{cameraRay.origin, cameraRay.dir};
    std::optional<HitRecord> nextHit{primaryHit};
    BounceState bounceState{};

    for (int bounce{0}; bounce < maxDepth; ++bounce) {
        if (!nextHit.has_value()) {
            if (env != nullptr && (bounce > 0 || settings.RenderIblBackground)) {
                totalRadiance += throughput * RGBToSpectrum(env->Sample(currentRay.Dir), lambda);
            }
            break;
        }

        HitRecord hit{*nextHit};
        const IMaterial* material{scene.GetMaterial(hit.MatId)};
        if (material == nullptr) {
            material = &DefaultMaterial::Instance();
        }

        BSDFClosure c{material->GetClosure(hit)};
        if (!settings.EnableSubsurface) {
            c.Subsurface = 0.0f;
        }

        if (c.Opacity < 0.999f && rng.NextFloat() > c.Opacity) {
            currentRay.Origin = hit.Position + currentRay.Dir * 1e-4f;
            nextHit = scene.IntersectScene(currentRay.Origin, currentRay.Dir);
            --bounce;
            continue;
        }

        const bool isInside{GfDot(c.Normal, currentRay.Dir) > 0.0f};
        BeerAbsorption(throughput, c, hit.Depth, isInside, lambda);

        GfVec3f shadingNormal{c.Normal};
        if (isInside) {
            shadingNormal = -shadingNormal;
        }

        totalRadiance += throughput * RGBToSpectrum(c.Emission, lambda);
        const std::unique_ptr<IBSDF> bsdfOwner{material->CreateBSDF(BSDFClosure{c})};
        const ShadingPoint surface{hit, *bsdfOwner, c, shadingNormal, currentRay.Dir, lambda, isInside};
        totalRadiance += throughput * SampleDirectLighting(surface, scene.GetLights(), scene, rng);

        const BounceConfig config{settings.MaxReflectionBounces, settings.MaxRefractionBounces};
        const BounceSample bs{material->SampleBounce(surface, config, bounceState, rng)};

        if (bs.Terminate) {
            break;
        }

        throughput *= bs.ThroughputMul;
        currentRay = bs.NextRay;
        nextHit = scene.IntersectScene(currentRay.Origin, currentRay.Dir);

        if (bs.SkipRoulette) {
            continue;
        }

        if (bounce > 3) {
            const float p{throughput.Max()};
            if (rng.NextFloat() > p) {
                break;
            }
            throughput *= 1.0f / p;
        }
    }

    return totalRadiance;
}

}  // namespace

void PathTracePass::Execute(RenderContext& ctx)
{
    DBG_ASSERT(ctx.buffers.Has(kGBufferOutputName), "GBuffer must be present (produced by RaycastPass)");

    const std::size_t count { gsl::narrow_cast<std::size_t>(ctx.width * ctx.height)};
    ctx.buffers.Add(kColorOutputName, sizeof(GfVec4f), count);
    auto fb  { ctx.buf<GfVec4f>(kColorOutputName) };
    auto gbuf{ ctx.buf<std::optional<HitRecord>>(kGBufferOutputName) };

    const IEnvironment* env{ctx.scene->GetEnvironment()};

    const GfMatrix4d invView    { ctx.viewMatrix.GetInverse()};
    const GfMatrix4d invProj    { ctx.projMatrix.GetInverse()};
    const int        width      { ctx.width };
    const int        height     { ctx.height };
    const int        maxDepth   { _maxDepth };
    const int        frameIndex { ctx.frameIndex };

    constexpr int kTileSize { 16 };
    const int numTilesX { (width  + kTileSize - 1) / kTileSize };
    const int numTilesY { (height + kTileSize - 1) / kTileSize };
    const int numTiles  { numTilesX * numTilesY };

    WorkParallelForN(static_cast<std::size_t>(numTiles),
        [&](std::size_t begin, std::size_t end) {
            for (std::size_t t { begin }; t < end; ++t) {
                const int tileX  { static_cast<int>(t) % numTilesX };
                const int tileY  { static_cast<int>(t) / numTilesX };
                const int startX { tileX * kTileSize };
                const int startY { tileY * kTileSize };
                const int endX   { std::min(startX + kTileSize, width) };
                const int endY   { std::min(startY + kTileSize, height) };
                for (int y { startY }; y < endY; ++y) {
                    for (int x { startX }; x < endX; ++x) {
                        const std::size_t i { static_cast<std::size_t>(y) * width + x };

                        Rng pixelRng{static_cast<std::uint32_t>(i)
                                        ^ static_cast<std::uint32_t>(frameIndex * 12345ULL)};
                        const float px { static_cast<float>(x) + pixelRng.NextFloat() };
                        const float py { static_cast<float>(y) + pixelRng.NextFloat() };

                        CameraRay ray;
                        if (ctx.cameraParams) {
                            ray = GenerateCameraRay(invView, invProj, px, py, width, height,
                                                    *ctx.cameraParams, pixelRng);
                        } else {
                            ray = GenerateCameraRay(invView, invProj, px, py, width, height);
                        }

                        const SampledWavelengths lambda{
                            SampledWavelengths::SampleUniform(pixelRng.NextFloat())};
                        const SampledSpectrum spectrum{
                            TracePath(ray,
                                      gbuf[i],
                                      *ctx.scene,
                                      env,
                                      _settings,
                                      maxDepth,
                                      pixelRng,
                                      lambda)};
                        GfVec3f L{SpectrumToRGB(spectrum, lambda)};

                        if (ctx.cameraParams && ctx.cameraParams->enableExposure) {
                            const auto& cam { *ctx.cameraParams };
                            L *= GetExposureMultiplier(cam);
                        }

                        fb[i] = GfVec4f{L[0], L[1], L[2], 1.0f};
                    }
                }
            }
        });
}

}  // namespace Restir
