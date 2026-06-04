#include "raycast_pass.h"

#include "camera_ray.h"
#include "render_context.h"

#include <gsl/gsl>
#include <pxr/base/work/loops.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace Restir {

namespace {

template<typename StringT>
[[nodiscard]] bool HasOutputName(const std::vector<StringT>& outputNames, std::string_view name)
{
    return std::find(outputNames.begin(), outputNames.end(), name) != outputNames.end();
}

}  // namespace

std::vector<std::string> RaycastPass::StaticInputs()
{
    return {};
}

std::vector<std::string> RaycastPass::StaticOutputs()
{
    std::vector<std::string> outputs{std::string{kGBufferOutputName}};
    outputs.push_back(std::string{kDepthOutputName});
    outputs.push_back(std::string{kAlbedoOutputName});
    outputs.push_back(std::string{kNormalOutputName});
    return outputs;
}

std::vector<std::string> RaycastPass::Outputs() const
{
    std::vector<std::string> outputs{std::string{kGBufferOutputName}};
    if (HasOutputName(_requestedOutputs, kDepthOutputName)) {
        outputs.push_back(std::string{kDepthOutputName});
    }
    if (HasOutputName(_requestedOutputs, kAlbedoOutputName)) {
        outputs.push_back(std::string{kAlbedoOutputName});
    }
    if (HasOutputName(_requestedOutputs, kNormalOutputName)) {
        outputs.push_back(std::string{kNormalOutputName});
    }
    return outputs;
}

void RaycastPass::_execute(RenderContext& ctx)
{
    const std::size_t count{ctx.frame.PixelCount()};
    ctx.buffers.Add(kGBufferOutputName, sizeof(std::optional<HitRecord>), count);
    auto gbuf{ ctx.buf<std::optional<HitRecord>>(kGBufferOutputName) };

    const auto outputs{Outputs()};
    const bool exportDepth{HasOutputName(outputs, kDepthOutputName)};
    const bool exportAlbedo{HasOutputName(outputs, kAlbedoOutputName)};
    const bool exportNormal{HasOutputName(outputs, kNormalOutputName)};

    if (exportDepth) {
        ctx.buffers.Add(kDepthOutputName, sizeof(float), count);
    }
    if (exportAlbedo) {
        ctx.buffers.Add(kAlbedoOutputName, sizeof(GfVec3f), count);
    }
    if (exportNormal) {
        ctx.buffers.Add(kNormalOutputName, sizeof(GfVec3f), count);
    }

    auto depth{exportDepth ? ctx.buf<float>(kDepthOutputName) : gsl::span<float>{}};
    auto albedo{exportAlbedo ? ctx.buf<GfVec3f>(kAlbedoOutputName) : gsl::span<GfVec3f>{}};
    auto normal{exportNormal ? ctx.buf<GfVec3f>(kNormalOutputName) : gsl::span<GfVec3f>{}};

    const GfMatrix4d invView    { ctx.viewMatrix.GetInverse()};
    const GfMatrix4d invProj    { ctx.projMatrix.GetInverse()};
    const int        width      {ctx.frame.RenderedWidth()};
    const int        height     {ctx.frame.RenderedHeight()};
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
                        const std::size_t i  { static_cast<std::size_t>(y) * width + x };
                        const float       px { static_cast<float>(x) + 0.5f };
                        const float       py { static_cast<float>(y) + 0.5f };
                        Ray ray;
                        if (ctx.cameraParams) {
                            Rng pixelRng{static_cast<std::uint32_t>(i)
                                        ^ static_cast<std::uint32_t>(frameIndex * 12345ULL)};
                            ray = GenerateCameraRay(invView, invProj, px, py, width, height,
                                                    *ctx.cameraParams, pixelRng);
                        } else {
                            ray = GenerateCameraRay(invView, invProj, px, py, width, height);
                        }

                        gbuf[i] = ctx.scene->IntersectScene(ray.Origin, ray.Dir);
                        if (gbuf[i].has_value()) {
                            if (exportDepth) {
                                depth[i] = gbuf[i]->Depth;
                            }
                            if (exportAlbedo) {
                                albedo[i] = gbuf[i]->Albedo;
                            }
                            if (exportNormal) {
                                normal[i] = gbuf[i]->Normal;
                            }
                        } else {
                            if (exportDepth) {
                                depth[i] = 1.0f;
                            }
                            if (exportAlbedo) {
                                albedo[i] = GfVec3f(0.0f);
                            }
                            if (exportNormal) {
                                normal[i] = GfVec3f(0.0f);
                            }
                        }
                    }
                }
            }
        });
}

}  // namespace Restir
