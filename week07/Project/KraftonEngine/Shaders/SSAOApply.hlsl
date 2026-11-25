#include "Common/Functions.hlsl"
#include "Common/SystemResources.hlsl"
#include "Common/SystemSamplers.hlsl"

// SSAO Blur 결과물은 t0 (DiffuseSRV)로 들어옴
Texture2D SSAOBlurredTex : register(t0);

PS_Input_UV VS(uint vertexID : SV_VertexID)
{
    return FullscreenTriangleVS(vertexID);
}

float4 PS(PS_Input_UV input) : SV_TARGET
{
    // LinearClampSampler (s0) 사용
    float occlusion = SSAOBlurredTex.SampleLevel(LinearClampSampler, input.uv, 0).r;

    // Multiplicative blend: RGB * Dest. Alpha = 1.0 (to preserve original alpha)
    return float4(occlusion, occlusion, occlusion, 1.0);
}

