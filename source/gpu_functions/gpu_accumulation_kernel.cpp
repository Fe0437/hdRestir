#include "gpu_accumulation_kernel.h"

#if GPU_ENABLED

#include <cassert>
#include <cstring>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

import lightRHI;

#ifndef GPU_SHADER_DIR
#error "GPU_SHADER_DIR must be defined by the build (see CMakeLists.txt)"
#endif

#ifndef GPU_SHADER_BACKEND_DIR
#error "GPU_SHADER_BACKEND_DIR must be defined by the build (see CMakeLists.txt)"
#endif

#ifndef GPU_SHADER_EXTENSION
#error "GPU_SHADER_EXTENSION must be defined by the build (see CMakeLists.txt)"
#endif

#ifndef GPU_SHADER_FORMAT
#error "GPU_SHADER_FORMAT must be defined by the build (see CMakeLists.txt)"
#endif

namespace Restir::Gpu
{

    namespace
    {

        struct alignas(8) PushConstants
        {
            std::uint64_t colorInAddr;
            std::uint64_t colorOutAddr;
            std::uint64_t accumAddr;
            std::uint64_t lumSumAddr;
            std::uint64_t lumSumSqAddr;
            std::uint32_t width;
            std::uint32_t height;
            float         invN;
            std::uint32_t fireflyEnable;
            std::uint32_t metricsEnabled;
        };

        constexpr std::size_t kBytesPerPixel{4 * sizeof(float)};

        [[nodiscard]] std::string _shaderArtifactPath(const std::string &name)
        {
            return std::string(GPU_SHADER_DIR) + "/" + GPU_SHADER_BACKEND_DIR + "/" + name + ".generated" +
                   GPU_SHADER_EXTENSION;
        }

        [[nodiscard]] std::vector<std::byte> _readShaderArtifactBytes(const std::string &path)
        {
            std::ifstream file{path, std::ios::binary | std::ios::ate};
            if (!file)
                throw std::runtime_error("[HdRestir] failed to open compiled shader artifact: " + path);

            const std::streamsize size{file.tellg()};
            file.seekg(0);
            std::vector<std::byte> bytes{static_cast<std::size_t>(size)};
            if (size > 0 && !file.read(reinterpret_cast<char *>(bytes.data()), size))
                throw std::runtime_error("[HdRestir] failed to read compiled shader artifact: " + path);
            return bytes;
        }

        [[nodiscard]] rhi::ShaderDesc _loadShaderArtifact(const std::string &name, std::string_view entryPoint,
                                                          rhi::ShaderStage stage)
        {
            // Process-wide cache: one HdRestir process can host multiple
            // AccumulationKernel instances (e.g. split-screen left/right
            // pipelines, or multiple render delegate instances), each on its
            // own render thread, so first-load access must be serialized.
            static std::mutex                                              cacheMutex;
            static std::unordered_map<std::string, std::vector<std::byte>> cache;

            const std::string path{_shaderArtifactPath(name)};

            std::lock_guard<std::mutex> lock{cacheMutex};
            auto [it, inserted]{cache.try_emplace(name)};
            if (inserted)
            {
                it->second = _readShaderArtifactBytes(path);
            }
            const auto &bytes{it->second};

            constexpr rhi::ShaderFormat format{GPU_SHADER_FORMAT};

            rhi::ShaderArtifactView artifact{
                .Format     = format,
                .Stage      = stage,
                .EntryPoint = entryPoint,
                .Data       = bytes,
            };
            auto desc{rhi::toShaderDesc(artifact)};
            if (!desc.has_value())
                throw std::runtime_error("[HdRestir] failed to interpret compiled shader artifact: " + path);
            return *desc;
        }

        // Loads the artifact tools/compile_shaders.py compiled from the
        // source/gpu_functions/shaders registry. The RHI core never compiles
        // Slang itself — this mirrors external/rhi/tests/shader_artifact_loader.h.
        [[nodiscard]] rhi::ShaderDesc _loadAccumulationShader()
        {
            return _loadShaderArtifact("accumulation_cs", "cs_main", rhi::ShaderStage::Compute);
        }

    } // namespace

    struct AccumulationKernel::Impl
    {
        std::unique_ptr<rhi::IDevice> _device;
        rhi::PipelineHandle           _pipeline;
        rhi::BufferHandle             _colorIn;
        rhi::BufferHandle             _colorOut;
        rhi::BufferHandle             _accum;
        std::uint32_t                 _bufferWidth{0};
        std::uint32_t                 _bufferHeight{0};
#if METRICS_ENABLED
        rhi::BufferHandle _lumSum;
        rhi::BufferHandle _lumSumSq;
        bool              _metricsBuffersReady{false};
#endif

        void _releaseBuffers()
        {
            if (!_device)
            {
                return;
            }
            if (_colorIn.valid())
            {
                _device->destroyBuffer(_colorIn);
                _colorIn = {};
            }
            if (_colorOut.valid())
            {
                _device->destroyBuffer(_colorOut);
                _colorOut = {};
            }
            if (_accum.valid())
            {
                _device->destroyBuffer(_accum);
                _accum = {};
            }
#if METRICS_ENABLED
            if (_lumSum.valid())
            {
                _device->destroyBuffer(_lumSum);
                _lumSum = {};
            }
            if (_lumSumSq.valid())
            {
                _device->destroyBuffer(_lumSumSq);
                _lumSumSq = {};
            }
            _metricsBuffersReady = false;
#endif
            _bufferWidth  = 0;
            _bufferHeight = 0;
        }

        void _ensureDevice()
        {
            if (_device)
            {
                return;
            }

            _device = rhi::createDevice(rhi::DeviceDesc{
                .EnableValidation = false,
                .AppName          = "HdRestir",
            });

            const rhi::ComputePipelineDesc pipelineDesc{
                .Shader          = _loadAccumulationShader(),
                .ThreadGroupSize = {8, 8, 1}, // must match accumulation.slang's numthreads(8, 8, 1)
                .DebugName       = "GpuAccumulationPass",
            };
            _pipeline = _device->createComputePipeline(pipelineDesc);
            assert(_pipeline.valid() && "Failed to create GpuAccumulationPass compute pipeline");
        }

        void _ensureBuffers(std::uint32_t width, std::uint32_t height)
        {
            if (_bufferWidth == width && _bufferHeight == height && _colorIn.valid())
            {
                return;
            }

            _releaseBuffers();

            const std::uint64_t byteSize{static_cast<std::uint64_t>(width) * height * kBytesPerPixel};

            _colorIn  = _device->createBuffer(rhi::BufferDesc{
                .Size       = byteSize,
                .Usage      = rhi::BufferUsage::Storage | rhi::BufferUsage::DeviceAddress,
                .MemoryType = rhi::MemoryType::CpuToGpu,
                .DebugName  = "GpuAccumulationPass.colorIn",
            });
            _colorOut = _device->createBuffer(rhi::BufferDesc{
                .Size       = byteSize,
                .Usage      = rhi::BufferUsage::Storage | rhi::BufferUsage::DeviceAddress,
                .MemoryType = rhi::MemoryType::GpuToCpu,
                .DebugName  = "GpuAccumulationPass.colorOut",
            });
            _accum    = _device->createBuffer(rhi::BufferDesc{
                .Size       = byteSize,
                .Usage      = rhi::BufferUsage::Storage | rhi::BufferUsage::DeviceAddress,
                .MemoryType = rhi::MemoryType::CpuToGpu, // uploaded from and read back to the caller every frame
                .DebugName  = "GpuAccumulationPass.accum",
            });

            _bufferWidth  = width;
            _bufferHeight = height;
        }

#if METRICS_ENABLED
        // Lazily allocated — only when the caller first asks for metrics, so
        // the common (metrics-off) path never pays for them.
        void _ensureMetricsBuffers()
        {
            if (_metricsBuffersReady)
            {
                return;
            }

            const std::uint64_t byteSize{static_cast<std::uint64_t>(_bufferWidth) * _bufferHeight * sizeof(float)};

            _lumSum   = _device->createBuffer(rhi::BufferDesc{
                .Size       = byteSize,
                .Usage      = rhi::BufferUsage::Storage | rhi::BufferUsage::DeviceAddress,
                .MemoryType = rhi::MemoryType::CpuToGpu, // read back into the caller's array every frame
                .DebugName  = "GpuAccumulationPass.lumSum",
            });
            _lumSumSq = _device->createBuffer(rhi::BufferDesc{
                .Size       = byteSize,
                .Usage      = rhi::BufferUsage::Storage | rhi::BufferUsage::DeviceAddress,
                .MemoryType = rhi::MemoryType::CpuToGpu, // uploaded from and read back to the caller every frame
                .DebugName  = "GpuAccumulationPass.lumSumSq",
            });

            _metricsBuffersReady = true;
        }
#endif
    };

    AccumulationKernel::AccumulationKernel() : _impl{std::make_unique<Impl>()} {}

    AccumulationKernel::AccumulationKernel(AccumulationKernel &&) noexcept            = default;
    AccumulationKernel &AccumulationKernel::operator=(AccumulationKernel &&) noexcept = default;

    AccumulationKernel::~AccumulationKernel()
    {
        if (!_impl)
        {
            return;
        }
        _impl->_releaseBuffers();
        if (_impl->_device && _impl->_pipeline.valid())
        {
            _impl->_device->destroyPipeline(_impl->_pipeline);
        }
        if (_impl->_device)
        {
            _impl->_device->waitIdle();
        }
    }

#if METRICS_ENABLED
    void AccumulationKernel::RunFrame(gsl::span<pxr::GfVec4f> colorInOut, gsl::span<pxr::GfVec4f> accumInOut,
                                      std::uint32_t width, std::uint32_t height, std::uint32_t frameIndex,
                                      bool fireflyEnable, gsl::span<float> lumSumInOut, gsl::span<float> lumSumSqInOut)
#else
    void AccumulationKernel::RunFrame(gsl::span<pxr::GfVec4f> colorInOut, gsl::span<pxr::GfVec4f> accumInOut,
                                      std::uint32_t width, std::uint32_t height, std::uint32_t frameIndex,
                                      bool fireflyEnable)
#endif
    {
        const std::size_t pixelCount{static_cast<std::size_t>(width) * height};
        static_assert(sizeof(pxr::GfVec4f) == kBytesPerPixel);
        Expects(colorInOut.size() == pixelCount);
        Expects(accumInOut.size() == pixelCount);
#if METRICS_ENABLED
        Expects(lumSumInOut.empty() == lumSumSqInOut.empty());
        Expects(lumSumInOut.empty() || lumSumInOut.size() == pixelCount);
        Expects(lumSumSqInOut.empty() || lumSumSqInOut.size() == pixelCount);
#endif

        _impl->_ensureDevice();
        _impl->_ensureBuffers(width, height);

        auto &device{*_impl->_device};

        const std::uint64_t byteSize{static_cast<std::uint64_t>(width) * height * kBytesPerPixel};

        // Upload current frame's color buffer.
        {
            rhi::MappedBuffer mapped{device.mapBuffer(_impl->_colorIn)};
            assert(mapped.valid() && "Failed to map GpuAccumulationPass.colorIn");
            std::memcpy(mapped.Data, colorInOut.data(), static_cast<std::size_t>(byteSize));
            device.unmapBuffer(_impl->_colorIn);
        }

        // Upload the caller-owned running color sum so far. accumInOut is the
        // single source of truth for accumulation state (RenderContext's
        // persistent buffer store) — this device buffer is pure compute
        // scratch, fully overwritten from it every call.
        {
            rhi::MappedBuffer mapped{device.mapBuffer(_impl->_accum)};
            assert(mapped.valid() && "Failed to map GpuAccumulationPass.accum for upload");
            std::memcpy(mapped.Data, accumInOut.data(), static_cast<std::size_t>(byteSize));
            device.unmapBuffer(_impl->_accum);
        }

#if METRICS_ENABLED
        const bool metricsEnable{!lumSumInOut.empty() && !lumSumSqInOut.empty()};
        if (metricsEnable)
        {
            _impl->_ensureMetricsBuffers();

            const std::uint64_t lumByteSize{static_cast<std::uint64_t>(width) * height * sizeof(float)};

            rhi::MappedBuffer sumMapped{device.mapBuffer(_impl->_lumSum)};
            assert(sumMapped.valid() && "Failed to map GpuAccumulationPass.lumSum for upload");
            std::memcpy(sumMapped.Data, lumSumInOut.data(), static_cast<std::size_t>(lumByteSize));
            device.unmapBuffer(_impl->_lumSum);

            rhi::MappedBuffer sumSqMapped{device.mapBuffer(_impl->_lumSumSq)};
            assert(sumSqMapped.valid() && "Failed to map GpuAccumulationPass.lumSumSq for upload");
            std::memcpy(sumSqMapped.Data, lumSumSqInOut.data(), static_cast<std::size_t>(lumByteSize));
            device.unmapBuffer(_impl->_lumSumSq);
        }
#else
        constexpr bool metricsEnable{false};
#endif

        const float invN{1.0f / static_cast<float>(frameIndex + 1)};

        const PushConstants pc{
            .colorInAddr  = device.bufferAddress(_impl->_colorIn).Address,
            .colorOutAddr = device.bufferAddress(_impl->_colorOut).Address,
            .accumAddr    = device.bufferAddress(_impl->_accum).Address,
#if METRICS_ENABLED
            .lumSumAddr   = metricsEnable ? device.bufferAddress(_impl->_lumSum).Address : 0,
            .lumSumSqAddr = metricsEnable ? device.bufferAddress(_impl->_lumSumSq).Address : 0,
#else
            .lumSumAddr   = 0,
            .lumSumSqAddr = 0,
#endif
            .width          = width,
            .height         = height,
            .invN           = invN,
            .fireflyEnable  = fireflyEnable ? 1u : 0u,
            .metricsEnabled = metricsEnable ? 1u : 0u,
        };

        auto cmd{device.createCommandList(rhi::QueueType::Compute, "GpuAccumulationPass.cmd")};
        cmd->begin();
        cmd->setPipeline(_impl->_pipeline);
        cmd->setPushConstants(pc);
        cmd->dispatch((width + 7) / 8, (height + 7) / 8, 1);
        cmd->end();

        device.waitForFence(device.submit(*cmd));

        // Read back the normalized color for downstream (still-CPU) passes.
        {
            rhi::MappedBuffer mapped{device.mapBuffer(_impl->_colorOut)};
            assert(mapped.valid() && "Failed to map GpuAccumulationPass.colorOut");
            std::memcpy(colorInOut.data(), mapped.Data, static_cast<std::size_t>(byteSize));
            device.unmapBuffer(_impl->_colorOut);
        }

        // Read the updated running sum back into the caller's persistent
        // buffer — this is the only place that sum is retained.
        {
            rhi::MappedBuffer mapped{device.mapBuffer(_impl->_accum)};
            assert(mapped.valid() && "Failed to map GpuAccumulationPass.accum for readback");
            std::memcpy(accumInOut.data(), mapped.Data, static_cast<std::size_t>(byteSize));
            device.unmapBuffer(_impl->_accum);
        }

#if METRICS_ENABLED
        if (metricsEnable)
        {
            const std::uint64_t lumByteSize{static_cast<std::uint64_t>(width) * height * sizeof(float)};

            rhi::MappedBuffer sumMapped{device.mapBuffer(_impl->_lumSum)};
            assert(sumMapped.valid() && "Failed to map GpuAccumulationPass.lumSum for readback");
            std::memcpy(lumSumInOut.data(), sumMapped.Data, static_cast<std::size_t>(lumByteSize));
            device.unmapBuffer(_impl->_lumSum);

            rhi::MappedBuffer sumSqMapped{device.mapBuffer(_impl->_lumSumSq)};
            assert(sumSqMapped.valid() && "Failed to map GpuAccumulationPass.lumSumSq for readback");
            std::memcpy(lumSumSqInOut.data(), sumSqMapped.Data, static_cast<std::size_t>(lumByteSize));
            device.unmapBuffer(_impl->_lumSumSq);
        }
#endif
    }

} // namespace Restir::Gpu

#endif // GPU_ENABLED
