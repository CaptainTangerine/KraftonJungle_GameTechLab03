#include "Common/Functions.hlsl"
#include "Common/SystemSamplers.hlsl"

// 내 갓레이는 예뻐야만 한다.. 일반적으로 32 ~ 64개로 샘플링을 합니다.
#define NUM_SAMPLES 256
 
Texture2D<float4> GodRayOcclusionTex : register(t0);

cbuffer GodRay : register(b2)
{
    float Density;
    float Decay;
    float Weight;
    float Exposure;

    float Intensity;
    float2 ScreenSunPos;
    float GodRayPad;

    float3 LightColor;
    float GodRayPad2;
}

PS_Input_UV VS(uint vertexID : SV_VertexID)
{
    return FullscreenTriangleVS(vertexID);
}

float4 PS(PS_Input_UV input) : SV_Target
{
    float2 deltaTexCoord = (input.uv - ScreenSunPos);
    deltaTexCoord *= (1.0f / NUM_SAMPLES) * Density;

    float2 sampleUV = input.uv;
    float illuminationDecay = 1.0f;
    float additiveWeight = 0.0f;

    for (int i = 0; i < NUM_SAMPLES; ++i)
    {
        sampleUV -= deltaTexCoord;

        float sample = GodRayOcclusionTex.Sample(LinearClampSampler, sampleUV).r;
        sample *= illuminationDecay * Weight;
        additiveWeight += sample;
        illuminationDecay *= Decay;
    }

    additiveWeight *= Exposure * Intensity;
    return float4(additiveWeight * LightColor, 1.0f);
}
