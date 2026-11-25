#ifndef CONSTANT_BUFFERS_HLSL
#define CONSTANT_BUFFERS_HLSL

#pragma pack_matrix(row_major)

// b0: 프레임 공통 — ViewProj, 와이어프레임 설정
cbuffer FrameBuffer : register(b0)
{
    float4x4 View;
    float4x4 Projection;
    float4x4 InvProjection;
    float4x4 InvViewProj;
    float bUseWireframe;
    float3 WireframeRGB;
    float Time;
    float3 CameraWorldPos;
    float2 ViewportSize;
    float2 ViewportOffset;
    // 이하 Cluster-based light culling에 사용
    int TotalLightCount;
    float ClusterScale;
    float ClusterBias;
    float GNearClip;
    float bUseSolidWireframe;
    float bLightingOnly;
    float2 _padFrame;       // padding
}

// b1: 오브젝트별 — 월드 변환, 색상
cbuffer PerObjectBuffer : register(b1)
{
    float4x4 Model;
    float4x4 InvModel; // 노말 변환용
    float4 PrimitiveColor;
};

struct FLightDataFull
{
    int LightType; // 0: Ambient, 1: Directional, 2: Point, 3: Spot
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

// Light Culling Grid Info (Mirror of FTileInfo in C++)
struct FTileInfo
{
    uint StartOffset;
    uint LightCount;
};

// t100: 모든 광원 데이터
StructuredBuffer<FLightDataFull> AllLights : register(t100);
// t101: 타일별 광원 인덱스 리스트
StructuredBuffer<uint> LightIndexList : register(t101);
// t102: 타일 그리드 정보
StructuredBuffer<FTileInfo> LightGrid : register(t102);

#endif // CONSTANT_BUFFERS_HLSL
