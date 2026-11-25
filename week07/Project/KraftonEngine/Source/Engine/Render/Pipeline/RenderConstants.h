#pragma once
#include "Render/Types/RenderTypes.h"
#include "Render/Resource/Buffer.h"
#include "Render/Device/D3DDevice.h"
#include "Core/EngineTypes.h"
#include "Core/ResourceTypes.h"

#include "Math/Matrix.h"
#include "Math/Vector.h"

class FShader;

/*
	GPU Constant Buffer 구조체, 섹션별 드로우 정보 등
	렌더링에 필요한 데이터 타입을 정의합니다.
*/

// HLSL CB 바인딩 슬롯 — b0/b1 고정, b2/b3 셰이더별 여분
namespace ECBSlot
{
	constexpr uint32 Frame = 0; // b0: View/Projection/Wireframe (고정)
	constexpr uint32 PerObject = 1; // b1: Model/Color (고정)
	constexpr uint32 PerShader0 = 2; // b2: 셰이더별 여분 슬롯 #0
	constexpr uint32 PerShader1 = 3; // b3: 셰이더별 여분 슬롯 #1
}

// HLSL 시스템 텍스처 슬롯 — Renderer가 패스 단위로 바인딩 (프레임 공통)
namespace ESystemTexSlot
{
	constexpr uint32 SceneDepth  = 10; // t10: CopyResource된 Depth (R24_UNORM)
	constexpr uint32 SceneColor  = 11; // t11: CopyResource된 SceneColor (R8G8B8A8_UNORM)
	constexpr uint32 SceneNormal = 12; // t12: Pre-pass에서 렌더링된 Normal (R10G10B10A2_UNORM)
	constexpr uint32 Stencil      = 13; // t13: CopyResource된 Stencil (X24_G8_UINT)
	constexpr uint32 SSAO_Noise   = 14; // t14: SSAO용 4x4 노이즈 텍스처

	// Light Culling (t100+)
	constexpr uint32 AllLights      = 100;
	constexpr uint32 LightIndexList = 101;
	constexpr uint32 LightGrid      = 102;
}

// HLSL 시스템 샘플러 슬롯 — Renderer가 프레임 시작 시 영구 바인딩
namespace ESamplerSlot
{
	constexpr uint32 LinearClamp = 0; // s0: PostProcess, UI, 기본
	constexpr uint32 LinearWrap  = 1; // s1: 메시 텍스처, 데칼
	constexpr uint32 PointClamp  = 2; // s2: 폰트, 깊이/스텐실 정밀 읽기
	// s3-s4: 셰이더별 커스텀 용도
}

// FConstantBufferPool 조회 키 — 바인딩 슬롯과 독립적인 고유 식별자
// [260413 WJ] : Material 개선 이후 다시 생각해본다. 이Key로 공유 상수버퍼를 얻어오는 형태. (@see FScontantBufferPool::GetBuffer)
namespace ECBPoolKey
{
	constexpr uint32 Gizmo = 0;
	constexpr uint32 Fog = 2;
	constexpr uint32 Outline = 3;
	constexpr uint32 SceneDepth = 4;
	constexpr uint32 FXAA = 5;
	constexpr uint32 SSAO = 6;
	constexpr uint32 GodRayOcclusion = 7;
	constexpr uint32 GodRay = 8;
	constexpr uint32 LensFlare = 9;
}

//PerObject
struct FPerObjectConstants
{
	FMatrix Model;
	FMatrix InvModel;
	FVector4 Color;

	// 기본 PerObject: WorldMatrix + White
	static FPerObjectConstants FromWorldMatrix(const FMatrix& WorldMatrix)
	{
		return { WorldMatrix, WorldMatrix.GetInverse(), FVector4(1.0f, 1.0f, 1.0f, 1.0f) };
	}
};

struct FFrameConstants
{
	FMatrix View;
	FMatrix Projection;
	FMatrix InvProjection;
	FMatrix InvViewProj;
	float bUseWireframe;
	FVector WireframeColor;
	float Time;
	FVector CameraWorldPos;
	FVector2 ViewportSize;
	FVector2 ViewportOffset;
	int32 TotalLightCount;
	float ClusterScale;
	float ClusterBias;
	float GNearClip;
	float bUseSolidWireframe;
	float bLightingOnly;
	float _pad[2];
};

// SubUV UV region — atlas frame offset + size (b2 slot, shared with Gizmo)
struct FSubUVRegionConstants
{
	float U = 0.0f;
	float V = 0.0f;
	float Width = 1.0f;
	float Height = 1.0f;
};

struct FGizmoConstants
{
	FVector4 ColorTint;
	uint32 bIsInnerGizmo;
	uint32 bClicking;
	uint32 SelectedAxis;
	float HoveredAxisOpacity;
	uint32 AxisMask;       // 비트 0=X, 1=Y, 2=Z — 1이면 표시, 0이면 숨김. 0x7=전부 표시
	uint32 _pad[3];
};

// PostProcess Outline CB (b3) — HLSL OutlinePostProcessCB와 1:1 대응
struct FOutlinePostProcessConstants
{
	FVector4 OutlineColor = FVector4(1.0f, 0.5f, 0.0f, 1.0f);
	float OutlineThickness = 1.0f;
	float Padding[3] = {};
};

struct FSceneDepthPConstants
{
	float Exponent;
	float NearClip;
	float FarClip;
	uint32 Mode;
};


// Height Fog CB (b6) — HLSL FogBuffer와 1:1 대응
struct FFogConstants
{
	FVector4 InscatteringColor;  // 16B
	float Density;               // 4B
	float HeightFalloff;         // 4B
	float FogBaseHeight;         // 4B
	float StartDistance;         // 4B  — 16B boundary
	float CutoffDistance;        // 4B
	float MaxOpacity;            // 4B
	float _pad[2];              // 8B  — 16B boundary
};

// GodRayOcclusion (b2)
struct FGodRayOcclusionConstants
{
	// 오클루전 패스 파라미터
	FVector2 ScreenSunPos;
	float	 SunRadius;
	float    AspectRatio;
};

// GodRay (b2)
struct FGodRayConstants
{
	// Radial Blur 파라미터
	float    Density;
	float    Decay;
	float    Weight;
	float    Exposure;

	float    Intensity;
	FVector2 ScreenSunPos;
	float	 Pad;

	FVector  LightColor;
	float    Pad2;
};

struct FLensFlareConstants
{
	FVector2 ScreenSunPos;
	float AspectRatio;
	float Intensity;

	FVector2 ScreenCenter;
	float InnerFadeStart;
	float InnerFadeEnd;

	float OuterFadeStart;
	float OuterFadeEnd;
	float SpreadScale;
	float OcclusionStrength;

	uint32 AtlasColumns;
	uint32 AtlasRows;
	uint32 FlareCount;
	float _pad0;
};

struct FFXAAConstants
{
	float EdgeThreshold;
	float EdgeThresholdMin;
	float _pad[2];
};

struct FSSAOConstants
{
	FVector4 Samples[64];
	FMatrix Projection;
	FMatrix InvProjection;
	FMatrix View;
	float Radius = 0.5f;
	float Bias = 0.025f;
	float Intensity = 1.0f;
	float _pad;
};

// ============================================================
// 타입별 CB 바인딩 디스크립터 — GPU CB에 업로드할 데이터를 인라인 보관
// ============================================================
struct FConstantBufferBinding
{
	FConstantBuffer* Buffer = nullptr;	// 업데이트할 CB (nullptr이면 미사용)
	uint32 Size = 0;					// 업로드할 바이트 수
	uint32 Slot = 0;					// VS/PS CB 슬롯

	static constexpr size_t kMaxDataSize = 2048; // SSAOConstants(1168B)를 수용하기 위해 확장
	alignas(16) uint8 Data[kMaxDataSize] = {};

	// Buffer/Size/Slot
	template<typename T>
	T& Bind(FConstantBuffer* InBuffer, uint32 InSlot)
	{
		static_assert(sizeof(T) <= kMaxDataSize, "CB data exceeds inline buffer size");
		Buffer = InBuffer;
		Size = sizeof(T);
		Slot = InSlot;
		return *reinterpret_cast<T*>(Data);
	}

	void CopyFrom(FConstantBuffer* InBuffer, uint32 InSlot, const void* InData, uint32 InSize)
	{
		Buffer = InBuffer;
		Slot = InSlot;
		Size = InSize;
		if (InData && InSize > 0)
		{
			memcpy(Data, InData, (InSize < kMaxDataSize) ? InSize : kMaxDataSize);
		}
	}

	template<typename T>
	T& As()
	{
		static_assert(sizeof(T) <= kMaxDataSize, "CB data exceeds inline buffer size");
		return *reinterpret_cast<T*>(Data);
	}

	template<typename T>
	const T& As() const
	{
		static_assert(sizeof(T) <= kMaxDataSize, "CB data exceeds inline buffer size");
		return *reinterpret_cast<const T*>(Data);
	}
};

// 텍스처 바인딩 정보 (Material 리플렉션용)
struct FTextureBinding
{
	ID3D11ShaderResourceView* SRV = nullptr;
	uint32 Slot = 0;
};

// 섹션별 드로우 정보 — 머티리얼(텍스처)이 다른 구간을 분리 드로우
struct FMeshSectionDraw
{
	ID3D11ShaderResourceView* DiffuseSRV = nullptr; // t0 고정 (기존 코드 호환)
	ID3D11ShaderResourceView* NormalSRV = nullptr;  // t1 고정 (기존 코드 호환)

	FShader* Shader = nullptr;
	uint32 FirstIndex = 0;
	uint32 IndexCount = 0;

	// 머티리얼 기반 렌더 상태
	EBlendState Blend = EBlendState::Opaque;
	EDepthStencilState DepthStencil = EDepthStencilState::Default;
	ERasterizerState Rasterizer = ERasterizerState::SolidBackCull;

	//PerShader
	FConstantBufferBinding MaterialCB[2] = {};//	[0]=b2, [1]=b3,

	// 추가 텍스처 바인딩 (t2 이상)
	static constexpr uint32 kMaxExtraTextures = 8;
	FTextureBinding ExtraTextures[kMaxExtraTextures] = {};
	uint32 ExtraTextureCount = 0;

	//UberShader 적용 여부
	bool bSupportUberShader = { false };
};
