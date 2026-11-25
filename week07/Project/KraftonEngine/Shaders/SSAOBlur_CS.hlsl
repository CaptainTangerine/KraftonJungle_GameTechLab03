#include "Common/SystemResources.hlsl"
#include "Common/SystemSamplers.hlsl"

// SSAO Main 결과물은 t0로 들어옴 (이전 패스에서 명시적으로 바인딩)
Texture2D InSSAO : register(t0);

// Output: SSAOBlurredTexture (R8_UNORM)
RWTexture2D<float> OutBlurredSSAO : register(u0);

[numthreads(16, 16, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint width, height;
    OutBlurredSSAO.GetDimensions(width, height);
    if (dispatchThreadID.x >= width || dispatchThreadID.y >= height) return;

    float2 texelSize = 1.0 / float2(width, height);
    float2 uv = (float2(dispatchThreadID.xy) + 0.5) * texelSize;
    
    // Center pixel depth for bilateral weight
    float centerDepth = SceneDepth.SampleLevel(PointClampSampler, uv, 0).r;
    
    // If background, no need to blur carefully
    if (centerDepth <= 0.0001)
    {
        OutBlurredSSAO[dispatchThreadID.xy] = 1.0;
        return;
    }

    float result = 0.0;
    float totalWeight = 0.0;

    // 4x4 Bilateral Blur
    for (int x = -2; x < 2; ++x)
    {
        for (int y = -2; y < 2; ++y)
        {
            float2 offset = float2(float(x), float(y)) * texelSize;
            float2 sampleUV = uv + offset;
            
            float sampleSSAO = InSSAO.SampleLevel(LinearClampSampler, sampleUV, 0).r;
            float sampleDepth = SceneDepth.SampleLevel(PointClampSampler, sampleUV, 0).r;
            
            // Bilateral weight: weight drops as depth difference increases
            // We use a simple linear falloff based on depth similarity
            float weight = 1.0 / (1.0 + abs(centerDepth - sampleDepth) * 1000.0);
            
            result += sampleSSAO * weight;
            totalWeight += weight;
        }
    }

    OutBlurredSSAO[dispatchThreadID.xy] = result / totalWeight;
}
