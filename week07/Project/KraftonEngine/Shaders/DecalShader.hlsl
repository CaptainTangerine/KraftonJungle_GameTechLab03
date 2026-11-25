#include "Common/Functions.hlsl"
#include "Common/VertexLayouts.hlsl"
#include "Common/SystemSamplers.hlsl"
#include "Common/Lighting.hlsl"
#include "BlinnPhong.hlsl"

Texture2D g_txColor : register(t0);

// System Textures
Texture2D g_txSceneDepth : register(t10);
Texture2D g_txSceneNormal : register(t12);

// b2 (PerShader0)
cbuffer DecalBuffer : register(b2)
{
    float4x4 DecalWorldToLocal;
    float4 DecalColor;
}

PS_Input_Decal VS(VS_Input_PNCT input)
{
    PS_Input_Decal output;

    float4 worldPos = mul(float4(input.position, 1.0f), Model);
    output.position = mul(mul(worldPos, View), Projection);
    output.worldPos = worldPos.xyz;
    output.normal = normalize(mul(input.normal, (float3x3) Model));
    output.barycentric = input.barycentric;
    output.color = input.color;
    return output;
}

// Alpha Blend state: SrcAlpha / InvSrcAlpha
// finalRGB = src.rgb * src.a + dst.rgb * (1 - src.a)
float4 PS(PS_Input_Decal input, float4 pixelPos : SV_Position) : SV_TARGET
{
    // 픽셀의 화면 좌표를 0~1 UV로 변환
    float2 screenUV = pixelPos.xy / ViewportSize;
    
    // 뎁스 버퍼에서 장면의 Depth 읽기 (t10)
    float sceneDepth = g_txSceneDepth.Sample(PointClampSampler, screenUV).r;
    
    // Depth와 Screen UV를 이용해 실제 장면의 World Position 복원
    // NDC: x[-1,1], y[-1,1], z[0,1]
    float4 clipPos;
    clipPos.x = screenUV.x * 2.0f - 1.0f;
    clipPos.y = (1.0f - screenUV.y) * 2.0f - 1.0f;
    clipPos.z = sceneDepth;
    clipPos.w = 1.0f;
    
    float4 sceneWorldPos = mul(clipPos, InvViewProj);
    sceneWorldPos /= sceneWorldPos.w;
    

    float3 decalLocalPos = mul(float4(sceneWorldPos.xyz, 1.0f), DecalWorldToLocal).xyz;


    if (abs(decalLocalPos.x) > 0.5f || abs(decalLocalPos.y) > 0.5f || abs(decalLocalPos.z) > 0.5f)
    {
        discard;
    }


    float2 uv;
    uv.x = decalLocalPos.y + 0.5f;
    uv.y = 0.5f - decalLocalPos.z;

    float4 texColor = g_txColor.Sample(LinearWrapSampler, uv);
    float3 albedo = texColor.rgb * DecalColor.rgb;
    float alpha = texColor.a * DecalColor.a;

    // 데칼은 Unlit: 라이팅 영향 없이 알베도 그대로 출력
    return float4(ApplyWireframe(ApplySolidWireframe(albedo, input.barycentric)), alpha);
}
