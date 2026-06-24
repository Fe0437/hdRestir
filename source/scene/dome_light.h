#pragma once

#include "environment.h"
#include "light_interface.h"
#include "light_params.h"
#include "pxr/usd/sdf/assetPath.h"

#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace Restir
{

    // Handles both NEE importance sampling (ILight) and miss-colour lookup (IEnvironment).
    // Owns the texture pixels and CDF tables; Prepare() is idempotent.
    class DomeLight final : public IEnvironment
    {
      public:
        explicit DomeLight(const LightParams &params) noexcept;

        void SetParams(const LightParams &params) override
        {
            _params = params;
        }

        void Prepare() override;

        // ILight — importance-sampled direction for NEE.
        [[nodiscard]] std::optional<LightSample> SampleLight(const GfVec3f &hitPos, Rng &rng) const override;

        [[nodiscard]] Pdf EvalPdf(const GfVec3f &hitPos, const GfVec3f &dir, float dist,
                                  const GfVec3f &lightNormal) const override;

        // IEnvironment — bilinear lookup for miss radiance, and solid-angle pdf for MIS.
        [[nodiscard]] GfVec3f Sample(const GfVec3f &dir) const override;
        [[nodiscard]] float   EvalPdf(const GfVec3f &dir) const override;

      private:
        LightParams        _params;
        SdfAssetPath       _loadedPath;
        std::vector<float> _pixels;
        std::vector<float> _rowCdf;
        std::vector<float> _colCdf;
        int                _width{0};
        int                _height{0};
        float              _totalLuminance{0.0f};
    };

} // namespace Restir
