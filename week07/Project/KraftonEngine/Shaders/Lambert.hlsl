#include "Common/Functions.hlsl"
#include "Common/Lighting.hlsl"

// 특정 샘플링된 라이트로부터 Lambert 결과를 계산
float4 CalculateLambert(FLightSample sample, float3 Normal, float4 DiffuseColor)
{
    // Diffuse
    float NdotL = ComputeLightIntensity(sample.Direction, Normal);
    float4 Diffuse = sample.Color * NdotL * DiffuseColor;

    return Diffuse;
}

float4 ComputeLambert(float4 WorldPosition, float3 Normal, float4 DiffuseColor, float2 pixelPos)
{
    float4 Ambient = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float4 TotalDiffuse = float4(0.0f, 0.0f, 0.0f, 0.0f);

    float viewZ = mul(WorldPosition, View).z;
    uint tileLightCount = GetTileLightCount(pixelPos, viewZ);
    
    for (uint j = 0; j < tileLightCount; ++j)
    {
        FLightDataFull lightFull = GetTiledLight(pixelPos, viewZ, j);
        FLightSample sample = GetLightSample(lightFull, WorldPosition.xyz);
        
        if (sample.LightType == 0) // Ambient
        {
            Ambient += sample.Color;
        }
        else
        {
            TotalDiffuse += CalculateLambert(sample, Normal, DiffuseColor);
        }
    }

    return Ambient + TotalDiffuse;
}
