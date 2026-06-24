#include "integration_pass.h"

#include "camera_ray.h"
#include "debug.h"
#include "pxr/base/gf/vec4f.h"
#include "render_context.h"
#include "spectrum.h"

#include <gsl/gsl>
#include <pxr/base/work/loops.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace Restir
{

    void IntegrationPass::_execute(RenderContext &ctx)
    {
        DBG_ASSERT(ctx.buffers.Has(kGBufferOutputName), "GBuffer must be present (produced by RaycastPass)");

        const std::size_t count{ctx.frame.PixelCount()};
        ctx.buffers.Add(kColorOutputName, sizeof(GfVec4f), count);
        auto fb{ctx.buf<GfVec4f>(kColorOutputName)};
        auto gbuf{ctx.buf<std::optional<HitRecord>>(kGBufferOutputName)};

        const GfMatrix4d invView{ctx.viewMatrix.GetInverse()};
        const GfMatrix4d invProj{ctx.projMatrix.GetInverse()};
        const int        width{ctx.frame.RenderedWidth()};
        const int        height{ctx.frame.RenderedHeight()};
        const int        frameIndex{ctx.frameIndex};

        constexpr int kTileSize{16};
        const int     numTilesX{(width + kTileSize - 1) / kTileSize};
        const int     numTilesY{(height + kTileSize - 1) / kTileSize};
        const int     numTiles{numTilesX * numTilesY};

        WorkParallelForN(static_cast<std::size_t>(numTiles),
                         [&](std::size_t begin, std::size_t end)
                         {
                             for (std::size_t t{begin}; t < end; ++t)
                             {
                                 const int tileX{static_cast<int>(t) % numTilesX};
                                 const int tileY{static_cast<int>(t) / numTilesX};
                                 const int startX{tileX * kTileSize};
                                 const int startY{tileY * kTileSize};
                                 const int endX{std::min(startX + kTileSize, width)};
                                 const int endY{std::min(startY + kTileSize, height)};
                                 for (int y{startY}; y < endY; ++y)
                                 {
                                     for (int x{startX}; x < endX; ++x)
                                     {
                                         const std::size_t i{static_cast<std::size_t>(y) * width + x};

                                         Rng         pixelRng{static_cast<std::uint32_t>(i) ^
                                                              static_cast<std::uint32_t>(frameIndex * 12345ULL)};
                                         const float px{static_cast<float>(x) + pixelRng.NextFloat()};
                                         const float py{static_cast<float>(y) + pixelRng.NextFloat()};

                                         Ray ray;
                                         if (ctx.cameraParams)
                                         {
                                             ray = GenerateCameraRay(invView, invProj, px, py, width, height,
                                                                     *ctx.cameraParams, pixelRng);
                                         }
                                         else
                                         {
                                             ray = GenerateCameraRay(invView, invProj, px, py, width, height);
                                         }

                                         const SampledWavelengths lambda{
                                             SampledWavelengths::SampleUniform(pixelRng.NextFloat())};
                                         const SampledSpectrum spectrum{
                                             _integrator->Li({ray, gbuf[i]}, *ctx.scene, pixelRng, lambda)};
                                         GfVec3f L{SpectrumToRGB(spectrum, lambda)};

                                         if (ctx.cameraParams && ctx.cameraParams->enableExposure)
                                         {
                                             L *= GetExposureMultiplier(*ctx.cameraParams);
                                         }

                                         fb[i] = GfVec4f{L[0], L[1], L[2], 1.0f};
                                     }
                                 }
                             }
                         });
    }

} // namespace Restir
