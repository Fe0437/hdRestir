#include "frame_buffer_map.h"

#include <utility>

namespace Restir
{

    FrameBuffer &FrameBuffersMap::GetFrameBuffer(std::string_view name)
    {
        const auto &it{_entries.find(std::string(name))};
        if (it == _entries.end())
        {
            DBG_ASSERT(false, "FrameBuffersMap::GetFrameBuffer: no buffer named '" + std::string(name) + "'");
            static FrameBuffer empty{"Empty", 1, 0};
            return empty;
        }

        return it->second.Owned;
    }

    const FrameBuffer &FrameBuffersMap::GetFrameBuffer(std::string_view name) const
    {
        const auto &it{_entries.find(std::string(name))};
        if (it == _entries.end())
        {
            DBG_ASSERT(false, "FrameBuffersMap::GetFrameBuffer: no buffer named '" + std::string(name) + "'");
            static const FrameBuffer empty{"Empty", 1, 0};
            return empty;
        }

        return it->second.Owned;
    }

    void FrameBuffersMap::Add(std::string_view name, std::size_t stride, std::size_t count)
    {
        const std::string key{name};
        const auto       &it{_entries.find(key)};
        if (it == _entries.end())
        {
            _entries.emplace(key, Entry{FrameBuffer(key, stride, count)});
            return;
        }

        if (it->second.Owned.Stride() == stride)
        {
            it->second.Owned.Reconfigure(stride, count);
            return;
        }

        DBG_ASSERT(false, "FrameBuffer name reused with incompatible stride: " + it->first);
    }

    bool FrameBuffersMap::Has(std::string_view name) const noexcept
    {
        return _entries.find(std::string{name}) != _entries.end();
    }

    void FrameBuffersMap::AddOrGetPersistent(std::string_view name, std::size_t stride, std::size_t count)
    {
        const std::string key{name};
        auto              it{_entries.find(key)};
        if (it == _entries.end())
        {
            auto [newIt, _]          = _entries.emplace(key, Entry{FrameBuffer(key, stride, count)});
            newIt->second.Persistent = true;
            return;
        }
        DBG_ASSERT(it->second.Owned.Stride() == stride, "FrameBuffer name reused with incompatible stride: " + key);
        if (it->second.Owned.Count() != count)
        {
            it->second.Owned.Resize(count);
        }
        it->second.Persistent = true;
    }

    void FrameBuffersMap::InjectFrom(FrameBuffersMap &store)
    {
        for (auto &[name, entry] : store._entries)
        {
            DBG_ASSERT(!_entries.contains(name), "InjectFrom: buffer '" + name + "' already exists in target map");
            _entries.emplace(name, std::move(entry));
        }
        store._entries.clear();
    }

    void FrameBuffersMap::ExtractPersistentTo(FrameBuffersMap &store)
    {
        store._entries.clear();
        for (auto it = _entries.begin(); it != _entries.end();)
        {
            if (it->second.Persistent)
            {
                store._entries.emplace(it->first, std::move(it->second));
                it = _entries.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void FrameBuffersMap::Clear()
    {
        _entries.clear();
    }

#if DEBUG_ENABLED
    void FrameBuffersMap::Dump() const
    {
        for (const auto &[name, entry] : _entries)
        {
            DBG_LOG("FrameBuffersMap: name=%s kind=owned stride=%zu count=%zu", name.c_str(), entry.Owned.Stride(),
                    entry.Owned.Count());
        }
    }
#endif

} // namespace Restir
