#include "Common/Functions.hlsl"
#include "Common/VertexLayouts.hlsl"
#include "Common/SystemSamplers.hlsl"
#include "Common/Lighting.hlsl"

PS_Input_Light VS(VS_Input_PNCT input)
{
    PS_Input_Light output;
    output.position = ApplyMVP(input.position);
    output.normal = normalize(mul(input.normal, (float3x3) Model));
    output.worldPos = mul(float4(input.position, 1.0f), Model);
    output.texcoord = input.texcoord;
    output.color = input.color;
    output.barycentric = input.barycentric;

    return output;
}

float4 PS(PS_Input_Light input, float4 pixelPos : SV_Position) : SV_TARGET
{
    // High-Density Heatmap Visualization
    // 0: Black
    // 1-64: Blue -> Green
    // 65-128: Green -> Yellow
    // 129-254: Yellow -> Red
    // 255+: White (Saturated/Limit)
    
    float viewZ = mul(input.worldPos, View).z;
    uint count = GetTileLightCount(pixelPos.xy, viewZ);
    float fCount = (float)count;
    
    float4 finalColor = float4(0, 0, 0, 1);
    
    if (count == 0) 
    {
        finalColor = float4(0, 0, 0, 1);
    }
    else if (count >= 255)
    {
        finalColor = float4(1.0, 1.0, 1.0, 1.0); // White: LIMIT REACHED
    }
    else if (count <= 64) 
    {
        finalColor = lerp(float4(0, 0, 1, 1), float4(0, 1, 0, 1), fCount / 64.0);
    }
    else if (count <= 128) 
    {
        finalColor = lerp(float4(0, 1, 0, 1), float4(1, 1, 0, 1), (fCount - 64.0) / 64.0);
    }
    else 
    {
        finalColor = lerp(float4(1, 1, 0, 1), float4(1, 0, 0, 1), (fCount - 128.0) / 126.0);
    }

    return float4(ApplyWireframe(ApplySolidWireframe(finalColor.rgb, input.barycentric)), 1.0f);
}
