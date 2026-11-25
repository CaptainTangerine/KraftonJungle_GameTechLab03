#pragma once

/*
	실제 렌더링을 담당하는 Class 입니다. (Rendering 최상위 클래스)
*/

#include "Render/Types/RenderTypes.h"

#include "Render/Pipeline/FrameContext.h"
#include "Render/Pipeline/DrawCommandList.h"
#include "Render/Proxy/PrimitiveSceneProxy.h"
#include "Render/Device/D3DDevice.h"
#include "Render/Resource/RenderResources.h"
#include "Render/Helper/LineGeometry.h"
#include "Render/Helper/FontGeometry.h"

#include "Render/Culling/LightCulling.h"

class FTextRenderSceneProxy;
class FScene;
class FLightSceneProxy;

class FRenderer
{
public:
	void Create(HWND hWindow);
	void Release();

	void UpdateLightBuffer(ID3D11DeviceContext* Context, const class FScene* Scene, const struct FConvexVolume& Frustum);
	void ExecuteLightCulling(const FFrameContext& Frame);
	void ExecuteSSAO(const FFrameContext& Frame);

	// --- Collect phase: Pipeline이 호출하여 커맨드 수집 시작/종료 ---
	// MaxProxyCount: Scene의 프록시 수. PerObjectCBPool을 미리 할당하여
	// Collect 도중 resize로 인한 포인터 무효화를 방지.
	void BeginCollect(const FFrameContext& Frame, uint32 MaxProxyCount = 0);

	// Collector가 직접 호출 — Proxy → FDrawCommand 변환
	void BuildCommandForProxy(const FPrimitiveSceneProxy& Proxy, ERenderPass Pass);
	void BuildDecalCommandForReceiver(const FPrimitiveSceneProxy& ReceiverProxy, const FPrimitiveSceneProxy& DecalProxy);

	// Collector가 직접 호출 — Font proxy → FontGeometry 배칭
	void AddWorldText(const FTextRenderSceneProxy* TextProxy, const FFrameContext& Frame);

	// Collect 마무리: FScene 경량 데이터(DebugLine, Grid, OverlayText) →
	// 동적 지오메트리 → FDrawCommand 변환. Pipeline의 Collect 블록 끝에서 호출.
	void BuildDynamicCommands(const FFrameContext& Frame, const FScene* Scene);

	// --- Render phase: 정렬 + GPU 제출 ---
	void BeginFrame();
	void RenderDepthPrePass(FFrameContext& Frame);
	void Render(const FFrameContext& Frame);
	void EndFrame();

	FD3DDevice& GetFD3DDevice() { return Device; }
	FRenderResources& GetResources() { return Resources; }


private:
	void UpdateFrameBuffer(ID3D11DeviceContext* Context, const FFrameContext& Frame);

	// 동적 지오메트리 (DebugLine, Grid, OverlayText) — 라인/폰트 헬퍼
	void PrepareDynamicGeometry(const FFrameContext& Frame, const FScene* Scene);

	// 동적 지오메트리 + PostProcess → FDrawCommand (VB 업로드 + 커맨드 생성)
	void BuildDynamicDrawCommands(const FFrameContext& Frame, ID3D11DeviceContext* Ctx, const FScene* Scene);
	void ApplyPassState(FDrawCommand& Cmd, ERenderPass Pass, EViewMode ViewMode);
	void AddEditorLineCommands(ID3D11DeviceContext* Ctx, EViewMode ViewMode);
	void AddGridLineCommands(ID3D11DeviceContext* Ctx, EViewMode ViewMode);
	void AddDepthBufferVisualizeCommand(const FFrameContext& Frame, ID3D11DeviceContext* Ctx);
	void AddLightComplexityCommand(const FFrameContext& Frame, ID3D11DeviceContext* Ctx);
	void AddLightInfluenceCommand(const FFrameContext& Frame, ID3D11DeviceContext* Ctx);
	void AddNormalBufferVisualizeCommand(const FFrameContext& Frame, ID3D11DeviceContext* Ctx);
	void AddPostProcessCommands(const FFrameContext& Frame, ID3D11DeviceContext* Ctx, const FScene* CollectScene);
	void AddFontCommands(ID3D11DeviceContext* Ctx, EViewMode ViewMode);

	// 패스 루프 Pre/Post 이벤트 등록
	void BuildPassEvents(TArray<struct FPassEvent>& PrePassEvents,
		ID3D11DeviceContext* Context, const FFrameContext& Frame, FStateCache& Cache);

	// 패스 루프 종료 후 시스템 텍스처 언바인딩 + 캐시 정리
	void CleanupPassState(ID3D11DeviceContext* Context, FStateCache& Cache);

	// PerObjectCB 풀 관리
	void EnsurePerObjectCBPoolCapacity(uint32 RequiredCount);
	FConstantBuffer* GetPerObjectCBForProxy(const FPrimitiveSceneProxy& Proxy);

	void ChangeUberLitShader(FDrawCommand& Cmd, const FMeshSectionDraw& Section, EViewMode eViewMode);
	
	FVector2 ComputeScreenSunPosition(const FFrameContext& Frame, const FLightSceneProxy* LightFroxy);
	bool IsLitMode(EViewMode eMode);

private:
	FD3DDevice Device;
	FRenderResources Resources;
	FLineGeometry  EditorLines;
	FLineGeometry  GridLines;
	FFontGeometry  FontGeometry;

	// FDrawCommand 기반 렌더링
	FDrawCommandList DrawCommandList;

	TArray<FConstantBuffer> PerObjectCBPool;

	FLightCulling LightCulling;

	TArray<FLightDataGPU> CollectedLights;


	// BeginCollect에서 저장, BuildCommandForProxy에서 사용
	EViewMode CollectViewMode = EViewMode::Lit_BlinnPhong;
	bool bHasSelectionMaskCommands = false;
};
