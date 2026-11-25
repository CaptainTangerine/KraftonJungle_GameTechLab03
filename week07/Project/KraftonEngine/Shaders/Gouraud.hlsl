#include "Common/Functions.hlsl"
#include "Common/Lighting.hlsl"

// 특정 샘플링된 라이트로부터 Gouraud(Blinn-Phong 기반) 결과를 계산
float4 CalculateGouraud(FLightSample sample, float3 Normal, float3 ViewDir, float4 DiffuseColor)
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

float4 ComputeGouraud(float4 WorldPosition, float3 Normal, float4 DiffuseColor)
{
    float4 Ambient = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float4 TotalDiffuseSpecular = float4(0.0f, 0.0f, 0.0f, 0.0f);

    float3 ViewDir = normalize(CameraWorldPos - WorldPosition.xyz);
    
    // Gouraud는 Vertex Shader에서 실행되므로 Tiled Culling 대신 전체 라이트 루프를 수행합니다.
    for (int i = 0; i < TotalLightCount; ++i)
    {
        FLightDataFull lightFull = AllLights[i];
        FLightSample sample = GetLightSample(lightFull, WorldPosition.xyz);
        
        if (sample.LightType == 0) // Ambient
        {
            Ambient += sample.Color;
        }
        else
        {
            TotalDiffuseSpecular += CalculateGouraud(sample, Normal, ViewDir, DiffuseColor);
        }
    }

    return Ambient + TotalDiffuseSpecular;
}
