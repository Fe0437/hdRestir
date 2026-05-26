#include "camera_ray.h"

#include <cmath>

PXR_NAMESPACE_USING_DIRECTIVE

namespace Restir {

Ray GenerateCameraRay(
    const GfMatrix4d& inverseViewMatrix,
    const GfMatrix4d& inverseProjMatrix,
    float px, float py, int width, int height)
{
    const float ndcX = (2.0f * px / width)  - 1.0f;
    const float ndcY = (2.0f * py / height) - 1.0f;

    const GfVec3f origin{inverseViewMatrix.Transform(GfVec3d(0.0, 0.0, 0.0))};
    const GfVec3f nearCam{inverseProjMatrix.Transform(GfVec3d(ndcX, ndcY, -1.0))};
    const GfVec3f nearWorld{inverseViewMatrix.Transform(GfVec3d(nearCam))};

    return {origin, (nearWorld - origin).GetNormalized()};
}

Ray GenerateCameraRay(
    const GfMatrix4d& inverseViewMatrix,
    const GfMatrix4d& inverseProjMatrix,
    float px, float py, int width, int height,
    const CameraParams& params, Rng& rng)
{
    float ndcX = (2.0f * px / width)  - 1.0f;
    float ndcY = (2.0f * py / height) - 1.0f;

    if (params.lensDistortion != 0.0f) {
        const float r2 = ndcX * ndcX + ndcY * ndcY;
        const float f  = 1.0f + params.lensDistortion * r2;
        ndcX *= f;
        ndcY *= f;
    }

    const GfVec3f origin {inverseViewMatrix.Transform(GfVec3d(0.0, 0.0, 0.0))};
    const GfVec3f nearCam{inverseProjMatrix.Transform(GfVec3d(ndcX, ndcY, -1.0))};

    if (!params.enableDoF) {
        const GfVec3f nearWorld{inverseViewMatrix.Transform(GfVec3d(nearCam))};
        return {origin, (nearWorld - origin).GetNormalized()};
    }

    const float apertureRadius = (params.focalLength / 10.0f) / (2.0f * params.fStop);

    float lensU, lensV;
    if (params.bokehBlades < 3) {
        const float r     = std::sqrt(rng.NextFloat());
        const float theta = 2.0f * M_PI * rng.NextFloat();
        lensU = r * std::cos(theta);
        lensV = r * std::sin(theta);
    } else {
        const float theta         = 2.0f * M_PI * rng.NextFloat();
        const float r             = std::sqrt(rng.NextFloat());
        const float sectorAngle   = 2.0f * M_PI / static_cast<float>(params.bokehBlades);
        const float sector        = std::floor(theta / sectorAngle);
        const float angleInSector = theta - sector * sectorAngle;
        const float d             = std::cos(sectorAngle / 2.0f) / std::cos(sectorAngle / 2.0f - angleInSector);
        lensU = r * d * std::cos(theta);
        lensV = r * d * std::sin(theta);
    }
    lensU *= apertureRadius;
    lensV *= apertureRadius;

    const GfVec3f lensPointCam  {lensU, lensV, 0.0f};
    const GfVec3f focalPointCam { nearCam * params.focusDistance };
    const GfVec3f lensPointWorld {inverseViewMatrix.Transform(GfVec3d(lensPointCam))};
    const GfVec3f focalPointWorld{inverseViewMatrix.Transform(GfVec3d(focalPointCam))};

    return {lensPointWorld, (focalPointWorld - lensPointWorld).GetNormalized()};
}

}  // namespace Restir