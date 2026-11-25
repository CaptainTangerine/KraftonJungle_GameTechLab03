#pragma once

#include "Core/CoreTypes.h"

enum class EViewMode : int32
{
	Lit_Gouraud = 0,
	Lit_Lambert,
	Lit_BlinnPhong,
	Lit_BRDF,
	Lit_Toon,
	Unlit,
	Wireframe,
	SolidWireframe,
	SceneDepth,
	LightComplexity,
	LightingOnly,
	LightInfluence,
	Heatmap,
	WorldNormal,
	Count
};

struct FShowFlags
{
	bool bPrimitives = true;
	bool bGrid = true;
	bool bWorldAxis = true;
	bool bGizmo = true;
	bool bBillboardText = true;
	bool bBoundingVolume = false;
	bool bDebugDraw = true;
	bool bOctree = false;
	bool bFog = true;
	bool bFXAA = false;
	bool bSSAO = false;
	bool bGodRay = false;
	bool bLensFlare = false;
};

// 뷰포트 카메라 프리셋 (Perspective / 6방향 Orthographic)
enum class ELevelViewportType : uint8
{
	Perspective,
	Top,		// +Z → -Z
	Bottom,		// -Z → +Z
	Left,		// -Y → +Y
	Right,		// +Y → -Y
	Front,		// +X → -X
	Back,		// -X → +X
	FreeOrthographic	// 자유 각도 Orthographic
};

// 뷰포트별 렌더 옵션 — 각 뷰포트 클라이언트가 독립적으로 소유
struct FViewportRenderOptions
{
	EViewMode ViewMode = EViewMode::Lit_BlinnPhong;
	FShowFlags ShowFlags;

	float GridSpacing = 1.0f;
	int32 GridHalfLineCount = 100;

	float CameraMoveSensitivity = 1.0f;
	float CameraRotateSensitivity = 1.0f;
	ELevelViewportType ViewportType = ELevelViewportType::Perspective;

	// Scene Depth 전용 설정
	int32 SceneDepthVisMode = 1;
	float Exponent = 128.0f;
	float Range = 1000.0f;

	// FXAA 전용 설정
	float EdgeThreshold = 0.125f;
	float EdgeThresholdMin = 0.0625f;

	// GodRay 전용 설정
	float GodRaySunRadius  = 0.25f;
	float GodRayDensity    = 1.f;
	float GodRayDecay      = 0.965f;
	float GodRayWeight     = 0.4f;
	float GodRayExposure   = 0.65f;
	float GodRayIntensity  = 0.25f;

	// Lens flare 전용 설정
	float LensFlareIntensity = 0.5f;
	float LensFlareSpread = 0.8f;
	float LensFlareInnerFadeStart = 0.005f;
	float LensFlareInnerFadeEnd = 0.4f;
	float LensFlareOuterFadeStart = 0.3f;
	float LensFlareOuterFadeEnd = 0.65f;
	float LensFlareOcclusionStrength = 0.5f;

	float ClearColor[4] = { 0.25f, 0.25f, 0.25f, 1.0f };
};
