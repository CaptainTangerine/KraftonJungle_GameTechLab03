#include "Common/Functions.hlsl"
#include "Common/VertexLayouts.hlsl"
#include "Common/SystemSamplers.hlsl"

// 컬러 PNG/TGA 텍스처를 단일 quad에 그리는 빌보드 전용 셰이더.
// SubUV 와 다르게 R 채널이 아닌 알파 채널만으로 컷오프 판정한다.
Texture2D BillboardTex : register(t0);

PS_Input_Tex VS(VS_Input_PNCT input)
{
    PS_Input_Tex output;
    output.position = ApplyMVP(input.position);
    output.texcoord = input.texcoord;
    return output;
}

float4 PS(PS_Input_Tex input) : SV_TARGET
{
    float4 col = BillboardTex.Sample(LinearClampSampler, input.texcoord);
    col *= PrimitiveColor;
    if (col.a < 0.5f)
        discard;

    return float4(col.rgb, col.a);
    //와이어프레임에서 제외
    //float4(ApplySolidWireframe(col.rgb), bIsWireframe ? 1.0f : col.a);
}
