#include "camera_params.h"
#include "camera_ray.h"

#include <cassert>
#include <cmath>

namespace
{

    bool NearlyEqual(float a, float b, float epsilon = 1.0e-4f)
    {
        return std::abs(a - b) <= epsilon;
    }

    void TestExposureDisabledKeepsUnitMultiplier()
    {
        Restir::CameraParams params{};
        params.enableDoF     = true;
        params.focalLength   = 35.0f;
        params.fStop         = 22.0f;
        params.focusDistance = 8.0f;
        params.iso           = 100.0f;
        params.shutterSpeed  = 0.02f;

        assert(NearlyEqual(Restir::GetExposureMultiplier(params), 1.0f));
    }

    void TestExposureEnabledMatchesFormula()
    {
        Restir::CameraParams params{};
        params.enableExposure = true;
        params.fStop          = 8.0f;
        params.iso            = 200.0f;
        params.shutterSpeed   = 0.5f;

        const float expected{(params.iso / 100.0f) * params.shutterSpeed / (params.fStop * params.fStop) * 100.0f};
        assert(NearlyEqual(Restir::GetExposureMultiplier(params), expected));
    }

    void TestCenterRayHitsFocusPlane()
    {
        const GfMatrix4d identity{1.0};

        Restir::CameraParams params{};
        params.enableDoF     = true;
        params.focalLength   = 50.0f;
        params.fStop         = 5.6f;
        params.focusDistance = 10.0f;

        Restir::Rng rng{};
        rng.ResetSeed(1234u);

        const Restir::Ray ray{Restir::GenerateCameraRay(identity, identity, 0.5f, 0.5f, 1, 1, params, rng)};
        const float       t{(-params.focusDistance - ray.Origin[2]) / ray.Dir[2]};
        const GfVec3f     hit{ray.Origin + ray.Dir * t};

        assert(NearlyEqual(hit[0], 0.0f));
        assert(NearlyEqual(hit[1], 0.0f));
        assert(NearlyEqual(hit[2], -params.focusDistance));
    }

} // namespace

int main()
{
    TestExposureDisabledKeepsUnitMultiplier();
    TestExposureEnabledMatchesFormula();
    TestCenterRayHitsFocusPlane();
    return 0;
}