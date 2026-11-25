// LightComplexity.hlsl
// Unreal Engine 스타일 Light Complexity:
//  - 픽셀별 영향 라이트 개수를 불연속 색 버킷으로 매핑
//  - 씬 색을 덮어쓰고(불투명) 단색 평면으로 시각화
//  - UE 팔레트: 0=검정, 1=파랑, 2=청록, 3=초록, 4=노랑, 5=빨강, 6+=자홍
#include "Common/Functions.hlsl"
#include "Common/ConstantBuffers.hlsl"
#include "Common/SystemResources.hlsl"

#define TILE_SIZE 16

PS_Input_UV VS(uint vertexID : SV_VertexID)
{
    return FullscreenTriangleVS(vertexID);
}

// UE식 불연속 버킷 팔레트
float3 UEComplexityColor(uint count)
{
    if (count == 0) return float3(0.0f, 0.0f, 0.0f);
    if (count == 1) return float3(0.0f, 0.0f, 1.0f); // 파랑
    if (count == 2) return float3(0.0f, 1.0f, 1.0f); // 청록
    if (count == 3) return float3(0.0f, 1.0f, 0.0f); // 초록
    if (count == 4) return float3(1.0f, 1.0f, 0.0f); // 노랑
    if (count == 5) return float3(1.0f, 0.0f, 0.0f); // 빨강
    return            float3(1.0f, 0.0f, 1.0f);      // 6+ 자홍(오버플로우)
}

float3 ReconstructWorldPos(float2 uv, float depth)
{
    float2 ndc;
    ndc.x = uv.x * 2.0f - 1.0f;
    ndc.y = 1.0f - uv.y * 2.0f;

    float4 clip = float4(ndc, depth, 1.0f);
    float4 world = mul(clip, InvViewProj);
    return world.xyz / world.w;
}

float4 PS(PS_Input_UV input) : SV_TARGET
{
    uint2 pixelPos = uint2(input.position.xy);
    float d = SceneDepth.Load(int3(pixelPos, 0));

    // 배경(하늘): 0 버킷 색
    if (d <= 0.0f)
    {
        return float4(UEComplexityColor(0), 1.0f);
    }

    float3 worldPos = ReconstructWorldPos(input.uv, d);

    // 타일 컬링 결과가 아닌 전체 라이트를 직접 테스트 — 디버그 뷰용으로 정확도 우선
    uint effectiveCount = 0;
    uint total = (uint) max(TotalLightCount, 0);
    for (uint i = 0; i < total; ++i)
    {
        FLightDataFull L = AllLights[i];

        if (L.LightType == 0 || L.LightType == 1)
        {
            effectiveCount++;
            continue;
        }

        float dist = length(L.Position - worldPos);
        float atten = saturate(1.0f - dist / max(L.AttenuationRadius, 0.0001f));
        atten = pow(atten, max(L.LightFalloffExponent, 0.0001f));

        if (L.LightType == 3)
        {
            float3 ld = normalize(L.Position - worldPos);
            float spotEffect = dot(ld, -L.Direction);
            float cosOuter = cos(radians(L.OuterConeAngle));
            float cosInner = cos(radians(L.InnerConeAngle));
            cosInner = max(cosInner, cosOuter + 0.0001f);
            atten *= smoothstep(cosOuter, cosInner, spotEffect);
        }

        if (atten > 0.01f)
            effectiveCount++;
    }

    return float4(UEComplexityColor(effectiveCount), 1.0f);
}
