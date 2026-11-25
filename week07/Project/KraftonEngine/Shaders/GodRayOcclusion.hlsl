#include "Common/Functions.hlsl"
#include "Common/SystemResources.hlsl"


// b2 (PerShader0)
cbuffer GodRayOcclusion : register(b2)
{
    float2 ScreenSunPos;
    float SunRadius;
    float AspectRatio;
}


PS_Input_UV VS(uint vertexID : SV_VertexID)
{
    return FullscreenTriangleVS(vertexID);
}

float4 PS(PS_Input_UV input) : SV_Target
{
    int2 Coord = int2(input.position.xy);
    
    // Revers-Z 유의 far : 0,  near : 1 
    // GodRay는 GodRayOcclusion 패스 직전에 복사된 DepthPrePass 결과를 SceneDepth(t10)로 읽습니다.
    float Depth = SceneDepth.Load(int3(Coord, 0));
    
    // 태양을 가리는 물체는 검게 
    if (Depth > 0.0001f)
    {
        return float4(0.f, 0.f, 0.f, 1.f);
    };
    
    // 태양 주의로부터 밝게 표시
    float2 SunDelta = ScreenSunPos - input.uv;
    SunDelta.x *= AspectRatio;
    float Delta = length(SunDelta);
    float Bright = 1.f - smoothstep(SunRadius * 0.5, SunRadius, Delta);
    
    Bright = max(0.1, Bright);
    return float4(Bright, Bright, Bright, 1.f);

}
