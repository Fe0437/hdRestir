#pragma once

#include <string_view>

namespace Restir {

inline constexpr std::string_view kGBufferOutputName{"GBuffer"};
inline constexpr std::string_view kColorOutputName{"Color"};
inline constexpr std::string_view kDepthOutputName{"Depth"};
inline constexpr std::string_view kAlbedoOutputName{"Albedo"};
inline constexpr std::string_view kNormalOutputName{"Normal"};

enum class OutputDataType {
    Color4,
    Vec3,
    Float1,
    Unknown,
};

[[nodiscard]] inline bool IsColorOutputName(std::string_view name) noexcept
{
    return name == kColorOutputName;
}

[[nodiscard]] inline OutputDataType GetOutputDataType(std::string_view name) noexcept
{
    if (name == kColorOutputName) {
        return OutputDataType::Color4;
    }
    if (name == kAlbedoOutputName || name == kNormalOutputName) {
        return OutputDataType::Vec3;
    }
    if (name == kDepthOutputName) {
        return OutputDataType::Float1;
    }
    return OutputDataType::Unknown;
}

}  // namespace Restir