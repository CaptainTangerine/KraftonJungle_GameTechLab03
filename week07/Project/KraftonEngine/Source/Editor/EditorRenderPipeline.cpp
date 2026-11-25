#include "EditorRenderPipeline.h"
#include "Editor/EditorEngine.h"
#include "Editor/Viewport/LevelEditorViewportClient.h"
#include "Render/Pipeline/Renderer.h"
#include "Render/Proxy/FScene.h"
#include "Viewport/Viewport.h"
#include "Component/CameraComponent.h"
#include "GameFramework/World.h"
#include "Profiling/Stats.h"
#include "Profiling/GPUProfiler.h"

FEditorRenderPipeline::FEditorRenderPipeline(UEditorEngine* InEditor, FRenderer& InRenderer)
	: Editor(InEditor)
{
}

FEditorRenderPipeline::~FEditorRenderPipeline()
{
}

void FEditorRenderPipeline::OnSceneCleared()
{
	GPUOcclusion.InvalidateResults();
}

void FEditorRenderPipeline::Execute(float DeltaTime, FRenderer& Renderer)
{
#if STATS
	FStatManager::Get().TakeSnapshot();
	FGPUProfiler::Get().TakeSnapshot();
	FGPUProfiler::Get().BeginFrame();
	FLightStats::Reset();
	FDrawCallStats::Reset();
#endif

	for (FLevelEditorViewportClient* ViewportClient : Editor->GetLevelViewportClients())
	{
		RenderViewport(ViewportClient, Renderer);
	}

	// 스왑체인 백버퍼 복귀 → ImGui 합성 → Present
	Renderer.BeginFrame();
	{
		SCOPE_STAT_CAT("EditorUI", "5_UI");
		Editor->RenderUI(DeltaTime);
	}

#if STATS
	FGPUProfiler::Get().EndFrame();
#endif

	{
		SCOPE_STAT_CAT("Present", "2_Render");
		Renderer.EndFrame();
	}
}

void FEditorRenderPipeline::RenderViewport(FLevelEditorViewportClient* VC, FRenderer& Renderer)
{
	
	UCameraComponent* Camera = VC->GetCamera();
	if (!Camera) return;

	FViewport* VP = VC->GetViewport();
	if (!VP) return;
	
	// 지연 리사이즈 적용 + 오프스크린 RT 바인딩
	if (VP->ApplyPendingResize())
	{
		Camera->OnResize(static_cast<int32>(VP->GetWidth()), static_cast<int32>(VP->GetHeight()));
	}
		
	if (VC->GetViewport()->GetHeight() <= 0 
		|| VC->GetViewport()->GetWidth() <= 0)
	{
		return;
	}

	ID3D11DeviceContext* Ctx = Renderer.GetFD3DDevice().GetDeviceContext();
	if (!Ctx) return;

	UWorld* World = Editor->GetWorld();
	if (!World) return;
	
	// Early return 끝난 시점부터 통계 적용
	SCOPE_STAT_CAT("RenderViewport", "2_Render");

	// GPU Occlusion 지연 초기화
	if (!GPUOcclusion.IsInitialized())
		GPUOcclusion.Initialize(Renderer.GetFD3DDevice().GetDevice());

	// SSAO Processor 지연 초기화
	if (!SSAOProcessor.IsInitialized())
		SSAOProcessor.Initialize(Renderer.GetFD3DDevice().GetDevice());

	// 이전 프레임 Occlusion 결과 읽기 (staging → OccludedSet)
	GPUOcclusion.ReadbackResults(Ctx);

	// 뷰포트별 렌더 옵션 사용
	const FViewportRenderOptions& Opts = VC->GetRenderOptions();
	const FShowFlags& ShowFlags = Opts.ShowFlags;
	EViewMode ViewMode = Opts.ViewMode;

	// 렌더 시작 (RT 클리어 + DSV 바인딩)
	// DaylightComponent가 SetSkyColor를 설정하면 그걸 배경색으로 사용 (UE SkyAtmosphere 방식)
	{
		float ClearColor[4];
		const FScene& CheckScene = World->GetScene();
		if (CheckScene.HasSkyColor())
			CheckScene.GetSkyColor(ClearColor);
		else
			memcpy(ClearColor, Opts.ClearColor, sizeof(ClearColor));
		VP->BeginRender(Ctx, ClearColor);
	}

	// 1. Frame 설정
	Frame.ClearViewportResources();
	FScene& Scene = World->GetScene();
	Scene.ClearFrameData();

	Frame.SetCameraInfo(Camera);
	Frame.SetRenderSettings(ViewMode, ShowFlags);
	Frame.SetRenderOptions(Opts);
	Frame.SetViewportInfo(VP);
	Frame.ViewportType = Opts.ViewportType;
	Frame.OcclusionCulling = &GPUOcclusion;
	Frame.SSAOProcessor = &SSAOProcessor;
	Frame.LODContext = World->PrepareLODContext();

	// 2. BeginCollect → Proxy → FDrawCommand 직접 변환
	Renderer.BeginCollect(Frame, Scene.GetProxyCount());

	{
		SCOPE_STAT_CAT("Collector", "3_Collect");

		Collector.CollectWorld(World, Frame, Renderer);
		Collector.CollectAlwaysShowLightRanges(World, Scene);

		Collector.CollectGrid(Opts.GridSpacing, Opts.GridHalfLineCount, Scene);
		Collector.CollectDebugDraw(Frame, Scene);

		if (ShowFlags.bOctree)
			Collector.CollectOctreeDebug(World->GetOctree(), Scene);

		if (VC == Editor->GetActiveViewport())
			Collector.CollectOverlayText(Editor->GetOverlayStatSystem(), *Editor, Scene);

		Renderer.BuildDynamicCommands(Frame, &Scene);
	}

	// LightBuffer(b4) 바인딩 — Render 전에 매 뷰포트마다 갱신
	Renderer.UpdateLightBuffer(Ctx, &Scene, Frame.FrustumVolume);

	// 3. GPU 정렬 + 제출 (PostProcess 패스에서 Fog/Outline도 자동 처리)
	
	// 3-1. Depth Pre-pass
	{
		SCOPE_STAT_CAT("DepthPrePass", "4_ExecutePass");
		Renderer.RenderDepthPrePass(Frame);
	}

	// 3-2. Hi-Z Generation (Based on Pre-pass)
	// [Important] Light Culling depends on DepthCopySRV being valid.
	if (GPUOcclusion.IsInitialized())
	{
		SCOPE_STAT_CAT("HiZGenerate", "4_ExecutePass");

		// 깊이 복사를 위해 일시적 언바인딩 (SRV 충돌 방지)
		Ctx->OMSetRenderTargets(0, nullptr, nullptr);
		Ctx->CopyResource(VP->GetDepthCopyTexture(), VP->GetDepthTexture());

		// 렌더 타겟 재바인딩
		ID3D11RenderTargetView* rtv = VP->GetRTV();
		ID3D11DepthStencilView* dsv = VP->GetDSV();
		Ctx->OMSetRenderTargets(1, &rtv, dsv);

		// Hi-Z 생성 (Pre-pass의 깊이 결과물 사용)
		GPUOcclusion.GenerateHiZ(Ctx, VP->GetDepthCopySRV(), VP->GetWidth(), VP->GetHeight());
		Frame.bDepthPrePassDone = true;
	}

	// 3-2-1. Light Culling (Based on Depth Copy)
	{
		SCOPE_STAT_CAT("LightCulling", "4_ExecutePass");
		Renderer.ExecuteLightCulling(Frame);
	}

	// 3-2-2. SSAO (Compute Pass)
	if (Frame.ShowFlags.bSSAO)
	{
		SCOPE_STAT_CAT("SSAO", "4_ExecutePass");
		Renderer.ExecuteSSAO(Frame);
	}

	// 3-3. Main Pass (PostProcess 패스에서 Fog/Outline도 자동 처리)
	{
		SCOPE_STAT_CAT("Renderer.Render", "4_ExecutePass");
		Renderer.Render(Frame);
	}

	// 4. GPU Occlusion — Occlusion Test 디스패치 (생성된 Hi-Z 사용)
	if (GPUOcclusion.IsInitialized())
	{
		SCOPE_STAT_CAT("GPUOcclusion", "4_ExecutePass");

		GPUOcclusion.DispatchOcclusionTest(
			Ctx,
			nullptr, // Hi-Z는 이미 위에서 생성됨
			Collector.GetLastVisibleProxies(),
			Frame.View, Frame.Proj,
			VP->GetWidth(), VP->GetHeight());
	}
}
