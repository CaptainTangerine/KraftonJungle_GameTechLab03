#include "Common/Functions.hlsl"
#include "Common/Lighting.hlsl"
#include "Common/SystemSamplers.hlsl"

#define SHADOW_THRESHOLD 0.4
#define SHADOW_SMOOTHING 0.001
#define TINT_LIGHT float4(0.95, 0.95, 0.95, 1.0)
#define TINT_SHADOW float4(0.85, 0.76, 0.69, 1.0)
#define RIM_AREA_MULTIPLIER 10
#define RIM_STRENGTH 1
#define RIM_COLOR float4(0.5, 0.5, 0.5, 1.0)
#define METALIC 0.01

Texture2D tShadowMask : register(t3);

float4 ComputeToon(PS_Input_Light input, float3 Normal, float4 DiffuseColor, float2 pixelPos)
{
    float3 ViewDir = normalize(CameraWorldPos - input.worldPos.xyz);
    
    float4 Ambient = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float4 TotalDiffuse = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float4 TotalSpecular = float4(0.0f, 0.0f, 0.0f, 0.0f);

    float viewZ = mul(input.worldPos, View).z;
    uint tileLightCount = GetTileLightCount(input.position, viewZ);
    
    // 빛 계산
    for (uint j = 0; j < tileLightCount; ++j)
    {
        FLightDataFull lightFull = GetTiledLight(pixelPos, viewZ, j);
        FLightSample sample = GetLightSample(lightFull, input.worldPos.xyz);
        
        float NdotL = (dot(sample.Direction, Normal) + 1.0) * 0.5;
        float4 Diffuse = sample.Color * NdotL;
        
        float3 HalfDirection = normalize(ViewDir + sample.Direction);
        float NdotH = ComputeLightIntensity(Normal, HalfDirection);
        float4 Specular = pow(NdotH, 16.f) * sign(NdotL);
        
        TotalDiffuse += Diffuse;
        TotalSpecular += Specular;
    }
    
    
    float4 ShadowMask = tShadowMask.Sample(LinearWrapSampler, input.texcoord);
    float ShadowThreshold = ShadowMask.r;
    if (ShadowThreshold == 0)
    {
        ShadowThreshold = SHADOW_THRESHOLD;
    }
    // 명부와 암부 구분
    float Brightness = smoothstep(ShadowThreshold, ShadowThreshold + SHADOW_SMOOTHING, TotalDiffuse + Ambient);
    // 명부와 암부 각각 틴트 적용
    float4 ShadeTint = Brightness * TINT_LIGHT + abs(1-Brightness) * TINT_SHADOW;
    float4 TintedDiffuse = DiffuseColor * ShadeTint;
    
    // 림라이팅
    float RimBase = pow((1.0 - saturate(dot(normalize(Normal.xyz), ViewDir))), RIM_AREA_MULTIPLIER);
    float4 RimLight = RimBase * RIM_COLOR * RIM_STRENGTH;
    
    float4 FinalColor = TintedDiffuse + RimLight + TotalSpecular * METALIC;
    return FinalColor;
}
