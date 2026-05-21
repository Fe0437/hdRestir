#include "denoiser_pass.h"

#include "debug.h"
#include "denoiser.h"
#include "output_names.h"

#include "pxr/base/gf/vec3f.h"

#include <vector>

namespace Restir {

void DenoiserPass::Execute(RenderContext& ctx)
{
    DBG_ASSERT(ctx.buffers.Has(kColorOutputName), "Color must be present before DenoiserPass");

    if (!_config.EnableDenoiser && !_config.EnableChromaticityBlur) {
        return;
    }

    auto colorBuffer{ctx.buf<GfVec4f>(kColorOutputName)};
    std::vector<Denoiser::GuideBufferView> guideBuffers{};
    if (ctx.buffers.Has(kAlbedoOutputName)) {
        const auto albedo{ctx.buf<GfVec3f>(kAlbedoOutputName)};
        guideBuffers.push_back(Denoiser::GuideBufferView{
            .Name = kAlbedoOutputName,
            .Pixels = gsl::span<const pxr::GfVec3f>{albedo.data(), albedo.size()}
        });
    }
    if (ctx.buffers.Has(kNormalOutputName)) {
        const auto normal{ctx.buf<GfVec3f>(kNormalOutputName)};
        guideBuffers.push_back(Denoiser::GuideBufferView{
            .Name = kNormalOutputName,
            .Pixels = gsl::span<const pxr::GfVec3f>{normal.data(), normal.size()}
        });
    }

    Denoiser::Run(colorBuffer, guideBuffers, ctx.width, ctx.height, ctx.frameIndex, _config);
}

}  // namespace Restir