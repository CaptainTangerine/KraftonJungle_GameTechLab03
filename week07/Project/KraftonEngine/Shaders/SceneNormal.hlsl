#include "Common/Functions.hlsl"
#include "Common/VertexLayouts.hlsl"
#include "Common/SystemResources.hlsl"

// SceneNormal (t12) is declared in Common/SystemResources.hlsl

PS_Input_UV VS(uint vertexID : SV_VertexID)
{
    return FullscreenTriangleVS(vertexID);
}

float4 PS(PS_Input_UV input) : SV_TARGET
{
    int3 coord = int3(input.position.xy, 0);
    float4 normalRemapped = SceneNormal.Load(coord);

    // 셰이더에서 alpha는 1로 설정되므로, 0이면 아무것도 없는 공간
    if (normalRemapped.a < 0.1)
    {
        return float4(0, 0, 0, 1);
    }

    return float4(normalRemapped.rgb, 1.0);
}