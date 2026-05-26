#pragma once

#include "not_null_unique_ptr.h"
#include "direct_light_integrator_interface.h"
#include "integrator.h"
#include "material.h"

#include <functional>
#include <memory>
#include <utility>

namespace Restir {

struct PathTracePassSettings {
    bool EnableSubsurface{true};
    int  MaxReflectionBounces{8};
    int  MaxRefractionBounces{8};
    bool RenderIblBackground{true};
};

class PathIntegrator final : public IIntegrator {
public:
    using DirectLightIntegratorFactory = std::function<NotNullUniquePtr<IDirectLightIntegrator>(const IScene&)>;

    explicit PathIntegrator(
        DirectLightIntegratorFactory   directLightFactory,
        PathTracePassSettings          settings     = {},
        int                            maxDepth     = 32);

    [[nodiscard]] SampledSpectrum Li(
        const RayIntersection&    isect,
        const IScene&             scene,
        Rng&                      rng,
        const SampledWavelengths& lambda) const override;

    [[nodiscard]] SampledSpectrum Li(
        const ShadingPoint& surface,
        const IScene&       scene,
        Rng&                rng) const override;

    void SetSettings(PathTracePassSettings settings) { _settings = std::move(settings); }

private:
    PathTracePassSettings          _settings;
    int                            _maxDepth;
    DirectLightIntegratorFactory   _directLightFactory;
};

}  // namespace Restir
