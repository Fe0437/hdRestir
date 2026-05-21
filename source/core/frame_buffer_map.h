#pragma once

#include "debug.h"
#include "frame_buffer.h"

#include <gsl/gsl>

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace Restir {

class FrameBuffersMap final {
public:
    // Owned transient buffer. Re-declaring with the same stride resizes;
    // re-declaring with a different stride is a DBG_ASSERT.
    void Add(std::string_view name, std::size_t stride, std::size_t count);

    template<typename T> [[nodiscard]] gsl::span<T> Get(std::string_view name);
    [[nodiscard]] FrameBuffer& GetFrameBuffer(std::string_view name);
    [[nodiscard]] const FrameBuffer& GetFrameBuffer(std::string_view name) const;

    [[nodiscard]] bool Has(std::string_view name) const noexcept;

    void Clear();

#if DEBUG_ENABLED
    void Dump() const;
#endif

private:
    struct Entry {
        explicit Entry(FrameBuffer fb)
            : Owned(std::move(fb)) {
        }

        FrameBuffer  Owned;
    };
    std::unordered_map<std::string, Entry> _entries;
};

template<typename T>
gsl::span<T> FrameBuffersMap::Get(std::string_view name) {
    const auto& it{ _entries.find(std::string(name))};
    if (it == _entries.end()) {
        DBG_ASSERT(false, "FrameBuffersMap::get: no buffer named '" + std::string(name) + "'");
        return {};
    }

    Entry& entry{ it->second };
    Expects(sizeof(T) == entry.Owned.Stride());

    return entry.Owned.As<T>();
}

} // namespace Restir
