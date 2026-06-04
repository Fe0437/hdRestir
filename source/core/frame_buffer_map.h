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

    // Creates the buffer if absent (zero-initialized); if already present (injected from
    // the previous frame's persistent store) keeps the existing data, resizing only if
    // count changed. Marks the buffer as persistent so it survives across frames.
    void AddOrGetPersistent(std::string_view name, std::size_t stride, std::size_t count);

    template<typename T> [[nodiscard]] gsl::span<T> Get(std::string_view name);
    [[nodiscard]] FrameBuffer& GetFrameBuffer(std::string_view name);
    [[nodiscard]] const FrameBuffer& GetFrameBuffer(std::string_view name) const;

    [[nodiscard]] bool Has(std::string_view name) const noexcept;

    // Moves all buffers from `store` into this map (store is emptied).
    // Called at the start of a pipeline execution to inject last frame's persistent state.
    void InjectFrom(FrameBuffersMap& store);

    // Moves all persistent buffers out of this map into `store` (store is first cleared).
    // Called at the end of a pipeline execution to save state for the next frame.
    void ExtractPersistentTo(FrameBuffersMap& store);

    void Clear();

#if DEBUG_ENABLED
    void Dump() const;
#endif

private:
    struct Entry {
        explicit Entry(FrameBuffer fb)
            : Owned(std::move(fb)) {
        }

        FrameBuffer Owned;
        bool        Persistent{false};
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
