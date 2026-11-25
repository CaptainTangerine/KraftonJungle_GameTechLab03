#include "Common/VertexLayouts.hlsl"
#include "Common/SystemSamplers.hlsl"
#include "Common/Functions.hlsl"
#include "Common/Lighting.hlsl"
// LIGHTING_MODEL_GOURAUD -> 고로 셰이딩(PerVertex) + 블린퐁 조명 모델 사용
// LIGHTING_MODEL_LAMBERT -> 퐁 셰이딩(PerPixel)    + 렘버트 조명 모델 사용 (스페큘러 제거)
// LIGHTING_MODEL_PHONG   -> 퐁 셰이딩(PerPixel)    + 블린퐁 조명 모델 사용


#if DEPTH_NORMAL_PASS
      // lighting 코드 include 생략
#elif LIGHTING_MODEL_GOURAUD
      #include "Gouraud.hlsl"
#elif LIGHTING_MODEL_LAMBERT
      #include "Lambert.hlsl"
#elif LIGHTING_MODEL_PHONG
      #include "BlinnPhong.hlsl"
#elif LIGHTING_MODEL_BRDF
      #include "BRDF.hlsl"
#elif LIGHTING_MODEL_TOON
      #include "Toon.hlsl"
#else
      #include "BlinnPhong.hlsl"    // return value not completely initialized 에러 해결을 위해 else 필요
#endif

Texture2D g_txColor  : register(t0);
Texture2D g_txNormal : register(t1);


cbuffer PerShader1 : register(b2)
{
    float4 SectionColor;
};

PS_Input_Light VS(VS_Input_PNCT input)
{
    PS_Input_Light output;
    output.position = ApplyMVP(input.position);
    
    // InvModel에서는 셰이더에서 읽을 때 열벡터로 읽으니 알아서 TransPose 되있습니다.
    // ColumMajor로 해석했기 때문에 곱하는 순서도 반대
    output.normal = normalize(mul((float3x3) InvModel , input.normal));
    output.tangent = normalize(mul(input.tangent, (float3x3) Model));
    output.worldPos = mul(float4(input.position, 1.0f), Model);
    output.texcoord = input.texcoord;
    output.barycentric = input.barycentric;
    
#if LIGHTING_MODEL_GOURAUD
    float4 GouraudDiffuse = (bLightingOnly > 0.5f) ? float4(0.5f, 0.5f, 0.5f, 1.0f) : float4(1, 1, 1, 1);
    output.color = ComputeGouraud(output.worldPos, output.normal, GouraudDiffuse);
#else
    output.color = input.color;
#endif
    
    return output;
}

float4 PS(PS_Input_Light input, float4 pixelPos : SV_Position) : SV_TARGET
{
    float4 DiffuseColor = g_txColor.Sample(LinearWrapSampler, input.texcoord);

    if (DiffuseColor.a < 0.001f)
        DiffuseColor = float4(1.0f, 0.0f, 1.0f, 1.0f);

    // LightingOnly: 알베도를 중성 회색으로 고정하여 라이팅 기여만 보이게.
    if (bLightingOnly > 0.5f)
        DiffuseColor = float4(0.5f, 0.5f, 0.5f, 1.0f);
    
    float4 finalColor;
    
    
#if LIGHTING_MODEL_GOURAUD // <- 이거 없애면 기본 적용되는 ComputeBlinnPhong 함수를 찾을 수 없습니다.
    finalColor = input.color * DiffuseColor;
#else
    float3 normalizeN = normalize(input.normal); // 보간된 이후 normal의 길이가 1이 아닐 수 있으니 다시 normalize 해줌.
    
    #if HAS_NORMAL_MAP
        float3 NormalMapSample = g_txNormal.Sample(LinearWrapSampler, input.texcoord).rgb;
        float3 tangent = normalize(input.tangent);
       normalizeN = ComputeNormalByNormalMap(tangent, normalizeN, NormalMapSample); //normal mapping을 적용.
    #endif
    
    #if DEPTH_NORMAL_PASS
        //-1~1 범위의 normal을 0~1 범위로 remapping 해서 출력 (Normal Buffer 용)
        finalColor = float4(normalizeN * 0.5f + 0.5f, 1.0f);
    #elif LIGHTING_MODEL_LAMBERT
        finalColor = ComputeLambert(input.worldPos, normalizeN, DiffuseColor, pixelPos.xy);
    #elif LIGHTING_MODEL_PHONG
        finalColor = ComputeBlinnPhong(input.worldPos, normalizeN, DiffuseColor, pixelPos.xy);
    #elif LIGHTING_MODEL_BRDF
        finalColor = ComputeBRDF(input, normalizeN, DiffuseColor, pixelPos.xy);
    #elif LIGHTING_MODEL_TOON
        finalColor = ComputeToon(input, normalizeN, DiffuseColor, pixelPos.xy);
    #else   // return value not completely initialized 에러 해결을 위해 else 필요
        finalColor = ComputeBlinnPhong(input.worldPos, normalizeN, DiffuseColor, pixelPos.xy);
    #endif
    
#endif

    return float4(ApplyWireframe(ApplySolidWireframe(finalColor.rgb, input.barycentric)), finalColor.a);
}
