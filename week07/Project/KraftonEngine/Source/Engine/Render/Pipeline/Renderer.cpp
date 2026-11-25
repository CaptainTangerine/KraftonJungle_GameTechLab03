#include "Renderer.h"

#include <iostream>
#include <algorithm>
#include <functional>
#include "Resource/ResourceManager.h"
#include "Render/Types/RenderTypes.h"
#include "Render/Types/FogParams.h"
#include "Render/Resource/ConstantBufferPool.h"
#include "Render/Proxy/TextRenderSceneProxy.h"
#include "Render/Proxy/FScene.h"
#include "Render/Proxy/LightSceneProxy.h"
#include "Profiling/Stats.h"
#include "Profiling/GPUProfiler.h"
#include "Engine/Runtime/Engine.h"
#include "Profiling/Timer.h"
#include "Render/Pipeline/RenderConstants.h"
#include "Materials/MaterialManager.h"
#include "Render/Resource/ShaderManager.h"
#include "Editor/UI/EditorConsoleWidget.h"
#include "GameFramework/World.h"
#include <random>
#include "Render/PostProcess/SSAOProcessor.h"
#include "Texture/Texture2D.h"

// ============================================================
// FPassEvent — 패스 루프 내 Pre/Post 이벤트 훅
// 특정 패스 조건이 만족되면 콜백을 실행합니다.
// ============================================================
enum class EPassCompare : uint8 { Equal, Less, Greater, LessEqual, GreaterEqual };

struct FPassEvent
{
	ERenderPass    Pass;
	EPassCompare   Compare;
	bool           bOnce;
	bool           bExecuted = false;
	std::function<void()> Fn;

	bool TryExecute(ERenderPass CurPass)
	{
		if (bOnce && bExecuted) return false;

		bool bMatch = false;
		switch (Compare)
		{
		case EPassCompare::Equal:        bMatch = (CurPass == Pass); break;
		case EPassCompare::Less:         bMatch = ((uint32)CurPass < (uint32)Pass); break;
		case EPassCompare::Greater:      bMatch = ((uint32)CurPass > (uint32)Pass); break;
		case EPassCompare::LessEqual:    bMatch = ((uint32)CurPass <= (uint32)Pass); break;
		case EPassCompare::GreaterEqual: bMatch = ((uint32)CurPass >= (uint32)Pass); break;
		}

		if (bMatch) { Fn(); if (bOnce) bExecuted = true; }
		return bMatch;
	}
};


#include <random>

void FRenderer::Create(HWND hWindow)
{
	Device.Create(hWindow);

	if (Device.GetDevice() == nullptr)
	{
		std::cout << "Failed to create D3D Device." << std::endl;
	}

	UTexture2D::InitializeDefaultTextures(Device.GetDevice());

	FShaderManager::Get().Initialize(Device.GetDevice());
	FConstantBufferPool::Get().Initialize(Device.GetDevice());
	Resources.Create(Device.GetDevice());

	EditorLines.Create(Device.GetDevice());
	GridLines.Create(Device.GetDevice());
	FontGeometry.Create(Device.GetDevice());

	// GPU Profiler 초기화
	FGPUProfiler::Get().Initialize(Device.GetDevice(), Device.GetDeviceContext());

	LightCulling.Initialize(Device.GetDevice());
}

void FRenderer::Release()
{
	LightCulling.Release();

	FGPUProfiler::Get().Shutdown();

	EditorLines.Release();
	GridLines.Release();
	FontGeometry.Release();

	for (FConstantBuffer& CB : PerObjectCBPool)
	{
		CB.Release();
	}
	PerObjectCBPool.clear();

	Resources.Release();
	FConstantBufferPool::Get().Release();
	FMaterialManager::Get().Release();
	FShaderManager::Get().Release();
	Device.Release();
}

// ============================================================
// BeginCollect — DrawCommandList + 동적 지오메트리 초기화
// Collector가 BuildCommandForProxy/AddWorldText를 호출하기 전에 반드시 호출
// ============================================================
void FRenderer::BeginCollect(const FFrameContext& Frame, uint32 MaxProxyCount)
{
	// Shader hot reload
	FShaderManager::Get().PollHotReload(Device.GetDevice());

	DrawCommandList.Reset();
	CollectViewMode = Frame.ViewMode;
	bHasSelectionMaskCommands = false;

	// PerObjectCBPool 미리 할당 — Collect 도중 resize로 FDrawCommand.PerObjectCB
	// 포인터가 무효화되는 것을 방지
	if (MaxProxyCount > 0)
		EnsurePerObjectCBPoolCapacity(MaxProxyCount);

	// 동적 지오메트리 초기화
	EditorLines.Clear();
	GridLines.Clear();
	FontGeometry.Clear();
	FontGeometry.ClearScreen();

	if (const FFontResource* FontRes = FResourceManager::Get().FindFont(FName("Default")))
		FontGeometry.EnsureCharInfoMap(FontRes);
}

// ============================================================
// BuildCommandForProxy — Collector가 직접 호출하여 Proxy → FDrawCommand 변환
// ============================================================
void FRenderer::BuildCommandForProxy(const FPrimitiveSceneProxy& Proxy, ERenderPass Pass)
{
	if (!Proxy.MeshBuffer || !Proxy.MeshBuffer->IsValid()) return;

	ID3D11DeviceContext* Ctx = Device.GetDeviceContext();

	// PerObjectCB 업데이트
	FConstantBuffer* PerObjCB = GetPerObjectCBForProxy(Proxy);
	if (PerObjCB && Proxy.NeedsPerObjectCBUpload())
	{
		PerObjCB->Update(Ctx, &Proxy.PerObjectConstants, sizeof(FPerObjectConstants));
		Proxy.ClearPerObjectCBDirty();
	}

	// SelectionMask 커맨드 존재 추적
	if (Pass == ERenderPass::SelectionMask)
		bHasSelectionMaskCommands = true;

	// SectionDraws를 순회하며 커맨드 수집 (모든 프록시는 최소 1개의 섹션을 가지도록 보장됨)
	for (const FMeshSectionDraw& Section : Proxy.SectionDraws)
	{
		// StaticMesh 등: Section.IndexCount가 0이면 스킵 (Rebuild 시 필터링됨)
		// Billboard/SubUV 등: IndexCount를 명시적으로 채워넣거나 0이면 전체 드로우
		if (Section.IndexCount == 0 && Proxy.MeshBuffer && Proxy.MeshBuffer->GetIndexBuffer().GetIndexCount() > 0)
		{
			// Billboard 등에서 IndexCount를 0으로 넘긴 경우 안전하게 보정 (DrawCommandList가 처리해주기도 함)
		}

		FDrawCommand& Cmd = DrawCommandList.AddCommand();


		Cmd.Shader = Section.Shader;

		if (Section.bSupportUberShader)
		{
			ChangeUberLitShader(Cmd, Section, CollectViewMode);

			// Pre-pass용 DepthNormalPass 셰이더 변종도 미리 찾아둠
			FUberLitVarientKey DNKey = {};
			DNKey.TypeID = static_cast<uint32>(EKeyTypeId::UberLit);
			DNKey.Flags = static_cast<uint32>(EUberLitFlags::DepthNormalPass);
			if (Section.NormalSRV)
				DNKey.Flags |= static_cast<uint32>(EUberLitFlags::HasNormal);
			
			Cmd.DepthNormalShader = FShaderManager::Get().GetUberShader(DNKey);
		}
		else if (Section.Shader == FShaderManager::Get().GetShader(EShaderType::StaticMesh))
		{
			// StaticMeshShader도 DepthNormalPass 변종 사용
			FStaticMeshVarientKey DNKey = {};
			DNKey.TypeID = static_cast<uint32>(EKeyTypeId::StaticMesh);
			DNKey.Flags = static_cast<uint32>(EStaticMeshFlags::DepthNormalPass);
			Cmd.DepthNormalShader = FShaderManager::Get().GetUberShader(DNKey);
		}

		// MaterialCB 업데이트
		for (uint32 i = 0; i < 2; ++i)
		{
			const FConstantBufferBinding& Binding = Section.MaterialCB[i];
			if (Binding.Buffer)
			{
				if (!Binding.Buffer->GetBuffer())
				{
					Binding.Buffer->Create(Device.GetDevice(), Binding.Size);
				}
				Binding.Buffer->Update(Ctx, Binding.Data, Binding.Size);
				Cmd.PerShaderCB[i] = Binding.Buffer;
			}
			else
			{
				Cmd.PerShaderCB[i] = nullptr;
			}
		}

		Cmd.Rasterizer = Section.Rasterizer;
		if (CollectViewMode == EViewMode::Wireframe && IsRenderPassWireframeAware(Pass))
		{
			Cmd.Rasterizer = ERasterizerState::WireFrame;
		}

		Cmd.Blend = Section.Blend;
		Cmd.DepthStencil = Section.DepthStencil;

		// SelectionMask 패스일 경우 강제로 RendererState 고정
		if (Pass == ERenderPass::SelectionMask)
		{
			Cmd.Blend = EBlendState::NoColor;
			Cmd.DepthStencil = EDepthStencilState::StencilWrite;
			Cmd.Rasterizer = ERasterizerState::SolidNoCull;
		}

		Cmd.Topology = GetRenderPassTopology(Pass);
		Cmd.MeshBuffer = Proxy.MeshBuffer;
		Cmd.FirstIndex = Section.FirstIndex;
		Cmd.IndexCount = Section.IndexCount;
		Cmd.PerObjectCB = PerObjCB;
		Cmd.DiffuseSRV = Section.DiffuseSRV;
		Cmd.NormalSRV  = Section.NormalSRV;
		Cmd.Pass = Pass;
		Cmd.SortKey = FDrawCommand::BuildSortKey(Pass, Section.Shader, Proxy.MeshBuffer, Section.DiffuseSRV);

		// ExtraTextures 복사
		Cmd.ExtraTextureCount = Section.ExtraTextureCount;
		for (uint32 i = 0; i < Section.ExtraTextureCount; ++i)
		{
			Cmd.ExtraTextures[i] = Section.ExtraTextures[i];
		}
	}
}

void FRenderer::BuildDecalCommandForReceiver(const FPrimitiveSceneProxy& ReceiverProxy, const FPrimitiveSceneProxy& DecalProxy)
{
	if (!ReceiverProxy.MeshBuffer || !ReceiverProxy.MeshBuffer->IsValid()) 
		return;
	
	// Decal은 현재 최소 1개의 Section을 가지도록 보장됨
	if (DecalProxy.SectionDraws.empty()) 
		return;
	const FMeshSectionDraw& DecalSection = DecalProxy.SectionDraws[0];
	if (!DecalSection.Shader || !DecalSection.DiffuseSRV)
		return;

	ID3D11DeviceContext* Ctx = Device.GetDeviceContext();
	const ERenderPass DecalPass = DecalProxy.Pass;

	FConstantBuffer* ReceiverPerObjCB = GetPerObjectCBForProxy(ReceiverProxy);
	if (ReceiverPerObjCB && ReceiverProxy.NeedsPerObjectCBUpload())
	{
		ReceiverPerObjCB->Update(Ctx, &ReceiverProxy.PerObjectConstants, sizeof(FPerObjectConstants));
		ReceiverProxy.ClearPerObjectCBDirty();
	}

	// Decal의 MaterialCB 업데이트
	for (uint32 i = 0; i < 2; ++i)
	{
		const FConstantBufferBinding& Binding = DecalSection.MaterialCB[i];
		if (Binding.Buffer)
		{
			if (!Binding.Buffer->GetBuffer())
			{
				Binding.Buffer->Create(Device.GetDevice(), Binding.Size);
			}
			Binding.Buffer->Update(Ctx, Binding.Data, Binding.Size);
		}
	}

	auto AddDraw = [&](uint32 FirstIndex, uint32 IndexCount)
		{
			if (IndexCount == 0) return;

			FDrawCommand& Cmd = DrawCommandList.AddCommand();
			Cmd.Shader = DecalSection.Shader;

			Cmd.Rasterizer = DecalSection.Rasterizer;
			if (CollectViewMode == EViewMode::Wireframe && IsRenderPassWireframeAware(DecalPass))
			{
				Cmd.Rasterizer = ERasterizerState::WireFrame;
			}

			Cmd.Blend = DecalSection.Blend;
			Cmd.DepthStencil = DecalSection.DepthStencil;

			// 유틸리티 패스 강제 상태
			if (DecalPass == ERenderPass::SelectionMask)
			{
				Cmd.Blend = EBlendState::NoColor;
				Cmd.DepthStencil = EDepthStencilState::StencilWrite;
				Cmd.Rasterizer = ERasterizerState::SolidNoCull;
			}

			Cmd.Topology = GetRenderPassTopology(DecalProxy.Pass);
			Cmd.MeshBuffer = ReceiverProxy.MeshBuffer;
			Cmd.FirstIndex = FirstIndex;
			Cmd.IndexCount = IndexCount;
			Cmd.PerObjectCB = ReceiverPerObjCB;
			Cmd.PerShaderCB[0] = DecalSection.MaterialCB[0].Buffer;
			Cmd.PerShaderCB[1] = DecalSection.MaterialCB[1].Buffer;
			Cmd.DiffuseSRV = DecalSection.DiffuseSRV;
			Cmd.NormalSRV = DecalSection.NormalSRV;
			
			// ExtraTextures 복사
			Cmd.ExtraTextureCount = DecalSection.ExtraTextureCount;
			for (uint32 i = 0; i < DecalSection.ExtraTextureCount; ++i)
			{
				Cmd.ExtraTextures[i] = DecalSection.ExtraTextures[i];
			}

			Cmd.Pass = DecalPass;
			Cmd.SortKey = FDrawCommand::BuildSortKey(DecalPass, DecalSection.Shader, ReceiverProxy.MeshBuffer, DecalSection.DiffuseSRV);
		};

	if (!ReceiverProxy.SectionDraws.empty())
	{
		for (const FMeshSectionDraw& Section : ReceiverProxy.SectionDraws)
		{
			AddDraw(Section.FirstIndex, Section.IndexCount);
		}
	}
	else if (ReceiverProxy.MeshBuffer->GetIndexBuffer().GetBuffer())
	{
		AddDraw(0, ReceiverProxy.MeshBuffer->GetIndexBuffer().GetIndexCount());
	}
}

// ============================================================
// AddWorldText — Collector가 Font 프록시를 배칭할 때 호출
// ============================================================
void FRenderer::AddWorldText(const FTextRenderSceneProxy* TextProxy, const FFrameContext& Frame)
{
	FontGeometry.AddWorldText(
		TextProxy->CachedText,
		TextProxy->CachedBillboardMatrix.GetLocation(),
		Frame.CameraRight,
		Frame.CameraUp,
		TextProxy->CachedBillboardMatrix.GetScale(),
		TextProxy->CachedFontScale
	);
}

//	스왑체인 백버퍼 복귀 — ImGui 합성 직전에 호출
void FRenderer::BeginFrame()
{
	ID3D11DeviceContext* Context = Device.GetDeviceContext();
	ID3D11RenderTargetView* RTV = Device.GetFrameBufferRTV();
	ID3D11DepthStencilView* DSV = Device.GetDepthStencilView();

	Context->ClearRenderTargetView(RTV, Device.GetClearColor());
	Context->ClearDepthStencilView(DSV, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 0.0f, 0);

	const D3D11_VIEWPORT& Viewport = Device.GetViewport();
	Context->RSSetViewports(1, &Viewport);
	Context->OMSetRenderTargets(1, &RTV, DSV);
}

// ============================================================
// BuildDynamicCommands — Collect 마무리: FScene 경량 데이터 → 동적 지오메트리 → FDrawCommand
// Pipeline의 Collect 블록 끝에서 호출.
// ============================================================
void FRenderer::BuildDynamicCommands(const FFrameContext& Frame, const FScene* Scene)
{
	PrepareDynamicGeometry(Frame, Scene);
	BuildDynamicDrawCommands(Frame, Device.GetDeviceContext(), Scene);
}

// ============================================================
// Render — 정렬 + GPU 제출
// BeginCollect + Collector + BuildDynamicCommands 이후에 호출.
// ============================================================

// Opaque 패스의 오브젝트들을 뎁스 버퍼와 노말 버퍼에 렌더링합니다.
void FRenderer::RenderDepthPrePass(FFrameContext& Frame)
{
	ID3D11DeviceContext* Context = Device.GetDeviceContext();
	UpdateFrameBuffer(Context, Frame);

	// 커맨드 정렬 (패스 범위 확정)
	DrawCommandList.Sort();

	// Normal Buffer 클리어
	if (Frame.NormalRTV)
	{
		float ClearNormal[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		Context->ClearRenderTargetView(Frame.NormalRTV, ClearNormal);
	}

	FStateCache Cache;
	Cache.Reset();
	// Depth-Normal Pass: RT0에 Normal 기록, DSV에 Depth 기록
	Cache.RTV = Frame.NormalRTV;
	Cache.DSV = Frame.ViewportDSV;

	// MRT 설정
	Context->OMSetRenderTargets(1, &Frame.NormalRTV, Frame.ViewportDSV);

	uint32 Start, End;
	DrawCommandList.GetPassRange(ERenderPass::Opaque, Start, End);
	if (Start < End)
	{
		const char* PassName = "DepthNormalPrePass";
		SCOPE_STAT_CAT(PassName, "4_ExecutePass");
		GPU_SCOPE_STAT(PassName);

		DrawCommandList.SubmitDepthNormalRange(Start, End, Device, Context, Cache);
	}

	// 렌더 타깃 복구 (Render() 호출 전 메인 타깃으로 되돌림)
	Context->OMSetRenderTargets(1, &Frame.ViewportRTV, Frame.ViewportDSV);

	Frame.bDepthPrePassDone = true;
}

void FRenderer::Render(const FFrameContext& Frame)
{
	ID3D11DeviceContext* Context = Device.GetDeviceContext();
	{
		SCOPE_STAT_CAT("UpdateFrameBuffer", "4_ExecutePass");
		UpdateFrameBuffer(Context, Frame);
	}

	// 시스템 샘플러 영구 바인딩 (s0-s2)
	Resources.BindSystemSamplers(Context);

	// Light Culling SRVs 바인딩 (t100, t101, t102)
	ID3D11ShaderResourceView* LightSRVs[] = {
		LightCulling.GetAllLightsSRV(),
		LightCulling.GetLightIndexListSRV(),
		LightCulling.GetLightGridSRV()
	};
	Context->PSSetShaderResources(ESystemTexSlot::AllLights, 3, LightSRVs);
	Context->VSSetShaderResources(ESystemTexSlot::AllLights, 1, LightSRVs); // Gouraud VS에서 AllLights 접근용

	// 커맨드 정렬 + 패스별 오프셋 빌드
	DrawCommandList.Sort();

	// 단일 StateCache — 패스 간 상태 유지 (DSV Read-Only 전환 등)
	FStateCache Cache;
	Cache.Reset();
	Cache.RTV = Frame.ViewportRTV;
	Cache.DSV = Frame.ViewportDSV;

	// ── Pre/Post 패스 이벤트 등록 ──
	TArray<FPassEvent> PrePassEvents;
	BuildPassEvents(PrePassEvents, Context, Frame, Cache);

	// ── 패스 루프 ──
	for (uint32 i = 0; i < (uint32)ERenderPass::MAX; ++i)
	{
		ERenderPass CurPass = static_cast<ERenderPass>(i);

		for (auto& PrePassEvent : PrePassEvents)
		{
			PrePassEvent.TryExecute(CurPass);
		}

		uint32 Start, End;
		DrawCommandList.GetPassRange(CurPass, Start, End);
		if (Start >= End) continue;

		const char* PassName = GetRenderPassName(CurPass);
		SCOPE_STAT_CAT(PassName, "4_ExecutePass");
		GPU_SCOPE_STAT(PassName);

		// Depth Pre-pass가 이미 수행된 경우, Opaque 패스에서 DepthReadOnly(GreaterEqual)로 최적화. 
		if (Frame.bDepthPrePassDone && CurPass == ERenderPass::Opaque)
		{
			DrawCommandList.SubmitWithStateOverrideRange(Start, End, Device, Context, Cache, 
				EDepthStencilState::DepthReadOnly, EBlendState::Opaque);
		}
		else
		{
			DrawCommandList.SubmitRange(Start, End, Device, Context, Cache);
		}
	}

	CleanupPassState(Context, Cache);
}

// ============================================================
// CleanupPassState — 패스 루프 종료 후 시스템 텍스처 언바인딩 + 캐시 정리
// ============================================================
void FRenderer::CleanupPassState(ID3D11DeviceContext* Context, FStateCache& Cache)
{
	// 시스템 텍스처 언바인딩
	ID3D11ShaderResourceView* nullSRV = nullptr;
	Context->PSSetShaderResources(ESystemTexSlot::SceneDepth, 1, &nullSRV);
	Context->PSSetShaderResources(ESystemTexSlot::SceneColor, 1, &nullSRV);
	Context->PSSetShaderResources(ESystemTexSlot::SceneNormal, 1, &nullSRV);
	Context->PSSetShaderResources(ESystemTexSlot::Stencil, 1, &nullSRV);

	Cache.Cleanup(Context);
	DrawCommandList.Reset();
}

// ============================================================
// BuildPassEvents — 패스 루프 Pre/Post 이벤트 등록
// ============================================================
void FRenderer::BuildPassEvents(TArray<FPassEvent>& PrePassEvents,
	ID3D11DeviceContext* Context, const FFrameContext& Frame, FStateCache& Cache)
{
	if (Frame.ShowFlags.bGodRay && Frame.GodRayOcclusionRTV)
	{
		PrePassEvents.push_back({ ERenderPass::GodRayOcclusion, EPassCompare::Equal, true, false,
			[Context, &Frame, &Cache]()
			{
				const float ClearColor[4] = { 0.f, 0.f, 0.f, 1.f };
				Cache.RTV = Frame.GodRayOcclusionRTV;
				Cache.DSV = nullptr;
				Context->OMSetRenderTargets(1, &Cache.RTV, Cache.DSV);
				Context->ClearRenderTargetView(Frame.GodRayOcclusionRTV, ClearColor);
				Cache.bForceAll = true;
			}
			});

		PrePassEvents.push_back({ ERenderPass::PostProcess, EPassCompare::Equal, true, false,
			[Context, &Frame, &Cache]()
			{
				Cache.RTV = Frame.ViewportRTV;
				Cache.DSV = Frame.ViewportDSV;
				Context->OMSetRenderTargets(1, &Cache.RTV, Cache.DSV);
				Cache.bForceAll = true;
			}
			});
	}

	// CopyResource: Decal 패스 이상 패스 진입 전
	// Depth+Stencil 복사 → 시스템 텍스처 바인딩
	if (Frame.DepthTexture && Frame.DepthCopyTexture)
	{
		const ERenderPass DepthBindStartPass = ERenderPass::Decal;

		PrePassEvents.push_back({ DepthBindStartPass, EPassCompare::GreaterEqual, true, false,
			[Context, &Frame, &Cache]()
			{
				// DepthPrePass가 수행되지 않은 경우에만 여기서 복사 (GodRay/Decal 등에서 사용)
				if (!Frame.bDepthPrePassDone)
				{
					Context->OMSetRenderTargets(0, nullptr, nullptr);
					Context->CopyResource(Frame.DepthCopyTexture, Frame.DepthTexture);
					Context->OMSetRenderTargets(1, &Cache.RTV, Cache.DSV);
				}

				ID3D11ShaderResourceView* depthSRV = Frame.DepthCopySRV;
				Context->PSSetShaderResources(ESystemTexSlot::SceneDepth, 1, &depthSRV);

				if (Frame.StencilCopySRV)
				{
					ID3D11ShaderResourceView* stencilSRV = Frame.StencilCopySRV;
					Context->PSSetShaderResources(ESystemTexSlot::Stencil, 1, &stencilSRV);
				}

				if (Frame.NormalSRV)
				{
					ID3D11ShaderResourceView* normalSRV = Frame.NormalSRV;
					Context->PSSetShaderResources(ESystemTexSlot::SceneNormal, 1, &normalSRV);
				}

				Cache.bForceAll = true;
			}
			});
	}

	// CopySceneColor: FXAA 패스 진입 전 현재 화면 복사 → SceneColorCopySRV로 읽기
	if (Frame.SceneColorCopyTexture && Frame.ViewportRenderTexture)
	{
		PrePassEvents.push_back({ ERenderPass::FXAA, EPassCompare::Equal, true, false,
			[Context, &Frame, &Cache]()
			{
				Context->CopyResource(Frame.SceneColorCopyTexture, Frame.ViewportRenderTexture);
				Context->OMSetRenderTargets(1, &Cache.RTV, Cache.DSV);

				ID3D11ShaderResourceView* sceneColorSRV = Frame.SceneColorCopySRV;
				Context->PSSetShaderResources(ESystemTexSlot::SceneColor, 1, &sceneColorSRV);

				Cache.bForceAll = true;
			}
			});
	}
}

// ============================================================
// PrepareDynamicGeometry — FScene의 경량 데이터 → 라인/폰트 지오메트리
// ============================================================
void FRenderer::PrepareDynamicGeometry(const FFrameContext& Frame, const FScene* Scene)
{
	if (!Scene) return;

	// --- Editor 패스: AABB 디버그 박스 + DebugDraw 라인 ---
	for (const auto& AABB : Scene->GetDebugAABBs())
	{
		EditorLines.AddAABB(FBoundingBox{ AABB.Min, AABB.Max }, AABB.Color);
	}
	for (const auto& Line : Scene->GetDebugLines())
	{
		EditorLines.AddLine(Line.Start, Line.End, Line.Color.ToVector4());
	}

	// --- Grid 패스: 월드 그리드 + 축 ---
	if (Scene->HasGrid())
	{
		const FVector CameraPos = Frame.View.GetInverseFast().GetLocation();
		FVector CameraFwd = Frame.CameraRight.Cross(Frame.CameraUp);
		CameraFwd.Normalize();

		GridLines.AddWorldHelpers(
			Frame.ShowFlags,
			Scene->GetGridSpacing(),
			Scene->GetGridHalfLineCount(),
			CameraPos, CameraFwd, Frame.IsFixedOrtho());
	}

	// --- OverlayFont 패스: 스크린 공간 텍스트 ---
	for (const auto& Text : Scene->GetOverlayTexts())
	{
		if (!Text.Text.empty())
		{
			FontGeometry.AddScreenText(
				Text.Text,
				Text.Position.X,
				Text.Position.Y,
				Frame.ViewportWidth,
				Frame.ViewportHeight,
				Text.Scale
			);
		}
	}
}

// ============================================================
// Dynamic geometry → FDrawCommand 변환 (Font, Line)
// ============================================================
void FRenderer::BuildDynamicDrawCommands(const FFrameContext& Frame, ID3D11DeviceContext* Ctx, const FScene* CollectScene)
{
	EViewMode ViewMode = Frame.ViewMode;

	// --- Editor Lines + Grid Lines → EditorLines 패스 ---
	AddEditorLineCommands(Ctx, ViewMode);
	AddGridLineCommands(Ctx, ViewMode);

	// --- PostProcess: SceneDepth/Normal (디버그 뷰) 또는 일반 포스트 프로세스 (Fog → Outline → FXAA) ---
	if (CollectViewMode == EViewMode::SceneDepth)
	{
		AddDepthBufferVisualizeCommand(Frame, Ctx);
	}
	else if (CollectViewMode == EViewMode::LightInfluence)
	{
		AddLightInfluenceCommand(Frame, Ctx);
	}
	else if (CollectViewMode == EViewMode::LightComplexity)
	{
		AddLightComplexityCommand(Frame, Ctx);
	}
	else if (CollectViewMode == EViewMode::WorldNormal)
	{
		AddNormalBufferVisualizeCommand(Frame, Ctx);
	}
	else
	{
		AddPostProcessCommands(Frame, Ctx, CollectScene);
	}

	// --- Font (World → AlphaBlend, Screen → OverlayFont) ---
	AddFontCommands(Ctx, ViewMode);
}

void FRenderer::ApplyPassState(FDrawCommand& Cmd, ERenderPass Pass, EViewMode ViewMode)
{
	Cmd.Pass = Pass;
	Cmd.Topology = GetRenderPassTopology(Pass);

	// 유틸리티 패스별 기본 상태 정의
	switch (Pass)
	{
	case ERenderPass::EditorLines:
		Cmd.DepthStencil = EDepthStencilState::Default;
		Cmd.Blend = EBlendState::AlphaBlend;
		Cmd.Rasterizer = ERasterizerState::SolidBackCull;
		break;
	case ERenderPass::AlphaBlend: // Font (World) 등
		Cmd.DepthStencil = EDepthStencilState::Default;
		Cmd.Blend = EBlendState::AlphaBlend;
		Cmd.Rasterizer = ERasterizerState::SolidBackCull;
		break;
	case ERenderPass::OverlayFont:
		Cmd.DepthStencil = EDepthStencilState::NoDepth;
		Cmd.Blend = EBlendState::AlphaBlend;
		Cmd.Rasterizer = ERasterizerState::SolidBackCull;
		break;
	default:
		// 그 외 패스는 개별 Add 함수에서 직접 설정하거나 Section 정보를 사용함
		break;
	}

}

void FRenderer::AddEditorLineCommands(ID3D11DeviceContext* Ctx, EViewMode ViewMode)
{
	if (EditorLines.GetLineCount() > 0 && EditorLines.UploadBuffers(Ctx))
	{
		FShader* EditorShader = FShaderManager::Get().GetShader(EShaderType::Editor);
		FDrawCommand& Cmd = DrawCommandList.AddCommand();
		ApplyPassState(Cmd, ERenderPass::EditorLines, ViewMode);
		Cmd.Shader = EditorShader;
		Cmd.RawVB = EditorLines.GetVBBuffer();
		Cmd.RawVBStride = EditorLines.GetVBStride();
		Cmd.RawIB = EditorLines.GetIBBuffer();
		Cmd.IndexCount = EditorLines.GetIndexCount();
		Cmd.SortKey = FDrawCommand::BuildSortKey(ERenderPass::EditorLines, EditorShader, nullptr, nullptr);
	}
}

void FRenderer::AddGridLineCommands(ID3D11DeviceContext* Ctx, EViewMode ViewMode)
{
	if (GridLines.GetLineCount() > 0 && GridLines.UploadBuffers(Ctx))
	{
		FShader* EditorShader = FShaderManager::Get().GetShader(EShaderType::Editor);
		FDrawCommand& Cmd = DrawCommandList.AddCommand();
		ApplyPassState(Cmd, ERenderPass::EditorLines, ViewMode);
		Cmd.Shader = EditorShader;
		Cmd.RawVB = GridLines.GetVBBuffer();
		Cmd.RawVBStride = GridLines.GetVBStride();
		Cmd.RawIB = GridLines.GetIBBuffer();
		Cmd.IndexCount = GridLines.GetIndexCount();
		Cmd.SortKey = FDrawCommand::BuildSortKey(ERenderPass::EditorLines, EditorShader, nullptr, nullptr);
	}
}

// ============================================================
// Depth buffer 시각화 커맨드 추가
// ============================================================
void FRenderer::AddDepthBufferVisualizeCommand(const FFrameContext& Frame, ID3D11DeviceContext* Ctx)
{
	FShader* DepthShader = FShaderManager::Get().GetShader(EShaderType::SceneDepth);
	if (!DepthShader)
	{
		return;
	}

	FConstantBuffer* SceneDepthCB = FConstantBufferPool::Get().GetBuffer(ECBPoolKey::SceneDepth, sizeof(FSceneDepthPConstants));
	FViewportRenderOptions Opts = Frame.GetRenderOptions();
	FSceneDepthPConstants depthData = {};
	depthData.Exponent = Opts.Exponent;
	depthData.NearClip = Frame.NearClip;
	depthData.FarClip = Frame.FarClip;
	depthData.Mode = Opts.SceneDepthVisMode;
	SceneDepthCB->Update(Ctx, &depthData, sizeof(FSceneDepthPConstants));

	FDrawCommand& Cmd = DrawCommandList.AddCommand();
	Cmd.Shader = DepthShader;
	Cmd.DepthStencil = EDepthStencilState::NoDepth;
	Cmd.Blend = EBlendState::AlphaBlend;
	Cmd.Rasterizer = ERasterizerState::SolidNoCull;
	Cmd.Topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	Cmd.VertexCount = 3;
	Cmd.PerShaderCB[0] = SceneDepthCB;
	Cmd.Pass = ERenderPass::PostProcess;
	Cmd.SortKey = FDrawCommand::BuildSortKey(ERenderPass::PostProcess, DepthShader, nullptr, nullptr, 0);
}

void FRenderer::AddLightComplexityCommand(const FFrameContext& Frame, ID3D11DeviceContext* Ctx)
{
	FShader* Shader = FShaderManager::Get().GetShader(EShaderType::LightComplexity);
	if (!Shader) return;

	FDrawCommand& Cmd = DrawCommandList.AddCommand();
	Cmd.Shader = Shader;Cmd.DepthStencil = EDepthStencilState::NoDepth;
	Cmd.Blend = EBlendState::Opaque;
	Cmd.Rasterizer = ERasterizerState::SolidNoCull;
	Cmd.Topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	Cmd.VertexCount = 3;
	Cmd.Pass = ERenderPass::PostProcess;
	Cmd.SortKey = FDrawCommand::BuildSortKey(ERenderPass::PostProcess, Shader, nullptr, nullptr, 3);
}

void FRenderer::AddLightInfluenceCommand(const FFrameContext& Frame, ID3D11DeviceContext* Ctx)
{
	FShader* Shader = FShaderManager::Get().GetShader(EShaderType::LightInfluence);
	if (!Shader) return;

	FDrawCommand& Cmd = DrawCommandList.AddCommand();
	Cmd.Shader = Shader;
	Cmd.DepthStencil = EDepthStencilState::NoDepth;
	Cmd.Blend = EBlendState::Opaque;
	Cmd.Rasterizer = ERasterizerState::SolidNoCull;
	Cmd.Topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	Cmd.VertexCount = 3;
	Cmd.Pass = ERenderPass::PostProcess;
	Cmd.SortKey = FDrawCommand::BuildSortKey(ERenderPass::PostProcess, Shader, nullptr, nullptr, 0);
}

void FRenderer::AddNormalBufferVisualizeCommand(const FFrameContext& Frame, ID3D11DeviceContext* Ctx)
{
	FShader* NormalShader = FShaderManager::Get().GetShader(EShaderType::SceneNormal);
	if (!NormalShader)
	{
		return;
	}

	FDrawCommand& Cmd = DrawCommandList.AddCommand();
	Cmd.Shader = NormalShader;
	Cmd.DepthStencil = EDepthStencilState::NoDepth;
	Cmd.Blend = EBlendState::Opaque;
	Cmd.Rasterizer = ERasterizerState::SolidNoCull;
	Cmd.Topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	Cmd.VertexCount = 3;
	Cmd.Pass = ERenderPass::PostProcess;
	Cmd.SortKey = FDrawCommand::BuildSortKey(ERenderPass::PostProcess, NormalShader, nullptr, nullptr, 0);
}

// ============================================================
// PostProcess 렌더 커맨드 추가
// ============================================================
void FRenderer::AddPostProcessCommands(const FFrameContext& Frame, ID3D11DeviceContext* Ctx, const FScene* CollectScene)
{
	// SSAO Apply Pass (Multiplicative blend)
	if (Frame.ShowFlags.bSSAO)
	{
		FShader* ApplyShader = FShaderManager::Get().GetShader(EShaderType::SSAOApply);
		if (ApplyShader)
		{
			FDrawCommand& Cmd = DrawCommandList.AddCommand();
			Cmd.Shader = ApplyShader;
			// RenderTargetOverride = nullptr (Main target)
			Cmd.DepthStencil = EDepthStencilState::NoDepth;
			Cmd.Blend = EBlendState::Multiply;
			Cmd.Rasterizer = ERasterizerState::SolidNoCull;
			Cmd.Topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			Cmd.VertexCount = 3;
			Cmd.DiffuseSRV = Frame.SSAOBlurredSRV; // Input: Result of Blur pass
			Cmd.Pass = ERenderPass::PostProcess;
			Cmd.SortKey = FDrawCommand::BuildSortKey(ERenderPass::PostProcess, ApplyShader, nullptr, nullptr, 7);
		}
	}
	
	// HeightFog (UserBits=10 → Outline보다 먼저)
	if (Frame.ShowFlags.bFog && CollectScene && CollectScene->HasFog())
	{
		FShader* FogShader = FShaderManager::Get().GetShader(EShaderType::HeightFog);
		if (FogShader)
		{
			// Fog CB (b6) 업데이트
			FConstantBuffer* FogCB = FConstantBufferPool::Get().GetBuffer(ECBPoolKey::Fog, sizeof(FFogConstants));
			const FFogParams& FogParams = CollectScene->GetFogParams();
			FFogConstants fogData = {};
			fogData.InscatteringColor = FogParams.InscatteringColor;
			fogData.Density = FogParams.Density;
			fogData.HeightFalloff = FogParams.HeightFalloff;
			fogData.FogBaseHeight = FogParams.FogBaseHeight;
			fogData.StartDistance = FogParams.StartDistance;
			fogData.CutoffDistance = FogParams.CutoffDistance;
			fogData.MaxOpacity = FogParams.MaxOpacity;
			FogCB->Update(Ctx, &fogData, sizeof(FFogConstants));

			FDrawCommand& Cmd = DrawCommandList.AddCommand();
			Cmd.Shader = FogShader;
			Cmd.DepthStencil = EDepthStencilState::NoDepth;
			Cmd.Blend = EBlendState::AlphaBlend;
			Cmd.Rasterizer = ERasterizerState::SolidNoCull;
			Cmd.Topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			Cmd.VertexCount = 3;  // Fullscreen triangle (SV_VertexID)
			Cmd.PerShaderCB[0] = FogCB;
			Cmd.Pass = ERenderPass::PostProcess;
			Cmd.SortKey = FDrawCommand::BuildSortKey(ERenderPass::PostProcess, FogShader, nullptr, nullptr, 10);
		}
	}

	// Outline (UserBits=11 → HeightFog 뒤)
	if (bHasSelectionMaskCommands)
	{
		FShader* PPShader = FShaderManager::Get().GetShader(EShaderType::OutlinePostProcess);
		if (PPShader)
		{
			// Outline CB (b3) 업데이트
			FConstantBuffer* OutlineCB = FConstantBufferPool::Get().GetBuffer(ECBPoolKey::Outline, sizeof(FOutlinePostProcessConstants));
			FOutlinePostProcessConstants ppConstants;
			ppConstants.OutlineColor = FVector4(1.0f, 0.5f, 0.0f, 1.0f);
			ppConstants.OutlineThickness = 3.0f;
			OutlineCB->Update(Ctx, &ppConstants, sizeof(ppConstants));

			FDrawCommand& Cmd = DrawCommandList.AddCommand();
			Cmd.Shader = PPShader;
			Cmd.DepthStencil = EDepthStencilState::NoDepth;
			Cmd.Blend = EBlendState::AlphaBlend;
			Cmd.Rasterizer = ERasterizerState::SolidNoCull;
			Cmd.Topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			Cmd.VertexCount = 3;  // Fullscreen triangle (SV_VertexID)
			Cmd.PerShaderCB[0] = OutlineCB;
			Cmd.Pass = ERenderPass::PostProcess;
			Cmd.SortKey = FDrawCommand::BuildSortKey(ERenderPass::PostProcess, PPShader, nullptr, nullptr, 11);
		}
	}

	// FXAA
	if (Frame.ShowFlags.bFXAA)
	{
		FShader* FXAAShader = FShaderManager::Get().GetShader(EShaderType::FXAA);
		if (FXAAShader)
		{
			FConstantBuffer* FXAACB = FConstantBufferPool::Get().GetBuffer(ECBPoolKey::FXAA, sizeof(FFXAAConstants));
			FViewportRenderOptions Opts = Frame.GetRenderOptions();
			FFXAAConstants FXAAData = {};
			FXAAData.EdgeThreshold = Opts.EdgeThreshold;
			FXAAData.EdgeThresholdMin = Opts.EdgeThresholdMin;
			FXAACB->Update(Ctx, &FXAAData, sizeof(FFXAAConstants));

			FDrawCommand& Cmd = DrawCommandList.AddCommand();
			Cmd.Shader = FXAAShader;
			Cmd.DepthStencil = EDepthStencilState::NoDepth;
			Cmd.Blend = EBlendState::Opaque;
			Cmd.Rasterizer = ERasterizerState::SolidNoCull;
			Cmd.Topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			Cmd.VertexCount = 3;
			Cmd.PerShaderCB[0] = FXAACB;
			Cmd.Pass = ERenderPass::FXAA;
			Cmd.SortKey = FDrawCommand::BuildSortKey(ERenderPass::FXAA, FXAAShader, nullptr, nullptr, 0);
		}
	}

	// heightFog보다 먼저 sun 기반 post-process 처리
	if ((Frame.ShowFlags.bGodRay || Frame.ShowFlags.bLensFlare) && IsLitMode(Frame.ViewMode) && CollectScene && !Frame.bIsOrtho)
	{
		FViewportRenderOptions Opts = Frame.GetRenderOptions();

		FVector2 ScreenSunPos = FVector2(-1.0f, -1.0f);
		FVector DirectionalLightColor = FVector(1.0f, 1.0f, 1.0f);
		bool bFoundDirectionalLight = false;
		// 태양 스크린 좌표 계산
		for (const FLightSceneProxy* LightProxy : CollectScene->GetLights())
		{
			if (!LightProxy || LightProxy->LightType != ELightType::Directional) continue;

			// 태양은 디렉셔널 기준으로 카메라에서 파 클립의 절반에 위치
			ScreenSunPos = ComputeScreenSunPosition(Frame, LightProxy);
			DirectionalLightColor = FVector(LightProxy->LightColor.X, LightProxy->LightColor.Y, LightProxy->LightColor.Z);
			bFoundDirectionalLight = true;
			break;
		}

		if (!bFoundDirectionalLight)
		{
			return;
		}

		if (Frame.ShowFlags.bGodRay)
		{
			// GodRay 오클루전 패스
			FShader* GodRayOcclusionShader = FShaderManager::Get().GetShader(EShaderType::GodRayOcclusion);
			if (GodRayOcclusionShader)
			{
				FConstantBuffer* GodRayOccCB = FConstantBufferPool::Get().GetBuffer(ECBPoolKey::GodRayOcclusion, sizeof(FGodRayOcclusionConstants));
				FGodRayOcclusionConstants GodRayOccData = {};
				GodRayOccData.ScreenSunPos = ScreenSunPos;
				GodRayOccData.SunRadius = Opts.GodRaySunRadius;
				GodRayOccData.AspectRatio = (Frame.ViewportHeight > 0.0f)
					? (Frame.ViewportWidth / Frame.ViewportHeight)
					: 1.0f;
				GodRayOccCB->Update(Ctx, &GodRayOccData, sizeof(FGodRayOcclusionConstants));

				FDrawCommand& Cmd = DrawCommandList.AddCommand();
				Cmd.Shader = GodRayOcclusionShader;
				Cmd.DepthStencil = EDepthStencilState::NoDepth;
				Cmd.Blend = EBlendState::Opaque;
				Cmd.Rasterizer = ERasterizerState::SolidNoCull;
				Cmd.Topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
				Cmd.VertexCount = 3;
				Cmd.PerShaderCB[0] = GodRayOccCB;
				Cmd.Pass = ERenderPass::GodRayOcclusion;
				Cmd.SortKey = FDrawCommand::BuildSortKey(ERenderPass::GodRayOcclusion, GodRayOcclusionShader, nullptr, nullptr, 0);
			}

			// GodRay Radial Blur 패스
			FShader* GodRayShader = FShaderManager::Get().GetShader(EShaderType::GodRay);
			if (GodRayShader)
			{
				FConstantBuffer* GodRayCB = FConstantBufferPool::Get().GetBuffer(ECBPoolKey::GodRay, sizeof(FGodRayConstants));
				FGodRayConstants GodRayData = {};
				GodRayData.Density = Opts.GodRayDensity;
				GodRayData.Decay = Opts.GodRayDecay;
				GodRayData.Weight = Opts.GodRayWeight;
				GodRayData.Exposure = Opts.GodRayExposure;
				GodRayData.Intensity = Opts.GodRayIntensity;
				GodRayData.ScreenSunPos = ScreenSunPos;
				GodRayData.LightColor = DirectionalLightColor;

				GodRayCB->Update(Ctx, &GodRayData, sizeof(FGodRayConstants));

				FDrawCommand& Cmd = DrawCommandList.AddCommand();
				Cmd.Shader = GodRayShader;
				Cmd.DepthStencil = EDepthStencilState::NoDepth;
				Cmd.Blend = EBlendState::Additive;
				Cmd.Rasterizer = ERasterizerState::SolidNoCull;
				Cmd.Topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
				Cmd.VertexCount = 3;
				Cmd.PerShaderCB[0] = GodRayCB;
				Cmd.Pass = ERenderPass::PostProcess;
				Cmd.DiffuseSRV = Frame.GodRayOcclusionSRV;

				Cmd.SortKey = FDrawCommand::BuildSortKey(ERenderPass::PostProcess, GodRayShader, nullptr, nullptr, 1);
			}
		}

		if (Frame.ShowFlags.bLensFlare)
		{
			const FParticleResource* LensFlareAtlas = FResourceManager::Get().FindParticle(FName("Lens_Flare"));
			FShader* LensFlareShader = FShaderManager::Get().GetShader(EShaderType::LensFlare);
			if (LensFlareAtlas && LensFlareAtlas->IsLoaded() && LensFlareShader)
			{
				FConstantBuffer* LensFlareCB = FConstantBufferPool::Get().GetBuffer(ECBPoolKey::LensFlare, sizeof(FLensFlareConstants));
				FLensFlareConstants LensFlareData = {};
				LensFlareData.ScreenSunPos = ScreenSunPos;
				LensFlareData.AspectRatio = (Frame.ViewportHeight > 0.0f)
					? (Frame.ViewportWidth / Frame.ViewportHeight)
					: 1.0f;
				LensFlareData.Intensity = Opts.LensFlareIntensity;
				LensFlareData.ScreenCenter = FVector2(0.5f, 0.5f);
				LensFlareData.InnerFadeStart = std::clamp(Opts.LensFlareInnerFadeStart, 0.0f, 1.0f);
				LensFlareData.InnerFadeEnd = std::max(LensFlareData.InnerFadeStart + 0.001f, Opts.LensFlareInnerFadeEnd);
				LensFlareData.OuterFadeStart = std::max(LensFlareData.InnerFadeEnd, Opts.LensFlareOuterFadeStart);
				LensFlareData.OuterFadeEnd = std::max(LensFlareData.OuterFadeStart + 0.001f, Opts.LensFlareOuterFadeEnd);
				LensFlareData.SpreadScale = std::max(0.0f, Opts.LensFlareSpread);
				LensFlareData.OcclusionStrength = std::clamp(Opts.LensFlareOcclusionStrength, 0.0f, 1.0f);
				LensFlareData.AtlasColumns = LensFlareAtlas->Columns;
				LensFlareData.AtlasRows = LensFlareAtlas->Rows;
				LensFlareData.FlareCount = 9;
				LensFlareCB->Update(Ctx, &LensFlareData, sizeof(FLensFlareConstants));

				FDrawCommand& Cmd = DrawCommandList.AddCommand();
				Cmd.Shader = LensFlareShader;
				Cmd.DepthStencil = EDepthStencilState::NoDepth;
				Cmd.Blend = EBlendState::Additive;
				Cmd.Rasterizer = ERasterizerState::SolidNoCull;
				Cmd.Topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
				Cmd.VertexCount = 3;
				Cmd.PerShaderCB[0] = LensFlareCB;
				Cmd.Pass = ERenderPass::PostProcess;
				Cmd.DiffuseSRV = LensFlareAtlas->SRV;
				Cmd.SortKey = FDrawCommand::BuildSortKey(ERenderPass::PostProcess, LensFlareShader, nullptr, LensFlareAtlas->SRV, 2);
			}
		}
	}

}

void FRenderer::AddFontCommands(ID3D11DeviceContext* Ctx, EViewMode ViewMode)
{
	const FFontResource* FontRes = FResourceManager::Get().FindFont(FName("Default"));
	if (FontRes && FontRes->IsLoaded())
	{
		if (FontGeometry.GetWorldQuadCount() > 0 && FontGeometry.UploadWorldBuffers(Ctx))
		{
			FShader* FontShader = FShaderManager::Get().GetShader(EShaderType::Font);
			FDrawCommand& Cmd = DrawCommandList.AddCommand();
			ApplyPassState(Cmd, ERenderPass::AlphaBlend, ViewMode);
			Cmd.Shader = FontShader;
			Cmd.RawVB = FontGeometry.GetWorldVBBuffer();
			Cmd.RawVBStride = FontGeometry.GetWorldVBStride();
			Cmd.RawIB = FontGeometry.GetWorldIBBuffer();
			Cmd.IndexCount = FontGeometry.GetWorldIndexCount();
			Cmd.DiffuseSRV = FontRes->SRV;
			Cmd.SortKey = FDrawCommand::BuildSortKey(ERenderPass::AlphaBlend, FontShader, nullptr, FontRes->SRV);
		}

		if (FontGeometry.GetScreenQuadCount() > 0 && FontGeometry.UploadScreenBuffers(Ctx))
		{
			FShader* OverlayShader = FShaderManager::Get().GetShader(EShaderType::OverlayFont);
			FDrawCommand& Cmd = DrawCommandList.AddCommand();
			ApplyPassState(Cmd, ERenderPass::OverlayFont, ViewMode);
			Cmd.Shader = OverlayShader;
			Cmd.RawVB = FontGeometry.GetScreenVBBuffer();
			Cmd.RawVBStride = FontGeometry.GetScreenVBStride();
			Cmd.RawIB = FontGeometry.GetScreenIBBuffer();
			Cmd.IndexCount = FontGeometry.GetScreenIndexCount();
			Cmd.DiffuseSRV = FontRes->SRV;
			Cmd.SortKey = FDrawCommand::BuildSortKey(ERenderPass::OverlayFont, OverlayShader, nullptr, FontRes->SRV);
		}
	}
}

// ============================================================
// PerObjectCB 풀 관리
// ============================================================

void FRenderer::EnsurePerObjectCBPoolCapacity(uint32 RequiredCount)
{
	if (PerObjectCBPool.size() >= RequiredCount)
	{
		return;
	}

	const size_t OldCount = PerObjectCBPool.size();
	PerObjectCBPool.resize(RequiredCount);

	ID3D11Device* D3DDevice = Device.GetDevice();
	for (size_t Index = OldCount; Index < PerObjectCBPool.size(); ++Index)
	{
		PerObjectCBPool[Index].Create(D3DDevice, sizeof(FPerObjectConstants));
	}
}

FConstantBuffer* FRenderer::GetPerObjectCBForProxy(const FPrimitiveSceneProxy& Proxy)
{
	if (Proxy.ProxyId == UINT32_MAX)
	{
		return nullptr;
	}

	EnsurePerObjectCBPoolCapacity(Proxy.ProxyId + 1);
	return &PerObjectCBPool[Proxy.ProxyId];
}

void FRenderer::ChangeUberLitShader(FDrawCommand& Cmd, const FMeshSectionDraw& Section, EViewMode eViewMode)
{
	// Key 이후 ViewMode에 따른 셰이더 덮어쓰기
	// NormalMap은 Lit모드와 WorldNormal모드일 때 적용
	if (eViewMode == EViewMode::Lit_Gouraud ||
		eViewMode == EViewMode::Lit_Lambert ||
		eViewMode == EViewMode::Lit_BlinnPhong ||
		eViewMode == EViewMode::Lit_BRDF	||
		eViewMode == EViewMode::Lit_Toon	||
		eViewMode == EViewMode::WorldNormal ||
		eViewMode == EViewMode::LightingOnly)
	{
		FUberLitVarientKey UberKey = {};
		UberKey.TypeID = static_cast<uint32>(EKeyTypeId::UberLit);

		switch (eViewMode)
		{
		case EViewMode::Lit_Gouraud:
			UberKey.Flags = static_cast<uint32>(EUberLitFlags::Gouraud);
			break;
		case EViewMode::Lit_Lambert:
			UberKey.Flags = static_cast<uint32>(EUberLitFlags::Lambert);
			break;
		case EViewMode::Lit_BlinnPhong:
			UberKey.Flags = static_cast<uint32>(EUberLitFlags::Phong);
			break;
		case EViewMode::Lit_BRDF:
			UberKey.Flags = static_cast<uint32>(EUberLitFlags::BRDF);
			break;
		case EViewMode::Lit_Toon:
			UberKey.Flags = static_cast<uint32>(EUberLitFlags::Toon);
			break;
		case EViewMode::WorldNormal:
			// WorldNormal 뷰 모드는 이제 전용 포스트 프로세스로 처리하지만, 
			// 만약 여기에 진입한다면 기본 셰이더를 할당
			UberKey.Flags = static_cast<uint32>(EUberLitFlags::Phong);
			break;
		case EViewMode::LightingOnly:
			// LightingOnly는 기본 조명 모델(Phong)으로 그리고, FrameBuffer.bLightingOnly로 알베도만 회색화
			UberKey.Flags = static_cast<uint32>(EUberLitFlags::Phong);
			break;
		default:
			// 아무것도 안함
			break;
		}

		if (Section.NormalSRV)
		{
			UberKey.Flags |= static_cast<uint32>(EUberLitFlags::HasNormal);
		}

		Cmd.Shader = FShaderManager::Get().GetUberShader(UberKey);
	}
	// 히트맵 시각화
	else if (eViewMode == EViewMode::Heatmap)
	{
		Cmd.Shader = FShaderManager::Get().GetShader(EShaderType::Heatmap);
	}
}

FVector2 FRenderer::ComputeScreenSunPosition(const FFrameContext& Frame, const FLightSceneProxy* LightProxy)
{
	FVector2 ScreenSunPos = FVector2(-1.0f, -1.0f);

	const FDirectionalLightSceneProxy* DirLight = static_cast<const FDirectionalLightSceneProxy*>(LightProxy);

	FVector LightDir = DirLight->Direction;
	LightDir.Normalize();

	FVector SunWorldPos = Frame.CameraPosition - LightDir * (Frame.FarClip * 0.5f);

	FMatrix ViewProj = Frame.View * Frame.Proj;
	float cw = SunWorldPos.X * ViewProj.M[0][3] + SunWorldPos.Y * ViewProj.M[1][3] + SunWorldPos.Z * ViewProj.M[2][3] + ViewProj.M[3][3];

	if (cw > 0.0001f)
	{
		FVector NdcPos = ViewProj.TransformPositionWithW(SunWorldPos);
		ScreenSunPos.X = NdcPos.X * 0.5f + 0.5f;
		ScreenSunPos.Y = -NdcPos.Y * 0.5f + 0.5f;
	}
	return ScreenSunPos;
}

bool FRenderer::IsLitMode(EViewMode eMode)
{
	return (eMode == EViewMode::Lit_Gouraud 
		||  eMode == EViewMode::Lit_Lambert
		||  eMode == EViewMode::Lit_BlinnPhong
		||  eMode == EViewMode::Lit_Toon);
}


//	Present the rendered frame to the screen. 반드시 Render 이후에 호출되어야 함.
void FRenderer::EndFrame()
{
	Device.Present();
}

void FRenderer::UpdateFrameBuffer(ID3D11DeviceContext* Context, const FFrameContext& Frame)
{
	FFrameConstants frameConstantData = {};
	frameConstantData.View = Frame.View;
	frameConstantData.Projection = Frame.Proj;
	frameConstantData.InvProjection = Frame.Proj.GetInverse();
	frameConstantData.InvViewProj = (Frame.View * Frame.Proj).GetInverse();
	frameConstantData.bUseWireframe = (Frame.ViewMode == EViewMode::Wireframe) ? 1.0f : 0.0f;
	frameConstantData.WireframeColor = Frame.WireframeColor;
	frameConstantData.CameraWorldPos = Frame.CameraPosition;
	frameConstantData.ViewportSize = FVector2(Frame.ViewportWidth, Frame.ViewportHeight);
	frameConstantData.ViewportOffset = FVector2(Frame.ViewportOffsetX, Frame.ViewportOffsetY);
	frameConstantData.TotalLightCount = (int32)CollectedLights.size();
	frameConstantData.bUseSolidWireframe = (Frame.ViewMode == EViewMode::SolidWireframe) ? 1.0f : 0.0f;
	frameConstantData.bLightingOnly = (Frame.ViewMode == EViewMode::LightingOnly) ? 1.0f : 0.0f;

	// Logarithmic slicing for Cluster-based Light Culling (32 slices)
	float NearClip = Frame.NearClip;
	float FarClip = Frame.FarClip;
	frameConstantData.ClusterScale = 32.0f / log2(FarClip / NearClip);
	frameConstantData.ClusterBias = -(32.0f * log2(NearClip) / log2(FarClip / NearClip));
	frameConstantData.GNearClip = NearClip;

	if (GEngine && GEngine->GetTimer())
	{
		frameConstantData.Time = static_cast<float>(GEngine->GetTimer()->GetTotalTime());
	}

	Resources.FrameBuffer.Update(Context, &frameConstantData, sizeof(FFrameConstants));
	ID3D11Buffer* b0 = Resources.FrameBuffer.GetBuffer();
	Context->VSSetConstantBuffers(ECBSlot::Frame, 1, &b0);
	Context->PSSetConstantBuffers(ECBSlot::Frame, 1, &b0);
}

void FRenderer::UpdateLightBuffer(ID3D11DeviceContext* Context, const FScene* Scene, const FConvexVolume& Frustum)
{
	CollectedLights.clear();
	int32 TotalLights = 0;
	int32 VisibleAfterCPUFrustum = 0;

	if (Scene)
	{
		const auto& SceneLights = Scene->GetLights();
		for (auto It = SceneLights.begin(); It != SceneLights.end(); ++It)
		{
			FLightSceneProxy* LightProxy = *It;
			if (!LightProxy) continue;

			FLightParams Params;
			LightProxy->GetLightParameters(Params);

			if (!Params.bVisible || Params.Intensity <= 0.f) continue;

			TotalLights++;

			// For Tile-Based Culling, we collect ALL lights that pass basic visibility.
			// However, we can still do a broad CPU frustum cull to reduce the load on the GPU.
			if (Params.LightType == ELightType::Point || Params.LightType == ELightType::Spot)
			{
				if (!Frustum.IntersectSphere(Params.Position, Params.AttenuationRadius))
				{
					continue;
				}
			}

			VisibleAfterCPUFrustum++;

			FLightDataGPU LightData = {};
			LightData.LightType = static_cast<int32>(Params.LightType);
			LightData.Intensity = Params.Intensity;
			LightData.LightColor = Params.LightColor;
			LightData.Position = Params.Position;
			LightData.Direction = Params.Direction;
			LightData.AttenuationRadius = Params.AttenuationRadius;
			LightData.LightFalloffExponent = Params.LightFalloffExponent;
			LightData.InnerConeAngle = Params.InnerConeAngle;
			LightData.OuterConeAngle = Params.OuterConeAngle;

			CollectedLights.push_back(LightData);
		}
	}

	if (TotalLights > 0)
	{
		// Note: CulledByFrustum stat now only counts CPU-side culling. 
		// Exact GPU culling counts would need a readback (which we avoid for performance).
		FLightStats::Record(TotalLights, VisibleAfterCPUFrustum, TotalLights - VisibleAfterCPUFrustum);
	}
}

void FRenderer::ExecuteLightCulling(const FFrameContext& Frame)
{
	ID3D11DeviceContext* Context = Device.GetDeviceContext();
	LightCulling.Execute(
		Context,
		Frame.DepthCopySRV, // Use the depth buffer from pre-pass (must be copied first)
		Frame.View,
		Frame.Proj,
		Frame.ViewportWidth,
		Frame.ViewportHeight,
		Frame.NearClip,
		Frame.FarClip,
		CollectedLights
	);
}

void FRenderer::ExecuteSSAO(const FFrameContext& Frame)
{
	if (!Frame.ShowFlags.bSSAO || !Frame.SSAOProcessor) return;

	ID3D11DeviceContext* Context = Device.GetDeviceContext();
	Frame.SSAOProcessor->Execute(
		Context,
		Frame.DepthCopySRV,
		Frame.NormalSRV,
		Frame.SSAOUAV,
		Frame.SSAOBlurredUAV,
		Frame.SSAOSRV,
		Frame.View,
		Frame.Proj,
		(uint32)Frame.ViewportWidth,
		(uint32)Frame.ViewportHeight
	);
}

