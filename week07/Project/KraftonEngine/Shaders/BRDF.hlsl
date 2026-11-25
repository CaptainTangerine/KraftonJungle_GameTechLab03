#include "Common/Functions.hlsl"
#include "Common/Lighting.hlsl"
#include "Common/SystemSamplers.hlsl"

static const float PI = 3.14159265359;

Texture2D RoughnessMap : register(t3);
Texture2D MetallicMap : register(t4);


// 1. Normal Distribution Function (Trowbridge-Reitz GGX)
float DistributionGGX(float3 N, float3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return num / denom;
}

// 2. Geometry Function (Schlick-GGX)
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return num / denom;
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

// 3. Fresnel Equation (Schlick's Approximation)
float3 FresnelSchlick(float cosTheta, float3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Main Cook-Torrance BRDF Calculation
float3 CalculateCookTorrance(float3 N, float3 V, float3 L, float roughness, float F) {
    float3 H = normalize(V + L);
    
    float D = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);

    float3 numerator = D * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001; // Avoid div by zero
    float3 specular = numerator / denominator;

    return specular;
}

// 특정 샘플링된 라이트로부터 Blinn-Phong 결과를 계산
float4 CalculateBRDF(PS_Input_Light input, float3 Normal, float roughness, float metallic, 
                    float3 ViewDir, FLightSample Light, float4 DiffuseColor)
{
    // 1. 필요한 벡터들 계산
    float3 N = normalize(Normal);
    float3 V = ViewDir;
    float3 L = normalize(Light.Direction);
    float3 H = normalize(V + L);

    // 2. 재질 정보 (보통 텍스처에서 샘플링)
    float3 albedo = DiffuseColor;


    // 3. F0 설정 (비금속은 0.04, 금속은 albedo 값 사용)
    float3 F0 = float3(0.04, 0.04, 0.04);
    F0 = lerp(F0, albedo, metallic);

    // 4. Specular (Cook-Torrance) 계산
    float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);;
    float3 specBRDF = CalculateCookTorrance(N, V, L, roughness, F);

    // 5. Diffuse (Lambert) 계산 및 에너지 보존 적용
    float3 ks = F;             // 반사된 비율
    float3 kd = 1.0 - ks;      // 굴절된 비율
    kd *= (1.0 - metallic);    // 금속은 굴절광이 흡수되므로 diffuse가 없음

    float3 diffBRDF = kd * albedo / PI;

    // 6. 최종 합산
    float NdotL = max(dot(N, L), 0.0);
    float3 directLight = (diffBRDF + specBRDF) * Light.Color * NdotL;

    return float4(directLight, 1.0);
}

float4 ComputeBRDF(PS_Input_Light input, float3 Normal, float4 DiffuseColor, float2 pixelPos)
{
    float Roughness = RoughnessMap.Sample(LinearWrapSampler, input.texcoord).r;
    // NOTE: Roughness가 0이면 너무 어두워보이므로 Blinn-Phong과의 시각적 일치를 위해 기본값 설정.
    if (Roughness <= 0.01) Roughness = 0.5;
    // 실제 Roughness가 0인 material이 필요하다면 위 코드 1줄 삭제
    float Metallic = MetallicMap.Sample(LinearWrapSampler, input.texcoord).r;
    
    float3 ViewDir = normalize(CameraWorldPos - input.worldPos.xyz);
    
    float4 Ambient = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float4 TotalDiffuseSpecular = float4(0.0f, 0.0f, 0.0f, 0.0f);
    
    float viewZ = mul(input.worldPos, View).z;
    uint tileLightCount = GetTileLightCount(pixelPos, viewZ);
    for (uint j = 0; j < tileLightCount; ++j)
    {
        FLightDataFull lightFull = GetTiledLight(pixelPos, viewZ, j);
        FLightSample Sample = GetLightSample(lightFull, input.worldPos.xyz);
        
        if (Sample.LightType == 0) // Ambient
        {
            Ambient += Sample.Color;
        }
        else
        {
            TotalDiffuseSpecular += CalculateBRDF(input, Normal, Roughness, Metallic, ViewDir, Sample, DiffuseColor);
        }
    }
    
    return Ambient + TotalDiffuseSpecular;
}
