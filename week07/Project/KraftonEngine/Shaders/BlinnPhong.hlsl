#include "Common/Functions.hlsl"
#include "Common/Lighting.hlsl"

// 특정 샘플링된 라이트로부터 Blinn-Phong 결과를 계산
float4 CalculateBlinnPhong(FLightSample sample, float3 Normal, float3 ViewDir, float4 DiffuseColor)
{
    // Diffuse
    float NdotL = ComputeLightIntensity(sample.Direction, Normal);
    float4 Diffuse = sample.Color * NdotL * DiffuseColor;

    // Specular (Blinn-Phong)
    float3 HalfDirection = normalize(ViewDir + sample.Direction);
    float NdotH = ComputeLightIntensity(Normal, HalfDirection);
    float SpecularFactor = pow(NdotH, 16.f) * sign(NdotL);
    
    float4 Specular = sample.Color * SpecularFactor;

    return Diffuse + Specular;
}

float4 ComputeBlinnPhong(float4 WorldPosition, float3 Normal, float4 DiffuseColor, float2 pixelPos)
{
    float3 ViewDir = normalize(CameraWorldPos - WorldPosition.xyz);
    
    float4 Ambient = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float4 TotalDiffuseSpecular = float4(0.0f, 0.0f, 0.0f, 0.0f);

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
            TotalDiffuseSpecular += CalculateBlinnPhong(sample, Normal, ViewDir, DiffuseColor);
        }
    }

    return Ambient + TotalDiffuseSpecular;
}
