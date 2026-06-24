#pragma once

#include <string_view>

namespace Restir
{

    // Persistent per-pixel reservoir buffer written by RISPathTracePass.
    // Internal — not a pipeline AOV output.
    inline constexpr std::string_view kReservoirBufferName{"__ris_reservoir"};

} // namespace Restir
