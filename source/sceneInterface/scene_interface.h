#pragma once

#include "environment.h"
#include "hit_record.h"
#include "image_texture_sampler.h"
#include "light_interface.h"
#include "light_factory.h"
#include "light_params.h"
#include "materials/material.h"
#include "render_job.h"

#include <gsl/gsl>

#include <mutex>
#include <optional>

#include "pxr/base/gf/vec3f.h"
#include "pxr/usd/sdf/path.h"

PXR_NAMESPACE_USING_DIRECTIVE

namespace Restir {

struct LightFactoryInput {
    SdfPath    Id;
    LightType  Type{LightType::Point};
    LightParams Params{};
};

struct SceneBuildRenderStateConfig {
    bool EnablePhysicalSky{false};
    float PhysicalSkyTime{12.0f};
};

class IScene {
public:
    virtual ~IScene() = default;

    virtual std::recursive_mutex& GetSceneLock() = 0;

    virtual void BuildRenderState(const SceneBuildRenderStateConfig& config,
                                  const IRenderJob& job) = 0;

    // Returns nullptr if matId is invalid or the instance has no material bound.
    [[nodiscard]] virtual const IMaterial*               GetMaterial(int matId) const = 0;

    // Returns the active environment (IBL map or physical sky). Always non-null after
    // _PrepareScene; nullptr only before the first frame is prepared.
    [[nodiscard]] virtual const IEnvironment*            GetEnvironment() const = 0;

    [[nodiscard]] virtual gsl::span<ILight* const>       GetLights() const = 0;

    // Returns the sky-light (PhysicalSky sun) as an ILight, or nullptr if none.
    [[nodiscard]] virtual const ILight*                  GetSkyLight() const noexcept = 0;

    [[nodiscard]] virtual std::optional<HitRecord>       IntersectScene(
        const GfVec3f& rayOrigin,
        const GfVec3f& rayDir) const = 0;

    // Extension point: Epic 2.5 — texture sampling.
    [[nodiscard]] virtual const ImageTextureSamplerFactory* GetTextureSamplerFactory() const = 0;
};

}  // namespace Restir
