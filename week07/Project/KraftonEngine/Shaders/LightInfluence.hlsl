// LightInfluence.hlsl
// 라이트의 실제 영향 범위를 씬 depth 위에 시각화.
// 타일 라이트 리스트를 순회하며 각 라이트가 픽셀에 기여하는 attenuation 합을 색으로 출력.
#include "Common/Functions.hlsl"
#include "Common/ConstantBuffers.hlsl"
#include "Common/SystemResources.hlsl"

#define TILE_SIZE 16

PS_Input_UV VS(uint vertexID : SV_VertexID)
{
    return FullscreenTriangleVS(vertexID);
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
    int2 coord = int2(input.position.xy);
    float d = SceneDepth.Load(int3(coord, 0));

    // 배경(하늘)은 어둡게 처리
    if (d <= 0.0f)
        return float4(0.02f, 0.02f, 0.05f, 1.0f);

    float3 worldPos = ReconstructWorldPos(input.uv, d);

    uint2 tileID = uint2(coord) / TILE_SIZE;
    uint numTilesX = (uint) ViewportSize.x / TILE_SIZE + ((uint) ViewportSize.x % TILE_SIZE != 0 ? 1 : 0);
    uint tileIndex = tileID.y * numTilesX + tileID.x;

    FTileInfo tile = LightGrid[tileIndex];

    float3 Accum = float3(0.0f, 0.0f, 0.0f);
    uint hits = 0;

    for (uint i = 0; i < tile.LightCount; ++i)
    {
        uint lightIdx = LightIndexList[tile.StartOffset + i];
        FLightDataFull L = AllLights[lightIdx];  

        float contrib = 0.0f;

        if (L.LightType == 1) // Directional
        {
            contrib = 1.0f;
        }
        else if (L.LightType == 2 || L.LightType == 3) // Point / Spot
        {
            float dist = length(L.Position - worldPos);
            contrib = saturate(1.0f - dist / max(L.AttenuationRadius, 0.0001f));
            contrib = pow(contrib, max(L.LightFalloffExponent, 0.0001f));

            if (L.LightType == 3)
            {
                float3 ld = normalize(L.Position - worldPos);
                float spotEffect = dot(ld, -L.Direction);
                float cosInner = cos(radians(L.InnerConeAngle));
                float cosOuter = cos(radians(L.OuterConeAngle));
                contrib *= smoothstep(cosOuter, cosInner, spotEffect);
            }
        }

        if (contrib > 0.001f)
        {
            Accum += L.LightColor.rgb * contrib;
            hits++;
        }
    }

    // 아무 라이트의 영향도 없으면 어둡게, 있으면 라이트 컬러의 합으로
    float3 Base = (hits == 0) ? float3(0.05f, 0.05f, 0.05f) : Accum;
    return float4(saturate(Base), 1.0f);
}
