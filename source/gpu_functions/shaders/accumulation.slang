// accumulation.slang
// GPU port of AccumulationPass (source/passes/accumulation_pass.cpp):
// running-average accumulation plus an optional 3x3 neighborhood-max
// firefly clamp. Must match the CPU reference bit-for-bit.
//
// Metrics (lumSum/lumSumSq) are always present in this compiled artifact —
// tools/compile_shaders.py (the shared LightRHI shader pipeline) compiles one
// artifact per registry entry with no preprocessor defines, so there's no
// metrics-off variant at the shader level. metricsEnabled is a runtime flag;
// when the host (gpu_accumulation_kernel.cpp, gated on the host-side
// METRICS_ENABLED build flag) leaves it 0, the branch below is skipped and
// lumSumAddr/lumSumSqAddr are never dereferenced (left as 0/dummy addresses).

struct Constants
{
    uint64_t colorInAddr;
    uint64_t colorOutAddr;
    uint64_t accumAddr;
    uint64_t lumSumAddr;
    uint64_t lumSumSqAddr;
    uint32_t width;
    uint32_t height;
    float    invN;
    uint32_t fireflyEnable;
    uint32_t metricsEnabled;
};

[[vk::binding(30, 0)]] ConstantBuffer<Constants> g_constants : register(b30);

float Luminance(float4 c)
{
    return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z;
}

// Genuine 2D dispatch — pass ComputePipelineDesc::ThreadGroupSize = {8,8,1}
// when creating this pipeline so Metal's dispatchThreadgroups() actually uses
// an 8x8 threadgroup instead of guessing (1,1,1)-per-axis from the flattened
// maxTotalThreadsPerThreadgroup() total.
[shader("compute")]
[numthreads(8, 8, 1)]
void cs_main(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= g_constants.width || tid.y >= g_constants.height)
        return;

    float4 *colorIn  = (float4 *)g_constants.colorInAddr;
    float4 *colorOut = (float4 *)g_constants.colorOutAddr;
    float4 *accum    = (float4 *)g_constants.accumAddr;

    uint   px    = tid.x;
    uint   py    = tid.y;
    uint   i     = py * g_constants.width + px;
    float4 color = colorIn[i];

    if (g_constants.fireflyEnable != 0)
    {
        float maxNeighborLum = 0.0f;
        for (int dy = -1; dy <= 1; ++dy)
        {
            for (int dx = -1; dx <= 1; ++dx)
            {
                if (dx == 0 && dy == 0)
                    continue;

                int  nx   = clamp(int(px) + dx, 0, int(g_constants.width) - 1);
                int  ny   = clamp(int(py) + dy, 0, int(g_constants.height) - 1);
                uint nIdx = uint(ny) * g_constants.width + uint(nx);
                maxNeighborLum = max(maxNeighborLum, Luminance(colorIn[nIdx]));
            }
        }

        float threshold = maxNeighborLum * 4.0f + 0.5f;
        float lum        = Luminance(color);
        if (lum > threshold && lum > 0.0f)
        {
            float scale = threshold / lum;
            color.x *= scale;
            color.y *= scale;
            color.z *= scale;
        }
    }

    if (g_constants.metricsEnabled != 0)
    {
        // float precision here vs double on the CPU AccumulationPass — an
        // intentional, documented approximation (Apple GPUs have no double).
        float *lumSum   = (float *)g_constants.lumSumAddr;
        float *lumSumSq = (float *)g_constants.lumSumSqAddr;
        float  lum      = Luminance(color);
        lumSum[i] += lum;
        lumSumSq[i] += lum * lum;
    }

    float4 acc  = accum[i] + color;
    accum[i]    = acc;
    colorOut[i] = acc * g_constants.invN;
}
