#pragma once

#include "Render/Types/RenderTypes.h"
#include "Render/Resource/Buffer.h"
#include "Math/Vector.h"
#include "Math/Matrix.h"

namespace
{
	const uint32 TILE_SIZE = 16;
	// NOTE: LightCullingCS.hlsl의 MAX_LIGHTS_PER_CLUSTER와 
	// 값이 다르면 라이팅 계단 현상 발생할 수 있음.
	const uint32 GLOBAL_MAX_LIGHTS = 128;	
}

struct FLightCullingParams
{
	FMatrix View;
	FMatrix Proj;
	FMatrix InvProj;
	FVector2 ViewportSize;
	uint32 LightCount;
	// 2.5D light culling에서 인덱스 계산용
	float scale;	
	float bias;
	float NearClip;
	float _pad1;
	float _pad2;
};

struct FTileInfo
{
	uint32 StartOffset;
	uint32 LightCount;
};

// Mirror of FLightData in ConstantBuffers.hlsl
struct FLightDataGPU
{
	int32 LightType;
	float Intensity;
	float _pad0[2];

	FVector4 LightColor;

	FVector Position;
	float AttenuationRadius;

	FVector Direction;
	float LightFalloffExponent;

	float InnerConeAngle;
	float OuterConeAngle;
	float _pad1[2];
};

class FLightCulling
{
public:
	FLightCulling() = default;
	~FLightCulling() { Release(); }

	void Initialize(ID3D11Device* InDevice);
	void Release();

	void Execute(
		ID3D11DeviceContext* Ctx,
		ID3D11ShaderResourceView* DepthSRV,
		const FMatrix& View,
		const FMatrix& Proj,
		uint32 ViewportWidth,
		uint32 ViewportHeight,
		const float NearClip,
		const float FarClip,
		const TArray<FLightDataGPU>& Lights);

	ID3D11ShaderResourceView* GetAllLightsSRV() const { return AllLights.GetSRV(); }
	ID3D11ShaderResourceView* GetLightIndexListSRV() const { return LightIndexList.GetSRV(); }
	ID3D11ShaderResourceView* GetLightGridSRV() const { return LightGrid.GetSRV(); }

	uint32 GetGridX() const { return GridX; }
	uint32 GetGridY() const { return GridY; }
	uint32 GetGridZ() const { return GridZ; }

private:
	void UpdateBuffers(ID3D11Device* Device, uint32 Width, uint32 Height, uint32 LightCount);

private:
	ID3D11Device* Device = nullptr;
	ID3D11ComputeShader* LightCullingCS = nullptr;
	ID3D11Buffer* ParamsCB = nullptr;

	FStructuredBuffer AllLights;      // t100
	FStructuredBuffer LightIndexList; // t101
	FStructuredBuffer LightGrid;      // t102
	FStructuredBuffer IndexCounter;   // Atomic counter for LightIndexList

	uint32 GridX = 0;
	uint32 GridY = 0;
	uint32 GridZ = 32;
	uint32 CurrentLightCapacity = 0;

	bool bInitialized = false;
};
