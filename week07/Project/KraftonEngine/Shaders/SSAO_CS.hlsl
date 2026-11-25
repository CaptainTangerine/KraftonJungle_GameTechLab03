#include "Common/SystemResources.hlsl"
#include "Common/SystemSamplers.hlsl"

#pragma pack_matrix(row_major)

// SSAO Constants (b0 in CS)
cbuffer SSAOBuffer : register(b0)
{
    float4 Samples[64];
    float4x4 Projection;
    float4x4 InvProjection;
    float4x4 View;
    float Radius;
    float Bias;
    float Intensity;
    float _pad;
};

// Output: SSAOTexture (R8_UNORM)
RWTexture2D<float> OutSSAO : register(u0);

// Reconstruct view-space position from depth
float3 GetViewPos(float2 uv)
{
    float depth = SceneDepth.SampleLevel(PointClampSampler, uv, 0).r;
    float4 clipPos = float4(uv * float2(2, -2) + float2(-1, 1), depth, 1);
    float4 viewPos = mul(clipPos, InvProjection);
    return viewPos.xyz / viewPos.w;
}

[numthreads(16, 16, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint width, height;
    OutSSAO.GetDimensions(width, height);
    if (dispatchThreadID.x >= width || dispatchThreadID.y >= height) return;

    float2 uv = (float2(dispatchThreadID.xy) + 0.5) / float2(width, height);
    
    // 1. Background Check
    float depth = SceneDepth.SampleLevel(PointClampSampler, uv, 0).r;
    float4 normalRemapped = SceneNormal.SampleLevel(PointClampSampler, uv, 0);
    
    // Reversed-Z: 0.0 is far plane
    if (depth <= 0.0001 || normalRemapped.a < 0.1) 
    {
        OutSSAO[dispatchThreadID.xy] = 1.0;
        return;
    }

    float3 viewPos = GetViewPos(uv);
    
    // 2. Transform Normal (World -> View)
    // normalRemapped is 0~1, worldNormal is -1~1
    float3 worldNormal = normalRemapped.xyz * 2.0 - 1.0;
    
    // Use only rotation part of View matrix for normal transformation
    // In row-major, normal * 3x3View is standard
    float3 viewNormal = mul(worldNormal, (float3x3)View);
    viewNormal = normalize(viewNormal);

    // 3. Sample noise rotation vector
    float2 noiseScale = float2(width, height) / 4.0;
    float3 randomVec = SceneNormal_Noise.SampleLevel(LinearWrapSampler, uv * noiseScale, 0).xyz;

    // 4. Create TBN matrix
    float3 tangent = normalize(randomVec - viewNormal * dot(randomVec, viewNormal));
    float3 bitangent = cross(viewNormal, tangent);
    float3x3 TBN = float3x3(tangent, bitangent, viewNormal);

    float occlusion = 0.0;
    for (int i = 0; i < 64; ++i)
    {
        // Get sample position in view space (Normal-oriented hemisphere)
        float3 sampleVec = mul(Samples[i].xyz, TBN);
        float3 samplePos = viewPos + sampleVec * Radius;

        // Project sample position to screen space
        float4 offset = float4(samplePos, 1.0);
        offset = mul(offset, Projection);
        offset.xyz /= offset.w;
        float2 sampleUV = offset.xy * float2(0.5, -0.5) + 0.5;

        // 5. Sample actual depth at projected UV
        float actualDepth = SceneDepth.SampleLevel(PointClampSampler, sampleUV, 0).r;
        float4 actualClipPos = float4(sampleUV * float2(2, -2) + float2(-1, 1), actualDepth, 1);
        float4 actualViewPos = mul(actualClipPos, InvProjection);
        float actualZ = actualViewPos.z / actualViewPos.w;

        // 6. Accumulate Occlusion
        float rangeCheck = smoothstep(0.0, 1.0, Radius / abs(viewPos.z - actualZ));
        
        // actualZ < samplePos.z means surface is closer to camera (occluding the sample)
        // Bias is used to prevent self-occlusion on flat surfaces
        if (actualZ <= samplePos.z - Bias)
        {
            occlusion += 1.0 * rangeCheck;
        }
    }

    occlusion = 1.0 - (occlusion / 64.0) * Intensity;
    OutSSAO[dispatchThreadID.xy] = saturate(occlusion);
}
