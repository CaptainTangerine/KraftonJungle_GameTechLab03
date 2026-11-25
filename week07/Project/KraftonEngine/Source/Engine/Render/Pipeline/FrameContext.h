#pragma once

#include "Core/CoreTypes.h"
#include "Math/Matrix.h"
#include "Math/Vector.h"
#include "Render/Types/ViewTypes.h"
#include "Render/Pipeline/LODContext.h"
#include "Render/Culling/ConvexVolume.h"

#include <d3d11.h>

class UCameraComponent;
class FViewport;
class FGPUOcclusionCulling;
class FSSAOProcessor;

/*
	FFrameContext - per-frame/per-viewport read-only state.
	Camera, viewport, render settings, occlusion, LOD context.
	Populated once per frame by the render pipeline, then read by
	Renderer, Proxies, and RenderCollector.
*/
struct FFrameContext
{
	// Camera
	FMatrix View;
	FMatrix Proj;
	FVector CameraPosition;
	FVector CameraForward;
	FVector CameraRight;
	FVector CameraUp;
	float NearClip = 0.1f;
	float FarClip = 1000.0f;

	bool  bIsOrtho   = false;
	float OrthoWidth = 10.0f;

	// Viewport
	float ViewportWidth  = 0.0f;
	float ViewportHeight = 0.0f;
	float ViewportOffsetX = 0.0f;
	float ViewportOffsetY = 0.0f;

	ID3D11RenderTargetView*   ViewportRTV          = nullptr;
	ID3D11DepthStencilView*   ViewportDSV          = nullptr;

	ID3D11RenderTargetView*   NormalRTV            = nullptr;
	ID3D11ShaderResourceView* NormalSRV            = nullptr;

	// SSAO (R8_UNORM)
	ID3D11RenderTargetView*   SSAORTV              = nullptr;
	ID3D11ShaderResourceView* SSAOSRV              = nullptr;
	ID3D11UnorderedAccessView* SSAOUAV             = nullptr;
	ID3D11RenderTargetView*   SSAOBlurredRTV       = nullptr;
	ID3D11ShaderResourceView* SSAOBlurredSRV       = nullptr;
	ID3D11UnorderedAccessView* SSAOBlurredUAV      = nullptr;

	// SceneColor 복사 — FXAA 등 PostProcess에서 최종 화면 읽기용
	ID3D11ShaderResourceView* SceneColorCopySRV     = nullptr;
	ID3D11Texture2D* SceneColorCopyTexture          = nullptr;
	ID3D11Texture2D* ViewportRenderTexture          = nullptr;

	// GodRayOcclusion 처리
	ID3D11RenderTargetView* GodRayOcclusionRTV = nullptr;
	ID3D11ShaderResourceView* GodRayOcclusionSRV = nullptr;


	// CopyResource 소스/대상
	ID3D11Texture2D*          DepthTexture         = nullptr;  // 원본 (CopyResource 소스)
	ID3D11Texture2D*          DepthCopyTexture     = nullptr;  // 복사본 (CopyResource 대상)
	ID3D11ShaderResourceView* DepthCopySRV         = nullptr;
	ID3D11ShaderResourceView* StencilCopySRV       = nullptr;

	ELevelViewportType ViewportType = ELevelViewportType::Perspective;

	// Render Settings
	FViewportRenderOptions RenderOptions;
	EViewMode ViewMode = EViewMode::Lit_BlinnPhong;
	FShowFlags ShowFlags;

	FVector    WireframeColor = FVector(0.7f, 0.7f, 0.7f);

	// GPU Occlusion Culling
	FGPUOcclusionCulling* OcclusionCulling = nullptr;

	// SSAO Processor
	FSSAOProcessor* SSAOProcessor = nullptr;

	// Frustum (per-viewport, computed from View * Proj)
	FConvexVolume FrustumVolume;

	// LOD
	FLODUpdateContext LODContext;

	// Optimization flags
	bool bDepthPrePassDone = false;

	// Derived helpers
	bool IsFixedOrtho() const
	{
		return bIsOrtho
			&& ViewportType != ELevelViewportType::Perspective
			&& ViewportType != ELevelViewportType::FreeOrthographic;
	}

	// Batch setters - populate multiple fields at once
	void SetCameraInfo(const UCameraComponent* Camera);
	void SetViewportInfo(const FViewport* VP);

	void SetViewportSize(float InWidth, float InHeight)
	{
		ViewportWidth  = InWidth;
		ViewportHeight = InHeight;
	}

	void SetRenderOptions(const FViewportRenderOptions& InOptions)
	{
		RenderOptions = InOptions;
	}
	FViewportRenderOptions GetRenderOptions() const { return RenderOptions; }

	void SetRenderSettings(EViewMode InViewMode, const FShowFlags& InShowFlags)
	{
		RenderOptions.ViewMode  = InViewMode;
		RenderOptions.ShowFlags = InShowFlags;

		ViewMode = InViewMode;
		ShowFlags = InShowFlags;
	}

	// Reset D3D pointers
	void ClearViewportResources()
	{
		ViewportRTV             = nullptr;
		ViewportDSV             = nullptr;
		NormalRTV               = nullptr;
		NormalSRV               = nullptr;
		SSAORTV                 = nullptr;
		SSAOSRV                 = nullptr;
		SSAOUAV                 = nullptr;
		SSAOBlurredRTV          = nullptr;
		SSAOBlurredSRV          = nullptr;
		SSAOBlurredUAV          = nullptr;
		SceneColorCopySRV       = nullptr;
		SceneColorCopyTexture   = nullptr;
		ViewportRenderTexture   = nullptr;
		GodRayOcclusionRTV      = nullptr;
		GodRayOcclusionSRV      = nullptr;
		DepthTexture            = nullptr;
		DepthCopyTexture        = nullptr;
		DepthCopySRV            = nullptr;
		StencilCopySRV          = nullptr;

		bDepthPrePassDone       = false;
	}
};
