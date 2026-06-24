#include "camera_frame.h"
#include "default_material.h"
#include "output_names.h"
#include "path_tracer_pipeline.h"
#include "rng.h"
#include "split_screen.h"
#include "upscale_pass.h"

#include <cassert>
#include <cstddef>
#include <map>
#include <mutex>
#include <optional>
#include <vector>

namespace
{

    bool IsWhite(const GfVec4f &c)
    {
        return c[0] == 1.0f && c[1] == 1.0f && c[2] == 1.0f && c[3] == 1.0f;
    }

    void TestBlitSplitZero()
    {
        const int         width  = 8;
        const int         height = 4;
        const std::size_t count  = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);

        std::vector<GfVec4f> dst(count, GfVec4f(0.0f));
        std::vector<GfVec4f> left(count, GfVec4f(0.8f, 0.1f, 0.1f, 1.0f));
        std::vector<GfVec4f> right(count, GfVec4f(0.1f, 0.8f, 0.1f, 1.0f));

        Restir::SplitScreenCompositor::debugBlit(dst, left, right, width, height, 0.0f);

        const int splitX = 0;
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                const std::size_t idx =
                    static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
                if (x == splitX)
                {
                    assert(IsWhite(dst[idx]));
                }
                else
                {
                    assert(dst[idx] == right[idx]);
                }
            }
        }
    }

    void TestBlitSplitOne()
    {
        const int         width  = 8;
        const int         height = 4;
        const std::size_t count  = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);

        std::vector<GfVec4f> dst(count, GfVec4f(0.0f));
        std::vector<GfVec4f> left(count, GfVec4f(0.8f, 0.1f, 0.1f, 1.0f));
        std::vector<GfVec4f> right(count, GfVec4f(0.1f, 0.8f, 0.1f, 1.0f));

        Restir::SplitScreenCompositor::debugBlit(dst, left, right, width, height, 1.0f);

        const int splitX = width - 1;
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                const std::size_t idx =
                    static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
                if (x == splitX)
                {
                    assert(IsWhite(dst[idx]));
                }
                else
                {
                    assert(dst[idx] == left[idx]);
                }
            }
        }
    }

    class StubScene final : public Restir::IScene
    {
      public:
        std::recursive_mutex &GetSceneLock() override
        {
            return _sceneLock;
        }
        void BuildRenderState(const Restir::SceneBuildRenderStateConfig &, const Restir::IRenderJob &) override {}
        [[nodiscard]] const Restir::IMaterial &GetMaterial(int) const override
        {
            return Restir::DefaultMaterial::Instance();
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
        [[nodiscard]] const Restir::ILight *GetLightAtHit(const Restir::HitRecord &) const override
        {
            return nullptr;
        }
        [[nodiscard]] std::optional<Restir::HitRecord> IntersectScene(const GfVec3f &, const GfVec3f &) const override
        {
            return std::nullopt;
        }
        [[nodiscard]] const Restir::ImageTextureSamplerFactory *GetTextureSamplerFactory() const override
        {
            return nullptr;
        }

      private:
        std::recursive_mutex _sceneLock{};
    };

    void TestUpscalePassUpscalesLowResolutionInputs()
    {
        const int srcWidth  = 2;
        const int srcHeight = 1;
        const int dstWidth  = 4;
        const int dstHeight = 2;

        Restir::Rng           rng{0};
        StubScene             stubScene;
        Restir::RenderContext ctx{
            .scene      = &stubScene,
            .viewMatrix = GfMatrix4d(1.0),
            .projMatrix = GfMatrix4d(1.0),
            .frame =
                Restir::CameraFrame{
                    .windowWidth     = dstWidth,
                    .windowHeight    = dstHeight,
                    .resolutionLevel = 1, // half-res: RenderedWidth=2, RenderedHeight=1
                    .visibleMaxX     = dstWidth,
                    .visibleMaxY     = dstHeight,
                },
            .frameIndex  = 0,
            .rng         = rng,
            .buffers     = Restir::FrameBuffersMap{},
            .OutputNames = {std::string{Restir::kColorOutputName}, std::string{Restir::kAlbedoOutputName}}};

        ctx.buffers.Add(Restir::kColorOutputName, sizeof(GfVec4f),
                        static_cast<std::size_t>(srcWidth) * static_cast<std::size_t>(srcHeight));
        ctx.buffers.Add("Albedo", sizeof(GfVec3f),
                        static_cast<std::size_t>(srcWidth) * static_cast<std::size_t>(srcHeight));

        auto framebuffer{ctx.buf<GfVec4f>(Restir::kColorOutputName)};
        framebuffer[0] = GfVec4f(1.0f, 0.0f, 0.0f, 1.0f);
        framebuffer[1] = GfVec4f(0.0f, 1.0f, 0.0f, 1.0f);

        auto albedo{ctx.buf<GfVec3f>("Albedo")};
        albedo[0] = GfVec3f(0.2f, 0.3f, 0.4f);
        albedo[1] = GfVec3f(0.5f, 0.6f, 0.7f);

        Restir::UpscalePass pass{{std::string{Restir::kColorOutputName}, std::string{Restir::kAlbedoOutputName}}};
        pass.Execute(ctx);

        assert(!ctx.frame.NeedsUpscale());
        assert(ctx.frame.RenderedWidth() == dstWidth);
        assert(ctx.frame.RenderedHeight() == dstHeight);

        const std::vector<GfVec4f> expectedFramebuffer{
            GfVec4f(1.0f, 0.0f, 0.0f, 1.0f), GfVec4f(1.0f, 0.0f, 0.0f, 1.0f), GfVec4f(0.0f, 1.0f, 0.0f, 1.0f),
            GfVec4f(0.0f, 1.0f, 0.0f, 1.0f), GfVec4f(1.0f, 0.0f, 0.0f, 1.0f), GfVec4f(1.0f, 0.0f, 0.0f, 1.0f),
            GfVec4f(0.0f, 1.0f, 0.0f, 1.0f), GfVec4f(0.0f, 1.0f, 0.0f, 1.0f)};

        const std::vector<GfVec3f> expectedAlbedo{
            GfVec3f(0.2f, 0.3f, 0.4f), GfVec3f(0.2f, 0.3f, 0.4f), GfVec3f(0.5f, 0.6f, 0.7f), GfVec3f(0.5f, 0.6f, 0.7f),
            GfVec3f(0.2f, 0.3f, 0.4f), GfVec3f(0.2f, 0.3f, 0.4f), GfVec3f(0.5f, 0.6f, 0.7f), GfVec3f(0.5f, 0.6f, 0.7f)};

        auto upscaledFramebuffer{ctx.buf<GfVec4f>(Restir::kColorOutputName)};
        auto upscaledAlbedo{ctx.buf<GfVec3f>("Albedo")};
        assert(std::vector<GfVec4f>(upscaledFramebuffer.begin(), upscaledFramebuffer.end()) == expectedFramebuffer);
        assert(std::vector<GfVec3f>(upscaledAlbedo.begin(), upscaledAlbedo.end()) == expectedAlbedo);
    }

    void TestPathTracerPipelinesMatch()
    {
        const int         width  = 9;
        const int         height = 5;
        const std::size_t count  = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);

        Restir::Rng rng{0};

        StubScene             stubScene;
        Restir::RenderContext ctx{.scene      = &stubScene,
                                  .viewMatrix = GfMatrix4d(1.0),
                                  .projMatrix = GfMatrix4d(1.0),
                                  .frame =
                                      Restir::CameraFrame{
                                          .windowWidth  = width,
                                          .windowHeight = height,
                                          .visibleMaxX  = width,
                                          .visibleMaxY  = height,
                                      },
                                  .frameIndex  = 0,
                                  .rng         = rng,
                                  .buffers     = Restir::FrameBuffersMap{},
                                  .OutputNames = {std::string{Restir::kColorOutputName}}};

        Restir::SplitScreenCompositor compositor{Restir::makePathTracerPipeline("Left"),
                                                 Restir::makePathTracerPipeline("Right")};
        compositor.Execute(ctx);

        auto framebuffer = ctx.buf<GfVec4f>(Restir::kColorOutputName);
        assert(framebuffer.size() == count);

        const int splitX = width / 2;
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                const std::size_t idx =
                    static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
                if (x == splitX)
                {
                    assert(IsWhite(framebuffer[idx]));
                }
                else
                {
                    assert(framebuffer[idx] == GfVec4f(0.0f, 0.0f, 0.0f, 1.0f));
                }
            }
        }
    }

    void TestOptionalOutputsWithoutFramebuffer()
    {
        Restir::PathTracerPipelineSettings settings{};
        settings.OutputNames = {std::string{Restir::kAlbedoOutputName}};

        Restir::SplitScreenCompositor compositor{Restir::makePathTracerPipeline("LeftAlbedo", settings),
                                                 Restir::makePathTracerPipeline("RightAlbedo", settings)};

        Restir::Rng           rng{0};
        StubScene             stubScene;
        Restir::RenderContext ctx{.scene      = &stubScene,
                                  .viewMatrix = GfMatrix4d(1.0),
                                  .projMatrix = GfMatrix4d(1.0),
                                  .frame =
                                      Restir::CameraFrame{
                                          .windowWidth  = 4,
                                          .windowHeight = 4,
                                          .visibleMaxX  = 4,
                                          .visibleMaxY  = 4,
                                      },
                                  .frameIndex  = 0,
                                  .rng         = rng,
                                  .buffers     = Restir::FrameBuffersMap{},
                                  .OutputNames = {std::string{Restir::kAlbedoOutputName}}};

        compositor.Execute(ctx);

        assert(!ctx.buffers.Has(Restir::kColorOutputName));
        assert(ctx.buffers.Has(Restir::kAlbedoOutputName));
    }

    void TestOptionalDepthOutputWithoutColor()
    {
        Restir::PathTracerPipelineSettings settings{};
        settings.OutputNames = {std::string{Restir::kDepthOutputName}};

        Restir::SplitScreenCompositor compositor{Restir::makePathTracerPipeline("LeftDepth", settings),
                                                 Restir::makePathTracerPipeline("RightDepth", settings)};

        Restir::Rng           rng{0};
        StubScene             stubScene;
        Restir::RenderContext ctx{.scene      = &stubScene,
                                  .viewMatrix = GfMatrix4d(1.0),
                                  .projMatrix = GfMatrix4d(1.0),
                                  .frame =
                                      Restir::CameraFrame{
                                          .windowWidth  = 4,
                                          .windowHeight = 4,
                                          .visibleMaxX  = 4,
                                          .visibleMaxY  = 4,
                                      },
                                  .frameIndex  = 0,
                                  .rng         = rng,
                                  .buffers     = Restir::FrameBuffersMap{},
                                  .OutputNames = {std::string{Restir::kDepthOutputName}}};

        compositor.Execute(ctx);

        assert(!ctx.buffers.Has(Restir::kColorOutputName));
        assert(ctx.buffers.Has(Restir::kDepthOutputName));
    }

    void TestUnsupportedOutputDoesNotCrash()
    {
        Restir::PathTracerPipelineSettings settings{};
        settings.OutputNames = {std::string{"Unsupported"}};

        Restir::SplitScreenCompositor compositor{Restir::makePathTracerPipeline("LeftUnsupported", settings),
                                                 Restir::makePathTracerPipeline("RightUnsupported", settings)};

        Restir::Rng           rng{0};
        StubScene             stubScene;
        Restir::RenderContext ctx{.scene      = &stubScene,
                                  .viewMatrix = GfMatrix4d(1.0),
                                  .projMatrix = GfMatrix4d(1.0),
                                  .frame =
                                      Restir::CameraFrame{
                                          .windowWidth  = 4,
                                          .windowHeight = 4,
                                          .visibleMaxX  = 4,
                                          .visibleMaxY  = 4,
                                      },
                                  .frameIndex  = 0,
                                  .rng         = rng,
                                  .buffers     = Restir::FrameBuffersMap{},
                                  .OutputNames = {std::string{"Unsupported"}}};

        compositor.Execute(ctx);

        assert(!ctx.buffers.Has("Unsupported"));
    }

} // namespace

int main()
{
    TestBlitSplitZero();
    TestBlitSplitOne();
    TestUpscalePassUpscalesLowResolutionInputs();
    TestPathTracerPipelinesMatch();
    TestOptionalOutputsWithoutFramebuffer();
    TestOptionalDepthOutputWithoutColor();
    TestUnsupportedOutputDoesNotCrash();
    return 0;
}
