#include "frame_buffer.h"

#include <utility>

namespace Restir
{

    FrameBuffer::FrameBuffer(std::string name, std::size_t stride, std::size_t count)
        : _name(std::move(name)), _stride(stride), _count(count)
    {
        Expects(stride > 0);
        _storage.resize(stride * count);
    }

    void FrameBuffer::Reconfigure(std::size_t newStride, std::size_t newCount)
    {
        Expects(newStride > 0);
        _stride = newStride;
        _count  = newCount;
        _storage.resize(_stride * _count);
    }

    void FrameBuffer::Resize(std::size_t newCount)
    {
        _count = newCount;
        _storage.resize(_stride * _count);
    }

} // namespace Restir
