// Hi-Z Mip Chain Generator (Power of Two Optimized)
// CSCopyDepth: actual depth buffer -> PoT Hi-Z mip 0
// CSDownsample: PoT Hi-Z mip N-1 -> PoT Hi-Z mip N

cbuffer HiZParams : register(b0)
{
    uint2 SrcDimensions;     // Dimensions of the source for this step (PoT)
    uint2 ActualDimensions;  // Original viewport dimensions (for Mip 0 copy)
};

Texture2D<float4> SrcTexture : register(t0);
RWTexture2D<float2> DstTexture : register(u0);

[numthreads(8, 8, 1)]
void CSCopyDepth(uint3 DTid : SV_DispatchThreadID)
{
    // Check PoT dimensions
    if (DTid.x >= SrcDimensions.x || DTid.y >= SrcDimensions.y)
        return;

    // Only copy from actual viewport area
    if (DTid.x < ActualDimensions.x && DTid.y < ActualDimensions.y)
    {
        float d = SrcTexture[DTid.xy].r;
        DstTexture[DTid.xy] = float2(d, d);
    }
    else
    {
        // Out of viewport area (PoT padding): Fill with farthest depth
        // Reversed-Z: farthest is 0.0
        DstTexture[DTid.xy] = float2(0.0, 0.0);
    }
}

[numthreads(8, 8, 1)]
void CSDownsample(uint3 DTid : SV_DispatchThreadID)
{
    // Downsample by factor of 2 (PoT)
    uint2 dstSize = SrcDimensions / 2;

    if (DTid.x >= dstSize.x || DTid.y >= dstSize.y)
        return;

    uint2 srcCoord = DTid.xy * 2;
    
    // Read 2x2 neighborhood
    float2 d00 = SrcTexture[srcCoord].xy;
    float2 d10 = SrcTexture[srcCoord + uint2(1, 0)].xy;
    float2 d01 = SrcTexture[srcCoord + uint2(0, 1)].xy;
    float2 d11 = SrcTexture[srcCoord + uint2(1, 1)].xy;

    // x: Min (Far), y: Max (Near)
    float minDepth = min(min(d00.x, d10.x), min(d01.x, d11.x));
    float maxDepth = max(max(d00.y, d10.y), max(d01.y, d11.y));

    DstTexture[DTid.xy] = float2(minDepth, maxDepth);
}
