#ifndef HD_RESTIR_RENDER_BUFFER_H
#define HD_RESTIR_RENDER_BUFFER_H

#include "core/frame_buffer.h"

#include "pxr/pxr.h"
#include "pxr/imaging/hd/renderBuffer.h"
#include "rendererInterface/frame_buffer_target.h"
#include "pxr/base/gf/vec3i.h"
#include "pxr/base/gf/vec4f.h"
#include <atomic>
#include <mutex>

PXR_NAMESPACE_USING_DIRECTIVE

class HdRestirRenderBuffer final : public HdRenderBuffer, public Restir::IFrameBufferTarget {
public:
    HdRestirRenderBuffer(SdfPath const& id);
    ~HdRestirRenderBuffer() override = default;

    bool Allocate(GfVec3i const& dimensions,
                          HdFormat format,
                          bool multiSampled) override;

     unsigned int GetWidth() const override { return _width; }
     unsigned int GetHeight() const override { return _height; }
     unsigned int GetDepth() const override { return 1; }
     HdFormat GetFormat() const override { return _format; }
     bool IsMultiSampled() const override { return _multiSampled; }

    void* Map() override { return _buffer.Data(); }
     void Unmap() override {}
     bool IsMapped() const override { return false; }

     void Resolve() override;
    void ResolveBucket(unsigned int startX, unsigned int startY, unsigned int endX, unsigned int endY) override;
    bool IsConverged() const override { return _converged.load(); }
    void SetConverged(bool converged) override { _converged.store(converged); }

    void GetFloatBuffer(std::vector<float>& outFloats) const override;
    void CopyFromFrameBuffer(const Restir::FrameBuffer& source) override;

    void WriteSample(GfVec3i const& pixel, GfVec4f const& color) override;
    void Write(GfVec3i const& pixel, size_t numComponents, float const* value) override;
    void Write(GfVec3i const& pixel, size_t numComponents, int const* value) override;
    void Clear(size_t numComponents, float const* value) override;
    void Clear(size_t numComponents, int const* value) override;

protected:
     void _Deallocate() override;

private:
    unsigned int _width{0};
    unsigned int _height{0};
    HdFormat _format{HdFormatInvalid};
    bool _multiSampled{false};
    Restir::FrameBuffer _buffer{"DisplayBuffer", 1, 0};
    Restir::FrameBuffer _renderBuffer{"RenderBuffer", 1, 0};
    std::vector<float> _accumBuffer;
    std::vector<int> _sampleCount;
    std::atomic<bool> _converged{false};
    std::mutex _bufferMutex;
};

#endif // HD_RESTIR_RENDER_BUFFER_H
