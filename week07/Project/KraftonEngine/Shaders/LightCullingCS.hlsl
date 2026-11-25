#pragma pack_matrix(row_major)

#define TILE_SIZE 16
#define MAX_LIGHTS_PER_CLUSTER 128

struct FLightDataFull
{
    int LightType;
    float Intensity;
    float2 _pad0;

    float4 LightColor;

    float3 Position;
    float AttenuationRadius;

    float3 Direction;
    float LightFalloffExponent;

    float InnerConeAngle;
    float OuterConeAngle;
    float2 _pad1;
};

struct FClusterInfo
{
    uint StartOffset;
    uint LightCount;
};

cbuffer Params : register(b0)
{
    float4x4 View;
    float4x4 Proj;
    float4x4 InvProj;
    float2 ViewportSize;
    uint LightCount;
    float ClusterScale;
    float ClusterBias;
    float NearClip;
    float2 _padParam;
};

StructuredBuffer<FLightDataFull> AllLights : register(t1);

RWStructuredBuffer<uint> LightIndexList : register(u0);
RWStructuredBuffer<FClusterInfo> LightGrid : register(u1);
RWStructuredBuffer<uint> IndexCounter : register(u2);

float3 ScreenToView(float4 screen)
{
    float4 view = mul(screen, InvProj);
    return view.xyz / view.w;
}

[numthreads(4, 4, 4)]
void CSMain(uint3 DTid : SV_DispatchThreadID)
{
    uint GridX = (uint)ViewportSize.x / TILE_SIZE + ((uint)ViewportSize.x % TILE_SIZE != 0 ? 1 : 0);
    uint GridY = (uint)ViewportSize.y / TILE_SIZE + ((uint)ViewportSize.y % TILE_SIZE != 0 ? 1 : 0);
    uint GridZ = 32;

    if (DTid.x >= GridX || DTid.y >= GridY || DTid.z >= GridZ) return;

    // 1. Compute Cluster AABB in View Space
    // Z Bounds (Logarithmic)
    float minZ = pow(2.0, (float(DTid.z) - ClusterBias) / ClusterScale);
    float maxZ = pow(2.0, (float(DTid.z + 1) - ClusterBias) / ClusterScale);
    
    // Ensure we are within camera range
    minZ = max(minZ, NearClip);
    maxZ = max(maxZ, NearClip);

    // X, Y Bounds in NDC [-1, 1]
    float2 tileSize = 2.0 * float2(TILE_SIZE, TILE_SIZE) / ViewportSize;
    float2 minXY = float2(DTid.xy) * tileSize - 1.0;
    float2 maxXY = float2(DTid.xy + 1) * tileSize - 1.0;
    
    // Invert Y for NDC space (Screen space Y is down, NDC Y is up)
    minXY.y *= -1.0;
    maxXY.y *= -1.0;

    // View Space AABB
    // Derived pZ robustly from ViewZ
    float4 clipMinZ = mul(float4(0, 0, minZ, 1), Proj);
    float4 clipMaxZ = mul(float4(0, 0, maxZ, 1), Proj);
    float pMinZ = clipMinZ.z / clipMinZ.w;
    float pMaxZ = clipMaxZ.z / clipMaxZ.w;
    
    float3 corners[8];
    corners[0] = ScreenToView(float4(minXY.x, minXY.y, pMinZ, 1.0));
    corners[1] = ScreenToView(float4(maxXY.x, minXY.y, pMinZ, 1.0));
    corners[2] = ScreenToView(float4(minXY.x, maxXY.y, pMinZ, 1.0));
    corners[3] = ScreenToView(float4(maxXY.x, maxXY.y, pMinZ, 1.0));
    corners[4] = ScreenToView(float4(minXY.x, minXY.y, pMaxZ, 1.0));
    corners[5] = ScreenToView(float4(maxXY.x, minXY.y, pMaxZ, 1.0));
    corners[6] = ScreenToView(float4(minXY.x, maxXY.y, pMaxZ, 1.0));
    corners[7] = ScreenToView(float4(maxXY.x, maxXY.y, pMaxZ, 1.0));

    float3 aabbMin = float3(1e10, 1e10, 1e10);
    float3 aabbMax = float3(-1e10, -1e10, -1e10);
    [unroll]
    for(int c=0; c<8; ++c) {
        aabbMin = min(aabbMin, corners[c]);
        aabbMax = max(aabbMax, corners[c]);
    }

    // 2. Culling
    uint localLightIndices[MAX_LIGHTS_PER_CLUSTER];
    uint localLightCount = 0;

    for (uint i = 0; i < LightCount; ++i)
    {
        FLightDataFull light = AllLights[i];
        if (light.LightType == 0        // Ambient
            || light.LightType == 1)    // Directional
        {
            if (localLightCount < MAX_LIGHTS_PER_CLUSTER)
            {
                localLightIndices[localLightCount++] = i;
            }
        }
        else                             // Point or Spot
        {
            float3 lightViewPos = mul(float4(light.Position, 1.0), View).xyz;
            float radius = light.AttenuationRadius;

            // Sphere-AABB Test
            float3 closest = max(aabbMin, min(lightViewPos, aabbMax));
            float3 distVec = closest - lightViewPos;
            if (dot(distVec, distVec) <= radius * radius)
            {
                if (localLightCount < MAX_LIGHTS_PER_CLUSTER)
                {
                    localLightIndices[localLightCount++] = i;
                }
            }
        }
    }

    // 3. Store
    uint startOffset = 0;
    uint finalLightCount = 0;
    if (localLightCount > 0)
    {
        InterlockedAdd(IndexCounter[0], localLightCount, startOffset);
        
        // 32M entries capacity
        const uint maxEntries = 33554432;
        if (startOffset < maxEntries)
        {
            finalLightCount = min(localLightCount, maxEntries - startOffset);
            for (uint j = 0; j < finalLightCount; ++j)
            {
                LightIndexList[startOffset + j] = localLightIndices[j];
            }
        }
    }

    uint clusterIndex = DTid.z * (GridX * GridY) + DTid.y * GridX + DTid.x;
    FClusterInfo info;
    info.StartOffset = startOffset;
    info.LightCount = finalLightCount;
    LightGrid[clusterIndex] = info;
}
