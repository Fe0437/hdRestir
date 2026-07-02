#include "hd_restir_render_buffer.h"

#include <algorithm>
#include <cstring>
#include <pxr/base/gf/half.h>
#include <pxr/base/gf/vec2i.h>
#include <pxr/base/gf/vec3i.h>
#include <pxr/base/gf/vec4f.h>

PXR_NAMESPACE_USING_DIRECTIVE

HdRestirRenderBuffer::HdRestirRenderBuffer(SdfPath const &id)
    : HdRenderBuffer(id), _width(0), _height(0), _format(HdFormatInvalid), _multiSampled(false)
{
    _converged.store(false);
}

bool HdRestirRenderBuffer::Allocate(GfVec3i const &dimensions, HdFormat format, bool multiSampled)
{
    std::lock_guard<std::mutex> lock(_bufferMutex);
    _width                       = dimensions[0];
    _height                      = dimensions[1];
    _format                      = format;
    _multiSampled                = multiSampled;
    const std::size_t pixelCount = static_cast<std::size_t>(_width) * static_cast<std::size_t>(_height);
    const std::size_t formatSize = HdDataSizeOfFormat(format);
    _buffer.Reconfigure(formatSize, pixelCount);
    _renderBuffer.Reconfigure(formatSize, pixelCount);
    _accumBuffer.assign(_width * _height * 4, 0.0f);
    _sampleCount.assign(_width * _height, 0);
    return true;
}

void HdRestirRenderBuffer::_Deallocate()
{
    std::lock_guard<std::mutex> lock(_bufferMutex);
    _width  = 0;
    _height = 0;
    _format = HdFormatInvalid;
    _buffer.Resize(0);
    _renderBuffer.Resize(0);
    _accumBuffer.clear();
    _sampleCount.clear();
}

template <typename T> static void _WriteOutput(HdFormat format, uint8_t *dst, size_t valueComponents, T const *value)
{
    HdFormat componentFormat = HdGetComponentFormat(format);
    size_t   componentCount  = HdGetComponentCount(format);

    for (size_t c = 0; c < componentCount; ++c)
    {
        if (componentFormat == HdFormatInt32)
        {
            ((int32_t *)dst)[c] = (c < valueComponents) ? (int32_t)(value[c]) : 0;
        }
        else if (componentFormat == HdFormatFloat16)
        {
            ((uint16_t *)dst)[c] = (c < valueComponents) ? GfHalf(value[c]).bits() : 0;
        }
        else if (componentFormat == HdFormatFloat32)
        {
            ((float *)dst)[c] = (c < valueComponents) ? (float)(value[c]) : 0.0f;
        }
        else if (componentFormat == HdFormatUNorm8)
        {
            ((uint8_t *)dst)[c] = (c < valueComponents) ? (uint8_t)std::clamp(value[c] * 255.0f, 0.0f, 255.0f) : 0;
        }
        else if (componentFormat == HdFormatSNorm8)
        {
            ((int8_t *)dst)[c] = (c < valueComponents) ? (int8_t)std::clamp(value[c] * 127.0f, -128.0f, 127.0f) : 0;
        }
    }
}

void HdRestirRenderBuffer::Write(GfVec3i const &pixel, size_t numComponents, float const *value)
{
    std::lock_guard<std::mutex> lock(_bufferMutex);
    if (pixel[0] < 0 || pixel[0] >= (int)_width || pixel[1] < 0 || pixel[1] >= (int)_height)
        return;
    size_t idx = pixel[1] * _width + pixel[0];
    if (idx >= _renderBuffer.Count())
        return;
    uint8_t *dst = _renderBuffer.Data() + idx * _renderBuffer.Stride();
    _WriteOutput(_format, dst, numComponents, value);
}

void HdRestirRenderBuffer::Write(GfVec3i const &pixel, size_t numComponents, int const *value)
{
    std::lock_guard<std::mutex> lock(_bufferMutex);
    if (pixel[0] < 0 || pixel[0] >= (int)_width || pixel[1] < 0 || pixel[1] >= (int)_height)
        return;
    size_t idx = pixel[1] * _width + pixel[0];
    if (idx >= _renderBuffer.Count())
        return;
    uint8_t *dst = _renderBuffer.Data() + idx * _renderBuffer.Stride();
    _WriteOutput(_format, dst, numComponents, value);
}

void HdRestirRenderBuffer::GetFloatBuffer(std::vector<float> &outFloats) const
{
    std::lock_guard<std::mutex> lock((const_cast<HdRestirRenderBuffer *>(this))->_bufferMutex);
    outFloats.resize(_width * _height * 3);
    for (size_t i = 0; i < _width * _height; ++i)
    {
        float invCount       = (_sampleCount[i] > 0) ? (1.0f / (float)_sampleCount[i]) : 1.0f;
        outFloats[i * 3 + 0] = _accumBuffer[i * 4 + 0] * invCount;
        outFloats[i * 3 + 1] = _accumBuffer[i * 4 + 1] * invCount;
        outFloats[i * 3 + 2] = _accumBuffer[i * 4 + 2] * invCount;
    }
}

void HdRestirRenderBuffer::CopyFromFrameBuffer(const Restir::FrameBuffer &source)
{
    std::lock_guard<std::mutex> lock(_bufferMutex);

    if (source.Count() != _renderBuffer.Count())
    {
        return;
    }

    if (source.Stride() == _renderBuffer.Stride())
    {
        std::memcpy(_renderBuffer.Data(), source.Data(), source.SizeBytes());
        return;
    }

    const HdFormat componentFormat{HdGetComponentFormat(_format)};
    if (componentFormat != HdFormatFloat32 && componentFormat != HdFormatFloat16 && componentFormat != HdFormatUNorm8 &&
        componentFormat != HdFormatSNorm8 && componentFormat != HdFormatInt32)
    {
        return;
    }

    if (source.Stride() % sizeof(float) != 0)
    {
        return;
    }

    const size_t sourceComponents{source.Stride() / sizeof(float)};
    auto         sourceValues{source.As<const float>()};
    for (size_t idx{0}; idx < source.Count(); ++idx)
    {
        const float *value{sourceValues.data() + idx * sourceComponents};
        uint8_t     *dst{_renderBuffer.Data() + idx * _renderBuffer.Stride()};
        _WriteOutput(_format, dst, sourceComponents, value);
    }
}

void HdRestirRenderBuffer::WriteSample(GfVec3i const &pixel, GfVec4f const &color)
{
    std::lock_guard<std::mutex> lock(_bufferMutex);
    if (pixel[0] < 0 || pixel[0] >= (int)_width || pixel[1] < 0 || pixel[1] >= (int)_height)
    {
        return;
    }

    size_t idx = pixel[1] * _width + pixel[0];
    if (idx * 4 + 3 >= _accumBuffer.size())
        return;

    _accumBuffer[idx * 4 + 0] += color[0];
    _accumBuffer[idx * 4 + 1] += color[1];
    _accumBuffer[idx * 4 + 2] += color[2];
    _accumBuffer[idx * 4 + 3] += color[3];
    _sampleCount[idx]++;

    float invCount = 1.0f / (float)_sampleCount[idx];
    float avg[4]   = {_accumBuffer[idx * 4 + 0] * invCount, _accumBuffer[idx * 4 + 1] * invCount,
                      _accumBuffer[idx * 4 + 2] * invCount, _accumBuffer[idx * 4 + 3] * invCount};

    uint8_t *dst = _renderBuffer.Data() + idx * _renderBuffer.Stride();
    _WriteOutput(_format, dst, 4, avg);
}

void HdRestirRenderBuffer::Resolve()
{
    // Runs on the main/Hydra thread every frame while render worker threads
    // write _renderBuffer and (on framing changes) Allocate()/_Deallocate()
    // resize it. Take _bufferMutex like every other accessor: without it the
    // memcpy reads a buffer mid-write (visible as white spikes) or mid-realloc
    // (a use-after-free / access-violation crash).
    std::lock_guard<std::mutex> lock(_bufferMutex);
    // Copy from background render buffer to front display buffer
    if (_buffer.SizeBytes() == _renderBuffer.SizeBytes() && !_buffer.Empty())
    {
        std::memcpy(_buffer.Data(), _renderBuffer.Data(), _buffer.SizeBytes());
    }
}

void HdRestirRenderBuffer::ResolveBucket(unsigned int startX, unsigned int startY, unsigned int endX, unsigned int endY)
{
    std::lock_guard<std::mutex> lock(_bufferMutex);
    if (_buffer.Empty() || _buffer.SizeBytes() != _renderBuffer.SizeBytes())
        return;

    endX = std::min(endX, _width);
    endY = std::min(endY, _height);
    if (startX >= endX || startY >= endY)
        return;

    size_t rowBytes = (endX - startX) * _buffer.Stride();

    for (unsigned int y = startY; y < endY; ++y)
    {
        size_t idx = (y * _width + startX) * _buffer.Stride();
        std::memcpy(_buffer.Data() + idx, _renderBuffer.Data() + idx, rowBytes);
    }
}

void HdRestirRenderBuffer::Clear(size_t numComponents, float const *value)
{
    std::lock_guard<std::mutex> lock(_bufferMutex);
    // Do not visually clear the buffer to prevent black flashes, just reset the accumulators
    _accumBuffer.assign(_width * _height * 4, 0.0f);
    _sampleCount.assign(_width * _height, 0);
}

void HdRestirRenderBuffer::Clear(size_t numComponents, int const *value)
{
    std::lock_guard<std::mutex> lock(_bufferMutex);
    // Do not visually clear the buffer to prevent black flashes, just reset the accumulators
    _accumBuffer.assign(_width * _height * 4, 0.0f);
    _sampleCount.assign(_width * _height, 0);
}
