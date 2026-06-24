#pragma once

#include "camera_frame.h"
#include "camera_params.h"
#include "frame_buffer_map.h"
#include "pxr/base/gf/matrix4d.h"
#include "rng.h"
#include "scene_interface.h"

#include <gsl/gsl>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace Restir
{

    struct RenderContext
    {
        gsl::not_null<const IScene *> scene;
        const GfMatrix4d             &viewMatrix;
        const GfMatrix4d             &projMatrix;
        CameraFrame                   frame;
        int                           frameIndex;
        Rng                          &rng;
        FrameBuffersMap               buffers;
        std::vector<std::string>      OutputNames{};
        std::optional<CameraParams>   cameraParams; // nullopt = pinhole

        template <typename T> [[nodiscard]] gsl::span<T> buf(std::string_view name)
        {
            return buffers.Get<T>(name);
        }
    };

} // namespace Restir
