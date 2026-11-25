#pragma once

//	Windows API Include
#define NOMINMAX
#include <Windows.h>
#include <windowsx.h>

//	D3D API Include
#pragma comment(lib, "user32")
#pragma comment(lib, "d3d11")
#pragma comment(lib, "d3dcompiler")

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_5.h>

#pragma comment(lib, "dxgi")
#include "Core/CoreTypes.h"
#include "Render/Types/RenderStateTypes.h"

//	Mesh Shape Enum — MeshBufferManager 조회용 (순수 기하 형상)
enum class EMeshShape
{
	Cube,
	Sphere,
	Plane,
	Quad,
	TexturedQuad,
	TransGizmo,
	RotGizmo,
	ScaleGizmo,
};

enum class ERenderPass : uint32
{
	Opaque,			// 불투명 지오메트리 (StaticMesh 등)
	Decal,			// 데칼 (DepthReadOnly)
	AlphaBlend,		// 반투명 지오메트리 (Font, SubUV, Billboard, Translucent)
	SelectionMask,	// 선택 스텐실 마스크
	GodRayOcclusion,
	PostProcess,	// 아웃라인 풀스크린, Fog, SceneDepth
	FXAA,			// FXAA 안티앨리어싱 (SceneColor 복사 후 실행)
	EditorLines,	// 디버그 라인 + 그리드 (LINELIST) — Fog 뒤로 이동 (에디터 가시성)
	EditorOverlay,	// 에디터 전용 아이콘 빌보드 등 — Fog/PostProcess 이후에 그려 영향 배제 (언리얼 방식)
	GizmoOuter,		// 기즈모 외곽 (깊이 테스트 O)
	GizmoInner,		// 기즈모 내부 (깊이 무시)
	OverlayFont,	// 스크린 공간 텍스트 (깊이 무시)
	MAX
};

inline const char* GetRenderPassName(ERenderPass Pass)
{
	static const char* Names[] = {
		"RenderPass::Opaque",
		"RenderPass::Decal",
		"RenderPass::AlphaBlend",
		"RenderPass::SelectionMask",
		"RenderPass::GodRayOcclusion",
		"RenderPass::PostProcess",
		"RenderPass::FXAA",
		"RenderPass::EditorLines",
		"RenderPass::EditorOverlay",
		"RenderPass::GizmoOuter",
		"RenderPass::GizmoInner",
		"RenderPass::OverlayFont",
	};
	static_assert(ARRAYSIZE(Names) == (uint32)ERenderPass::MAX, "Names must match ERenderPass entries");
	return Names[(uint32)Pass];
}

inline D3D11_PRIMITIVE_TOPOLOGY GetRenderPassTopology(ERenderPass Pass)
{
	if (Pass == ERenderPass::EditorLines)
	{
		return D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
	}
	return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
}

inline bool IsRenderPassWireframeAware(ERenderPass Pass)
{
	switch (Pass)
	{
	case ERenderPass::Opaque:
	case ERenderPass::Decal:
	case ERenderPass::AlphaBlend:
		return true;
	default:
		return false;
	}
}

namespace RenderStateStrings
{
	inline constexpr FEnumEntry RenderPassMap[] =
	{
		{ "Opaque",           (int)ERenderPass::Opaque },
		{ "Decal",            (int)ERenderPass::Decal },
		{ "AlphaBlend",       (int)ERenderPass::AlphaBlend },
		{ "SelectionMask",    (int)ERenderPass::SelectionMask },
		{ "GodRayOcclusion",  (int)ERenderPass::GodRayOcclusion },
		{ "PostProcess",      (int)ERenderPass::PostProcess },
		{ "FXAA",             (int)ERenderPass::FXAA },
		{ "EditorLines",      (int)ERenderPass::EditorLines },
		{ "EditorOverlay",    (int)ERenderPass::EditorOverlay },
		{ "GizmoOuter",       (int)ERenderPass::GizmoOuter },
		{ "GizmoInner",       (int)ERenderPass::GizmoInner },
		{ "OverlayFont",      (int)ERenderPass::OverlayFont },
	};

	static_assert(ARRAYSIZE(RenderPassMap) == (int)ERenderPass::MAX, "RenderPassMap must match ERenderPass entries");
}