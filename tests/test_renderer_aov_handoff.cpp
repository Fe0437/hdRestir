#include "output_names.h"
#include "pxr/base/gf/vec3i.h"
#include "pxr/base/gf/vec4f.h"
#include "pxr/imaging/hd/tokens.h"
#include "renderer.h"
#include "rendererInterface/render_job.h"
#include "restir_render_settings.h"
#include "scene.h"

#include <cassert>
#include <optional>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace
{

    class RecordingTarget final : public Restir::IFrameBufferTarget
    {
      public:
        void *Map() override
        {
            return nullptr;
        }
        void               Unmap() override {}
        [[nodiscard]] bool IsConverged() const override
        {
            return _converged;
        }
        void SetConverged(bool converged) override
        {
            _converged = converged;
        }
        void Resolve() override
        {
            ++ResolveCount;
        }
        void ResolveBucket(unsigned int, unsigned int, unsigned int, unsigned int) override {}
        void GetFloatBuffer(std::vector<float> &) const override {}
        void CopyFromFrameBuffer(const Restir::FrameBuffer &source) override
        {
            ++CopyCount;
            LastCopied = source;
        }
        void WriteSample(GfVec3i const &, GfVec4f const &) override {}
        void Write(GfVec3i const &, size_t, float const *) override {}
        void Write(GfVec3i const &, size_t, int const *) override {}
        void Clear(size_t, float const *) override {}
        void Clear(size_t, int const *) override {}

        int                                CopyCount{0};
        int                                ResolveCount{0};
        std::optional<Restir::FrameBuffer> LastCopied{};

      private:
        bool _converged{false};
    };

    [[nodiscard]] Restir::AovBinding MakeColorBinding(Restir::IFrameBufferTarget &target)
    {
        return Restir::AovBinding{
            .AovName    = HdAovTokens->color,
            .Target     = &target,
            .ClearValue = VtValue{},
        };
    }

    void TestResolveTargetsUsesCurrentBinding()
    {
        Restir::Renderer renderer{};
        renderer.SetRenderSetting(HdRestirRenderSettingsTokens->physicalSkyEnable, VtValue(true));
        renderer.SetRenderSetting(HdRestirRenderSettingsTokens->targetSampleCount, VtValue(1));
        renderer.SetRequestedOutputNames({std::string{Restir::kColorOutputName}});
        renderer.SetDataWindow(GfRect2i(GfVec2i(0), 4, 4));
        renderer.Clear();

        Restir::Scene scene{};
        renderer.Render(Restir::GetMainThreadRenderJob(), scene);

        RecordingTarget firstTarget{};
        const auto      firstBinding{MakeColorBinding(firstTarget)};
        renderer.ResolveTargets({&firstBinding, 1});

        assert(firstTarget.CopyCount == 1);
        assert(firstTarget.ResolveCount == 1);
        assert(firstTarget.IsConverged());
        assert(firstTarget.LastCopied.has_value());
        assert(firstTarget.LastCopied->Name() == Restir::kColorOutputName);

        RecordingTarget secondTarget{};
        const auto      secondBinding{MakeColorBinding(secondTarget)};
        renderer.ResolveTargets({&secondBinding, 1});

        assert(firstTarget.CopyCount == 1);
        assert(secondTarget.CopyCount == 1);
        assert(secondTarget.ResolveCount == 1);
        assert(secondTarget.IsConverged());
        assert(secondTarget.LastCopied.has_value());
        assert(secondTarget.LastCopied->Name() == Restir::kColorOutputName);
    }

    void TestClearDropsResolvedOutputs()
    {
        Restir::Renderer renderer{};
        renderer.SetRenderSetting(HdRestirRenderSettingsTokens->physicalSkyEnable, VtValue(true));
        renderer.SetRequestedOutputNames({std::string{Restir::kColorOutputName}});
        renderer.SetDataWindow(GfRect2i(GfVec2i(0), 4, 4));
        renderer.Clear();

        Restir::Scene scene{};
        renderer.Render(Restir::GetMainThreadRenderJob(), scene);
        renderer.Clear();

        RecordingTarget target{};
        const auto      binding{MakeColorBinding(target)};
        renderer.ResolveTargets({&binding, 1});

        assert(target.CopyCount == 0);
        assert(target.ResolveCount == 0);
        assert(!target.LastCopied.has_value());
    }

} // namespace

int main()
{
    TestResolveTargetsUsesCurrentBinding();
    TestClearDropsResolvedOutputs();
    return 0;
}