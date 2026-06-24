#include "integration_pass.h"

#include "buffer_provider.h"
#include "buffer_user.h"
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

    namespace
    {

        // Wraps FrameBuffersMap for use as IBufferProvider.
        struct FrameBuffersProvider final : IBufferProvider
        {
            explicit FrameBuffersProvider(FrameBuffersMap &b, std::size_t n) : buffers{b}, count{n} {}

            [[nodiscard]] bool Has(std::string_view name) const override
            {
                return buffers.Has(name);
            }

            [[nodiscard]] FrameBuffer &GetChecked(std::string_view name) override
            {
                DBG_ASSERT(buffers.Has(name), "Buffer must be prepared before parallel Li(): " + std::string{name});
                return buffers.GetFrameBuffer(name);
            }

            [[nodiscard]] void *GetOrCreatePersistent(std::string_view name, std::size_t stride) override
            {
                if (!buffers.Has(name))
                {
                    buffers.AddOrGetPersistent(name, stride, count);
                }
                return buffers.GetFrameBuffer(name).Data();
            }

            [[nodiscard]] void *Add(std::string_view name, std::size_t stride) override
            {
                buffers.Add(name, stride, count);
                return buffers.GetFrameBuffer(name).Data();
            }

            FrameBuffersMap &buffers;
            std::size_t      count;
        };

    } // namespace

    void IntegrationPass::_execute(RenderContext &ctx)
    {
        DBG_ASSERT(ctx.buffers.Has(kGBufferOutputName), "GBuffer must be present (produced by RaycastPass)");

        const std::size_t    count{ctx.frame.PixelCount()};
        FrameBuffersProvider provider{ctx.buffers, count};

        if (IBufferStager *stager = _integrator->GetBufferStager())
        {
            stager->PrepareBuffers(provider, *ctx.scene);
        }

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

        WorkParallelForN(
            static_cast<std::size_t>(numTiles),
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
                                ray = GenerateCameraRay(invView, invProj, px, py, width, height, *ctx.cameraParams,
                                                        pixelRng);
                            }
                            else
                            {
                                ray = GenerateCameraRay(invView, invProj, px, py, width, height);
                            }

                            const SampledWavelengths lambda{SampledWavelengths::SampleUniform(pixelRng.NextFloat())};

                            const SampledSpectrum spectrum{
                                _integrator->Li({ray, gbuf[i]}, *ctx.scene, pixelRng, lambda, provider, {i, count})};

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
