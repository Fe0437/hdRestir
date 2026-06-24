#pragma once

#include "camera_params.h"
#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/pxr.h"
#include "ray.h"
#include "rng.h"

PXR_NAMESPACE_USING_DIRECTIVE

namespace Restir
{

    [[nodiscard]] Ray GenerateCameraRay(const GfMatrix4d &inverseViewMatrix, const GfMatrix4d &inverseProjMatrix,
                                        float px, float py, int width, int height);

    [[nodiscard]] Ray GenerateCameraRay(const GfMatrix4d &inverseViewMatrix, const GfMatrix4d &inverseProjMatrix,
                                        float px, float py, int width, int height, const CameraParams &params,
                                        Rng &rng);

} // namespace Restir