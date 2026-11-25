// HeightFog.hlsl
// Fullscreen Triangle VS (SV_VertexID) + Exponential Height Fog PS

#include "Common/Functions.hlsl"
#include "Common/SystemResources.hlsl"

// b2 (PerShader0): Fog parameters
cbuffer FogBuffer : register(b2)
{
    float4 FogInscatteringColor;
    float FogDensity;
    float FogHeightFalloff;
    float FogBaseHeight;
    float FogStartDistance;
    float FogCutoffDistance;
    float FogMaxOpacity;
    float2 _fogPad;
};

// SceneDepth (t10) is declared in Common/ConstantBuffers.hlsl

// ── VS: Fullscreen Triangle ──
PS_Input_UV VS(uint vertexID : SV_VertexID)
{
    return FullscreenTriangleVS(vertexID);
}

float4 PS(PS_Input_UV input) : SV_TARGET
{
    int2 coord = int2(input.position.xy);

    // Reversed-Z: 1=near, 0=far (depth==0 → sky)
    float depth = SceneDepth.Load(int3(coord, 0));
    bool bIsSky = (depth <= 0.0);

    // View ray direction (월드). 스카이와 지오메트리 공통.
    float2 ndc = float2(input.uv.x * 2.0 - 1.0, 1.0 - input.uv.y * 2.0);
    float3 rayDir;
    float rayLength;

    if (bIsSky)
    {
        // 스카이: clip-space far plane(reversed-Z에서 z=0) 방향 복원
        float4 farH  = mul(float4(ndc, 0.0, 1.0), InvViewProj);
        float3 farP  = farH.xyz / farH.w;
        rayDir = normalize(farP - CameraWorldPos);
        rayLength = (FogCutoffDistance > 0.0) ? FogCutoffDistance : 1.0e6;
    }
    else
    {
        float4 clipPos = float4(ndc, depth, 1.0);
        float4 worldH  = mul(clipPos, InvViewProj);
        float3 worldPos = worldH.xyz / worldH.w;
        float3 v = worldPos - CameraWorldPos;
        rayLength = length(v);
        rayDir = v / max(rayLength, 1e-4);
    }

    float3 camPos = CameraWorldPos;
    float rayDirZ = rayDir.z;
    float falloff = max(FogHeightFalloff, 1e-4);

    // StartDistance ~ Cutoff 구간만 적분
    float segStart = FogStartDistance;
    float segEnd   = rayLength;
    if (FogCutoffDistance > 0.0)
        segEnd = min(segEnd, FogCutoffDistance);
    float effectiveLength = max(segEnd - segStart, 0.0);
    if (effectiveLength <= 0.0)
        return float4(0, 0, 0, 0);

 
    float startHeight = camPos.z + rayDirZ * segStart - FogBaseHeight;
    float endHeight   = startHeight + rayDirZ * effectiveLength;


    float dzRay = rayDirZ * effectiveLength;
    float lineIntegral;
    if (abs(dzRay * falloff) > 1e-3)
    {

        float eA = exp(-falloff * startHeight);
        float eB = exp(-falloff * endHeight);
        lineIntegral = FogDensity * (eA - eB) / (falloff * rayDirZ);
    }
    else
    {
        // 수평 근처: 밀도 거의 일정
        lineIntegral = FogDensity * exp(-falloff * startHeight) * effectiveLength;
    }
    lineIntegral = max(lineIntegral, 0.0);

    // Beer–Lambert: transmittance = exp(-τ), opacity = 1 - transmittance
    float fogFactor = 1.0 - exp(-lineIntegral);


    fogFactor = clamp(fogFactor, 0.0, FogMaxOpacity);

    return float4(FogInscatteringColor.rgb, fogFactor);
}
