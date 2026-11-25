#ifndef LIGHTING_HLSL
#define LIGHTING_HLSL

#include "Common/ConstantBuffers.hlsl"

#define TILE_SIZE 16

// ── Structures ──

// 특정 픽셀 위치에서 계산된 통합 라이팅 정보
struct FLightSample
{
    float3 Direction; // 픽셀 -> 광원 방향 (단위 벡터)
    float4 Color;     // 감쇄가 적용된 최종 광색상 (RGB * Intensity * Attenuation)
    int LightType;    // 분기가 필요할 경우를 대비해 남겨둠
};

// ── Cluster Data Provider ──

uint GetClusterIndex(float2 pixelPos, float viewZ)
{
    uint2 tileID = uint2(pixelPos) / TILE_SIZE;
    uint numTilesX = (uint)ViewportSize.x / TILE_SIZE + ((uint)ViewportSize.x % TILE_SIZE != 0 ? 1 : 0);
    uint numTilesY = (uint)ViewportSize.y / TILE_SIZE + ((uint)ViewportSize.y % TILE_SIZE != 0 ? 1 : 0);
    
    uint slice = (uint)max(0.0f, log2(max(GNearClip, viewZ)) * ClusterScale + ClusterBias);
    slice = min(slice, 31); // 32 slices max

    return slice * (numTilesX * numTilesY) + tileID.y * numTilesX + tileID.x;
}

uint GetTileLightCount(float2 pixelPos, float viewZ)
{
    uint clusterIndex = GetClusterIndex(pixelPos, viewZ);
    return LightGrid[clusterIndex].LightCount;
}

FLightDataFull GetTiledLight(float2 pixelPos, float viewZ, uint localIdx)
{
    uint clusterIndex = GetClusterIndex(pixelPos, viewZ);
    FTileInfo tile = LightGrid[clusterIndex];
    uint lightIdx = LightIndexList[tile.StartOffset + localIdx];
    return AllLights[lightIdx];
}

// ── Light Sampling Logic ──

FLightSample GetLightSample(FLightDataFull light, float3 worldPos)
{
    FLightSample sample;
    sample.LightType = light.LightType;
    sample.Direction = float3(0, 0, 0);
    sample.Color = float4(0, 0, 0, 0);

    if (light.LightType == 0) // Ambient
    {
        sample.Color = light.LightColor * light.Intensity;
    }
    else if (light.LightType == 1) // Directional
    {
        sample.Direction = normalize(-light.Direction);
        sample.Color = light.LightColor * light.Intensity;
    }
    else if (light.LightType == 2) // Point
    {
        float3 lightVec = light.Position - worldPos;
        float dist = length(lightVec);
        sample.Direction = normalize(lightVec);
        
        // 역제곱 감쇄 (UE 스타일: (1 - (d/r)^4)^2 / (d^2 + 1))
        float ratio = saturate(dist / light.AttenuationRadius);
        float ratio2 = ratio * ratio;
        float ratio4 = ratio2 * ratio2;
        float attenuation = pow(saturate(1.0 - ratio4), 2.0) / (dist * dist + 1.0);
        
        sample.Color = light.LightColor * light.Intensity * attenuation;
    }
    else if (light.LightType == 3) // Spot
    {
        float3 lightVec = light.Position - worldPos;
        float dist = length(lightVec);
        sample.Direction = normalize(lightVec);
        
        // 거리 감쇄
        float ratio = saturate(dist / light.AttenuationRadius);
        float ratio2 = ratio * ratio;
        float ratio4 = ratio2 * ratio2;
        float attenuation = pow(saturate(1.0 - ratio4), 2.0) / (dist * dist + 1.0);
        
        // 각도 감쇄
        float cosAngle = dot(sample.Direction, normalize(-light.Direction));
        float cosInner = cos(radians(light.InnerConeAngle));
        float cosOuter = cos(radians(light.OuterConeAngle));
        float spotAttenuation = saturate((cosAngle - cosOuter) / (cosInner - cosOuter));
        spotAttenuation = pow(spotAttenuation, light.LightFalloffExponent);
        
        sample.Color = light.LightColor * light.Intensity * attenuation * spotAttenuation;
    }

    return sample;
}

// ── Helper Functions ──

float ComputeLightIntensity(float3 LightDirection, float3 Normal)
{
    return max(dot(LightDirection, Normal), 0.f);
}

float3 ComputeNormalByNormalMap(float3 tangent, float3 normal, float3 NormalMapSample)
{
    float3 T = tangent;
    float3 N = normal;
    T = normalize(T - dot(T, N) * N);
    float3 B = cross(N, T);
    float3x3 TBN = float3x3(T, B, N);
    float3 TangentNormal = NormalMapSample * 2.f - 1.f;
    return normalize(mul(TangentNormal, TBN));
}

#endif // LIGHTING_HLSL
