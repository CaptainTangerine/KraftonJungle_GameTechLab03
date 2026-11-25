#include "Common/Functions.hlsl"
#include "Common/SystemSamplers.hlsl"
#include "Common/SystemResources.hlsl"

Texture2D LensFlareAtlas : register(t0);

cbuffer LensFlare : register(b2)
{
    float2 ScreenSunPos;    //태양의 Screen Space 좌표 (0-1)
    float AspectRatio;
    float Intensity;

    float2 ScreenCenter;    // 화면 중심의 Screen Space 좌표 (보통 0.5,0.5)
    float InnerFadeStart;   // 태양으로부터 얼마나 떨어졌을 때 flare가 나타나기 시작하는지
    float InnerFadeEnd;     // 태양으로부터 얼마나 떨어졌을 때 flare가 완전히 나타나는지

    float OuterFadeStart;   // 태양으로부터 얼마나 떨어졌을 때 flare가 사라지기 시작하는지
    float OuterFadeEnd;     // 태양으로부터 얼마나 떨어졌을 때 flare가 완전히 사라지는지
    float SpreadScale;      // 전체적으로 얼마나 퍼질지
    float OcclusionStrength;// 태양이 부분적으로 가려질 때 flare는 얼마나 투명해질지

    uint AtlasColumns;
    uint AtlasRows;
    uint FlareCount;
    float LensFlarePad;
}

static const uint kMaxFlares = 9;

// float4(xOffset, size, intensity, atlasFrameIndex)
// - xOffset: 태양↔화면중심 축 기준 상대 위치 계수
// - size: 플레어 스프라이트 반경(스크린 UV 스케일)
// - intensity: 해당 플레어의 기본 세기
// - atlasFrameIndex: 아틀라스에서 사용할 프레임 인덱스
static const float4 kFlareParams[kMaxFlares] =
{
    float4(0.00, 0.085, 0.20, 4.0),
    float4(0.18, 0.040, 0.38, 0.0),
    float4(-0.10, 0.050, 0.30, 8.0),
    float4(0.36, 0.060, 0.26, 2.0),
    float4(-0.28, 0.070, 0.22, 6.0),
    float4(0.58, 0.095, 0.16, 1.0),
    float4(-0.42, 0.048, 0.24, 3.0),
    float4(0.74, 0.055, 0.14, 5.0),
    float4(-0.62, 0.080, 0.18, 7.0)
};

static const float3 kFlareTints[kMaxFlares] =
{
    float3(1.00, 0.95, 0.85),
    float3(0.90, 0.75, 1.00),
    float3(0.70, 0.95, 1.00),
    float3(1.00, 0.72, 0.55),
    float3(0.55, 0.82, 1.00),
    float3(1.00, 0.88, 0.60),
    float3(0.85, 1.00, 0.78),
    float3(1.00, 0.68, 0.82),
    float3(0.78, 0.88, 1.00)
};

PS_Input_UV VS(uint vertexID : SV_VertexID)
{
    return FullscreenTriangleVS(vertexID);
}

// 태양이 가려지는지 샘플링해 반환
float SampleVisibility(float2 uv)
{
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
    {
        return 0.0f;
    }

    float2 texel = 1.0f / max(ViewportSize, float2(1.0f, 1.0f));
    float2 offsets[5] =
    {
        float2(0.0, 0.0),
        float2(texel.x * 2.0, 0.0),
        float2(-texel.x * 2.0, 0.0),
        float2(0.0, texel.y * 2.0),
        float2(0.0, -texel.y * 2.0)
    };

    float visible = 0.0f;
    [unroll]
    for (int i = 0; i < 5; ++i)
    {
        float2 sampleUV = saturate(uv + offsets[i]);
        float depth = SceneDepth.SampleLevel(PointClampSampler, sampleUV, 0).r;
        visible += depth <= 0.0001f ? 1.0f : 0.0f;
    }

    visible /= 5.0f;
    return lerp(1.0f, visible, saturate(OcclusionStrength));
}

float4 SampleAtlas(uint frameIndex, float2 atlasUV)
{
    uint safeCols = max(AtlasColumns, 1u);
    uint safeRows = max(AtlasRows, 1u);
    uint cell = frameIndex % (safeCols * safeRows);
    uint col = cell % safeCols;
    uint row = cell / safeCols;

    float2 frameSize = float2(1.0f / safeCols, 1.0f / safeRows);
    float2 uv = float2(col, row) * frameSize + atlasUV * frameSize;
    return LensFlareAtlas.Sample(LinearClampSampler, uv);
}

float4 PS(PS_Input_UV input) : SV_Target
{
    float2 toSun = ScreenSunPos - ScreenCenter; //화면 중심에서 태양까지의 벡터
    toSun.x *= AspectRatio;
    float sunDistance = length(toSun);

    float appear = smoothstep(InnerFadeStart, max(InnerFadeEnd, InnerFadeStart + 0.0001f), sunDistance);
    float disappear = 1.0f - smoothstep(OuterFadeStart, max(OuterFadeEnd, OuterFadeStart + 0.0001f), sunDistance);
    float fade = saturate(appear * disappear);
    if (fade <= 0.0001f)
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    float visibility = SampleVisibility(ScreenSunPos);
    float globalAlpha = Intensity * fade * visibility;
    if (globalAlpha <= 0.0001f)
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    // flare 배치
    float2 axisAspect = ScreenCenter - ScreenSunPos;
    axisAspect.x *= AspectRatio;
    float axisLength = length(axisAspect);
    float2 axisUV = float2(0.0, 0.0);
    if (axisLength > 0.0001f)
    {
        float2 axisAspectNorm = axisAspect / axisLength;
        axisUV = float2(axisAspectNorm.x / AspectRatio, axisAspectNorm.y);
    }

    //화면에 멀어질 수록 약해짐
    float spreadProgress = smoothstep(
        InnerFadeStart,
        max(OuterFadeStart, InnerFadeEnd + 0.0001f),
        sunDistance);
    spreadProgress *= spreadProgress;
    float spread = SpreadScale * spreadProgress;
    float3 flareColor = float3(0.0, 0.0, 0.0);
    uint activeCount = min(FlareCount, kMaxFlares);

    [unroll]
    for (uint i = 0; i < kMaxFlares; ++i)
    {
        if (i >= activeCount)
        {
            break;
        }

        float4 flare = kFlareParams[i];
        float2 flareCenter = ScreenSunPos + axisUV * (flare.x * spread);
        float size = flare.y * lerp(0.70f, 1.0f, appear);

        float2 local = input.uv - flareCenter;
        local.x *= AspectRatio;
        float2 spriteUV = local / size * 0.5f + 0.5f;

        if (spriteUV.x < 0.0f || spriteUV.x > 1.0f || spriteUV.y < 0.0f || spriteUV.y > 1.0f)
        {
            continue;
        }

        float4 atlas = SampleAtlas((uint)flare.w, spriteUV);
        flareColor += atlas.rgb * kFlareTints[i] * (atlas.a * flare.z);
    }

    return float4(flareColor * globalAlpha, 1.0f);
}
