#include "Editor/Viewport/FLevelViewportLayout.h"

#include "Editor/EditorEngine.h"
#include "Editor/Viewport/LevelEditorViewportClient.h"
#include "Editor/Settings/EditorSettings.h"
#include "Editor/Selection/SelectionManager.h"
#include "Engine/Runtime/WindowsWindow.h"
#include "Engine/Input/InputSystem.h"
#include "GameFramework/AActor.h"
#include "GameFramework/DecalActor.h"
#include "GameFramework/LightActor.h"
#include "GameFramework/DaylightActor.h"
#include "GameFramework/StaticMeshActor.h"
#include "GameFramework/World.h"
#include "Render/Pipeline/Renderer.h"
#include "Render/Resource/Shader.h"
#include "Viewport/Viewport.h"
#include "UI/SSplitter.h"
#include "Math/MathUtils.h"
#include "Platform/Paths.h"
#include "ImGui/imgui.h"
#include "WICTextureLoader.h"
#include "Component/CameraComponent.h"
#include "Component/GizmoComponent.h"

#include <algorithm>

namespace
{
enum class EToolbarIcon : int32
{
	Menu = 0,
	Select,
	Translate,
	Rotate,
	Scale,
	WorldSpace,
	LocalSpace,
	TranslateSnap,
	RotateSnap,
	ScaleSnap,
	Camera,
	Setting,
	Count
};

const wchar_t* GetToolbarIconFileName(EToolbarIcon Icon)
{
	switch (Icon)
	{
	case EToolbarIcon::Menu:      return L"Menu.png";
	case EToolbarIcon::Select:    return L"Select.png";
	case EToolbarIcon::Translate: return L"Translate.png";
	case EToolbarIcon::Rotate:    return L"Rotate.png";
	case EToolbarIcon::Scale:     return L"Scale.png";
	case EToolbarIcon::WorldSpace:return L"WorldSpace.png";
	case EToolbarIcon::LocalSpace:return L"LocalSpace.png";
	case EToolbarIcon::TranslateSnap: return L"Translate_Snap.png";
	case EToolbarIcon::RotateSnap:    return L"Rotate_Snap.png";
	case EToolbarIcon::ScaleSnap:     return L"Scale_Snap.png";
	case EToolbarIcon::Camera:    return L"Camera.png";
	case EToolbarIcon::Setting:   return L"Show_Flag.png";
	default:                      return L"";
	}
}

ID3D11ShaderResourceView** GetToolbarIconTable()
{
	static ID3D11ShaderResourceView* ToolbarIcons[static_cast<int32>(EToolbarIcon::Count)] = {};
	return ToolbarIcons;
}

static bool bToolbarIconsLoaded = false;

void ReleaseToolbarIcons()
{
	if (!bToolbarIconsLoaded) return;

	ID3D11ShaderResourceView** ToolbarIcons = GetToolbarIconTable();
	for (int32 i = 0; i < static_cast<int32>(EToolbarIcon::Count); ++i)
	{
		if (ToolbarIcons[i])
		{
			ToolbarIcons[i]->Release();
			ToolbarIcons[i] = nullptr;
		}
	}
	bToolbarIconsLoaded = false;
}

void EnsureToolbarIconsLoaded(FRenderer* RendererPtr)
{
	if (bToolbarIconsLoaded || !RendererPtr)
	{
		return;
	}

	ID3D11Device* Device = RendererPtr->GetFD3DDevice().GetDevice();
	ID3D11ShaderResourceView** ToolbarIcons = GetToolbarIconTable();
	const std::wstring IconDir = FPaths::Combine(FPaths::RootDir(), L"Asset/Editor/ToolIcons/");
	for (int32 i = 0; i < static_cast<int32>(EToolbarIcon::Count); ++i)
	{
		const std::wstring FilePath = IconDir + GetToolbarIconFileName(static_cast<EToolbarIcon>(i));
		DirectX::CreateWICTextureFromFile(Device, FilePath.c_str(), nullptr, &ToolbarIcons[i]);
	}

	bToolbarIconsLoaded = true;
}

ImVec2 GetToolbarIconRenderSize(EToolbarIcon Icon, float FallbackSize, float MaxIconSize)
{
	ID3D11ShaderResourceView* IconSRV = GetToolbarIconTable()[static_cast<int32>(Icon)];
	if (!IconSRV)
	{
		return ImVec2(FallbackSize, FallbackSize);
	}

	ID3D11Resource* Resource = nullptr;
	IconSRV->GetResource(&Resource);
	if (!Resource)
	{
		return ImVec2(FallbackSize, FallbackSize);
	}

	ImVec2 IconSize(FallbackSize, FallbackSize);
	D3D11_RESOURCE_DIMENSION Dimension = D3D11_RESOURCE_DIMENSION_UNKNOWN;
	Resource->GetType(&Dimension);
	if (Dimension == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
	{
		ID3D11Texture2D* Texture2D = static_cast<ID3D11Texture2D*>(Resource);
		D3D11_TEXTURE2D_DESC Desc{};
		Texture2D->GetDesc(&Desc);
		IconSize = ImVec2(static_cast<float>(Desc.Width), static_cast<float>(Desc.Height));
	}
	Resource->Release();

	if (IconSize.x > MaxIconSize || IconSize.y > MaxIconSize)
	{
		const float Scale = (IconSize.x > IconSize.y) ? (MaxIconSize / IconSize.x) : (MaxIconSize / IconSize.y);
		IconSize.x *= Scale;
		IconSize.y *= Scale;
	}

	return IconSize;
}

bool DrawToolbarIconButton(const char* Id, EToolbarIcon Icon, const char* FallbackLabel, float FallbackSize, float MaxIconSize)
{
	ID3D11ShaderResourceView* IconSRV = GetToolbarIconTable()[static_cast<int32>(Icon)];
	if (!IconSRV)
	{
		return ImGui::Button(FallbackLabel);
	}

	const ImVec2 IconSize = GetToolbarIconRenderSize(Icon, FallbackSize, MaxIconSize);
	return ImGui::ImageButton(Id, reinterpret_cast<ImTextureID>(IconSRV), IconSize);
}
}

// ─── 레이아웃별 슬롯 수 ─────────────────────────────────────

int32 FLevelViewportLayout::GetSlotCount(EViewportLayout Layout)
{
	switch (Layout)
	{
	case EViewportLayout::OnePane:          return 1;
	case EViewportLayout::TwoPanesHoriz:
	case EViewportLayout::TwoPanesVert:     return 2;
	case EViewportLayout::ThreePanesLeft:
	case EViewportLayout::ThreePanesRight:
	case EViewportLayout::ThreePanesTop:
	case EViewportLayout::ThreePanesBottom: return 3;
	default:                                return 4;
	}
}

// ─── 아이콘 파일명 매핑 ──────────────────────────────────────

static const wchar_t* GetLayoutIconFileName(EViewportLayout Layout)
{
	switch (Layout)
	{
	case EViewportLayout::OnePane:          return L"ViewportLayout_OnePane.png";
	case EViewportLayout::TwoPanesHoriz:   return L"ViewportLayout_TwoPanesHoriz.png";
	case EViewportLayout::TwoPanesVert:    return L"ViewportLayout_TwoPanesVert.png";
	case EViewportLayout::ThreePanesLeft:  return L"ViewportLayout_ThreePanesLeft.png";
	case EViewportLayout::ThreePanesRight: return L"ViewportLayout_ThreePanesRight.png";
	case EViewportLayout::ThreePanesTop:   return L"ViewportLayout_ThreePanesTop.png";
	case EViewportLayout::ThreePanesBottom:return L"ViewportLayout_ThreePanesBottom.png";
	case EViewportLayout::FourPanes2x2:    return L"ViewportLayout_FourPanes2x2.png";
	case EViewportLayout::FourPanesLeft:   return L"ViewportLayout_FourPanesLeft.png";
	case EViewportLayout::FourPanesRight:  return L"ViewportLayout_FourPanesRight.png";
	case EViewportLayout::FourPanesTop:    return L"ViewportLayout_FourPanesTop.png";
	case EViewportLayout::FourPanesBottom: return L"ViewportLayout_FourPanesBottom.png";
	default:                               return L"";
	}
}

// ─── 아이콘 로드/해제 ────────────────────────────────────────

void FLevelViewportLayout::LoadLayoutIcons(ID3D11Device* Device)
{
	if (!Device) return;

	std::wstring IconDir = FPaths::Combine(FPaths::RootDir(), L"Asset/Editor/Icons/");

	for (int32 i = 0; i < static_cast<int32>(EViewportLayout::MAX); ++i)
	{
		std::wstring Path = IconDir + GetLayoutIconFileName(static_cast<EViewportLayout>(i));
		DirectX::CreateWICTextureFromFile(
			Device, Path.c_str(),
			nullptr, &LayoutIcons[i]);
	}
}

void FLevelViewportLayout::ReleaseLayoutIcons()
{
	for (int32 i = 0; i < static_cast<int32>(EViewportLayout::MAX); ++i)
	{
		if (LayoutIcons[i])
		{
			LayoutIcons[i]->Release();
			LayoutIcons[i] = nullptr;
		}
	}
}

// ─── Initialize / Release ────────────────────────────────────

void FLevelViewportLayout::Initialize(UEditorEngine* InEditor, FWindowsWindow* InWindow, FRenderer& InRenderer,
	FSelectionManager* InSelectionManager)
{
	Editor = InEditor;
	Window = InWindow;
	RendererPtr = &InRenderer;
	SelectionManager = InSelectionManager;

	// 아이콘 로드
	LoadLayoutIcons(InRenderer.GetFD3DDevice().GetDevice());

	// Play/Stop 툴바 초기화
	PlayToolbar.Initialize(InEditor, InRenderer.GetFD3DDevice().GetDevice());

	// LevelViewportClient 생성 (단일 뷰포트)
	auto* LevelVC = new FLevelEditorViewportClient();
	LevelVC->SetOverlayStatSystem(&Editor->GetOverlayStatSystem());
	LevelVC->SetSettings(&FEditorSettings::Get());
	LevelVC->Initialize(Window);
	LevelVC->SetViewportSize(Window->GetWidth(), Window->GetHeight());
	LevelVC->SetGizmo(SelectionManager->GetGizmo());
	LevelVC->SetSelectionManager(SelectionManager);

	auto* VP = new FViewport();
	VP->Initialize(InRenderer.GetFD3DDevice().GetDevice(),
		static_cast<uint32>(Window->GetWidth()),
		static_cast<uint32>(Window->GetHeight()));
	VP->SetClient(LevelVC);
	LevelVC->SetViewport(VP);

	LevelVC->CreateCamera();
	LevelVC->ResetCamera();

	AllViewportClients.push_back(LevelVC);
	LevelViewportClients.push_back(LevelVC);
	SetActiveViewport(LevelVC);

	ViewportWindows[0] = new SWindow();
	LevelVC->SetLayoutWindow(ViewportWindows[0]);
	ActiveSlotCount = 1;
	CurrentLayout = EViewportLayout::OnePane;
}

void FLevelViewportLayout::Release()
{
	SSplitter::DestroyTree(RootSplitter);
	RootSplitter = nullptr;
	DraggingSplitter = nullptr;

	for (int32 i = 0; i < MaxViewportSlots; ++i)
	{
		delete ViewportWindows[i];
		ViewportWindows[i] = nullptr;
	}

	ActiveViewportClient = nullptr;
	for (FEditorViewportClient* VC : AllViewportClients)
	{
		if (FViewport* VP = VC->GetViewport())
		{
			VP->Release();
			delete VP;
		}
		delete VC;
	}
	AllViewportClients.clear();
	LevelViewportClients.clear();

	ReleaseLayoutIcons();
	ReleaseToolbarIcons();
	PlayToolbar.Release();
}

// ─── 활성 뷰포트 ────────────────────────────────────────────

void FLevelViewportLayout::SetActiveViewport(FLevelEditorViewportClient* InClient)
{
	if (ActiveViewportClient)
	{
		ActiveViewportClient->SetActive(false);
	}
	ActiveViewportClient = InClient;
	if (ActiveViewportClient)
	{
		ActiveViewportClient->SetActive(true);
		UWorld* World = Editor->GetWorld();
		if (World && ActiveViewportClient->GetCamera())
		{
			World->SetActiveCamera(ActiveViewportClient->GetCamera());
		}
	}
}

void FLevelViewportLayout::ResetViewport(UWorld* InWorld)
{
	for (FLevelEditorViewportClient* VC : LevelViewportClients)
	{
		VC->CreateCamera();
		VC->ResetCamera();

		// 카메라 재생성 후 현재 뷰포트 크기로 AspectRatio 동기화
		if (FViewport* VP = VC->GetViewport())
		{
			UCameraComponent* Cam = VC->GetCamera();
			if (Cam && VP->GetWidth() > 0 && VP->GetHeight() > 0)
			{
				Cam->OnResize(static_cast<int32>(VP->GetWidth()), static_cast<int32>(VP->GetHeight()));
			}
		}

		// 기존 뷰포트 타입(Ortho 방향 등)을 새 카메라에 재적용
		VC->SetViewportType(VC->GetRenderOptions().ViewportType);
	}
	if (ActiveViewportClient && InWorld)
		InWorld->SetActiveCamera(ActiveViewportClient->GetCamera());
}

void FLevelViewportLayout::DestroyAllCameras()
{
	for (FEditorViewportClient* VC : AllViewportClients)
	{
		VC->DestroyCamera();
	}
}

void FLevelViewportLayout::DisableWorldAxisForPIE()
{
	if (bHasSavedWorldAxisVisibility)
	{
		for (int32 i = 0; i < ActiveSlotCount && i < static_cast<int32>(LevelViewportClients.size()); ++i)
		{
			LevelViewportClients[i]->GetRenderOptions().ShowFlags.bGrid = false;
			LevelViewportClients[i]->GetRenderOptions().ShowFlags.bWorldAxis = false;
		}
		return;
	}

	for (int32 i = 0; i < MaxViewportSlots; ++i)
	{
		SavedGridVisibility[i] = false;
		SavedWorldAxisVisibility[i] = false;
	}

	for (int32 i = 0; i < ActiveSlotCount && i < static_cast<int32>(LevelViewportClients.size()); ++i)
	{
		FViewportRenderOptions& Opts = LevelViewportClients[i]->GetRenderOptions();
		SavedGridVisibility[i] = Opts.ShowFlags.bGrid;
		SavedWorldAxisVisibility[i] = Opts.ShowFlags.bWorldAxis;
		Opts.ShowFlags.bGrid = false;
		Opts.ShowFlags.bWorldAxis = false;
	}

	bHasSavedWorldAxisVisibility = true;
}

void FLevelViewportLayout::RestoreWorldAxisAfterPIE()
{
	if (!bHasSavedWorldAxisVisibility)
	{
		return;
	}

	for (int32 i = 0; i < ActiveSlotCount && i < static_cast<int32>(LevelViewportClients.size()); ++i)
	{
		LevelViewportClients[i]->GetRenderOptions().ShowFlags.bGrid = SavedGridVisibility[i];
		LevelViewportClients[i]->GetRenderOptions().ShowFlags.bWorldAxis = SavedWorldAxisVisibility[i];
	}

	bHasSavedWorldAxisVisibility = false;
}

// ─── 뷰포트 슬롯 관리 ───────────────────────────────────────

void FLevelViewportLayout::EnsureViewportSlots(int32 RequiredCount)
{
	// 현재 슬롯보다 더 필요하면 추가 생성
	while (static_cast<int32>(LevelViewportClients.size()) < RequiredCount)
	{
		int32 Idx = static_cast<int32>(LevelViewportClients.size());

		auto* LevelVC = new FLevelEditorViewportClient();
		LevelVC->SetOverlayStatSystem(&Editor->GetOverlayStatSystem());
		LevelVC->SetSettings(&FEditorSettings::Get());
		LevelVC->Initialize(Window);
		LevelVC->SetViewportSize(Window->GetWidth(), Window->GetHeight());
		LevelVC->SetGizmo(SelectionManager->GetGizmo());
		LevelVC->SetSelectionManager(SelectionManager);

		auto* VP = new FViewport();
		VP->Initialize(RendererPtr->GetFD3DDevice().GetDevice(),
			static_cast<uint32>(Window->GetWidth()),
			static_cast<uint32>(Window->GetHeight()));
		VP->SetClient(LevelVC);
		LevelVC->SetViewport(VP);

		LevelVC->CreateCamera();
		LevelVC->ResetCamera();

		AllViewportClients.push_back(LevelVC);
		LevelViewportClients.push_back(LevelVC);

		ViewportWindows[Idx] = new SWindow();
		LevelVC->SetLayoutWindow(ViewportWindows[Idx]);
	}
}

void FLevelViewportLayout::ShrinkViewportSlots(int32 RequiredCount)
{
	while (static_cast<int32>(LevelViewportClients.size()) > RequiredCount)
	{
		FLevelEditorViewportClient* VC = LevelViewportClients.back();
		int32 Idx = static_cast<int32>(LevelViewportClients.size()) - 1;
		LevelViewportClients.pop_back();

		for (auto It = AllViewportClients.begin(); It != AllViewportClients.end(); ++It)
		{
			if (*It == VC) { AllViewportClients.erase(It); break; }
		}

		if (ActiveViewportClient == VC)
			SetActiveViewport(LevelViewportClients[0]);

		if (FViewport* VP = VC->GetViewport())
		{
			VP->Release();
			delete VP;
		}
		VC->DestroyCamera();
		delete VC;

		delete ViewportWindows[Idx];
		ViewportWindows[Idx] = nullptr;
	}
}

int32 FLevelViewportLayout::GetOnePaneSlotIndex() const
{
	if (CurrentLayout == EViewportLayout::OnePane && bIsTemporaryOnePane)
	{
		const int32 LastSlotIndex = static_cast<int32>(LevelViewportClients.size()) - 1;
		const int32 MaxSlotIndex = (std::max)(0, LastSlotIndex);
		return (std::max)(0, (std::min)(TemporaryOnePaneSourceSlot, MaxSlotIndex));
	}

	return 0;
}

// ─── SSplitter 트리 빌드 ─────────────────────────────────────

SSplitter* FLevelViewportLayout::BuildSplitterTree(EViewportLayout Layout)
{
	SWindow** W = ViewportWindows;

	switch (Layout)
	{
	case EViewportLayout::OnePane:
		return nullptr; // 트리 불필요

	case EViewportLayout::TwoPanesHoriz:
	{
		// H → [0] | [1]
		auto* Root = new SSplitterH();
		Root->SetSideLT(W[0]);
		Root->SetSideRB(W[1]);
		return Root;
	}
	case EViewportLayout::TwoPanesVert:
	{
		// V → [0] / [1]
		auto* Root = new SSplitterV();
		Root->SetSideLT(W[0]);
		Root->SetSideRB(W[1]);
		return Root;
	}
	case EViewportLayout::ThreePanesLeft:
	{
		// H → [0] | V([1]/[2])
		auto* RightV = new SSplitterV();
		RightV->SetSideLT(W[1]);
		RightV->SetSideRB(W[2]);
		auto* Root = new SSplitterH();
		Root->SetSideLT(W[0]);
		Root->SetSideRB(RightV);
		return Root;
	}
	case EViewportLayout::ThreePanesRight:
	{
		// H → V([0]/[1]) | [2]
		auto* LeftV = new SSplitterV();
		LeftV->SetSideLT(W[0]);
		LeftV->SetSideRB(W[1]);
		auto* Root = new SSplitterH();
		Root->SetSideLT(LeftV);
		Root->SetSideRB(W[2]);
		return Root;
	}
	case EViewportLayout::ThreePanesTop:
	{
		// V → [0] / H([1]|[2])
		auto* BottomH = new SSplitterH();
		BottomH->SetSideLT(W[1]);
		BottomH->SetSideRB(W[2]);
		auto* Root = new SSplitterV();
		Root->SetSideLT(W[0]);
		Root->SetSideRB(BottomH);
		return Root;
	}
	case EViewportLayout::ThreePanesBottom:
	{
		// V → H([0]|[1]) / [2]
		auto* TopH = new SSplitterH();
		TopH->SetSideLT(W[0]);
		TopH->SetSideRB(W[1]);
		auto* Root = new SSplitterV();
		Root->SetSideLT(TopH);
		Root->SetSideRB(W[2]);
		return Root;
	}
	case EViewportLayout::FourPanes2x2:
	{
		// H → V([0]/[2]) | V([1]/[3])
		auto* LeftV = new SSplitterV();
		LeftV->SetSideLT(W[0]);
		LeftV->SetSideRB(W[2]);
		auto* RightV = new SSplitterV();
		RightV->SetSideLT(W[1]);
		RightV->SetSideRB(W[3]);
		auto* Root = new SSplitterH();
		Root->SetSideLT(LeftV);
		Root->SetSideRB(RightV);
		return Root;
	}
	case EViewportLayout::FourPanesLeft:
	{
		// H → [0] | V([1] / V([2]/[3]))
		auto* InnerV = new SSplitterV();
		InnerV->SetSideLT(W[2]);
		InnerV->SetSideRB(W[3]);
		auto* RightV = new SSplitterV();
		RightV->SetRatio(0.333f);
		RightV->SetSideLT(W[1]);
		RightV->SetSideRB(InnerV);
		auto* Root = new SSplitterH();
		Root->SetSideLT(W[0]);
		Root->SetSideRB(RightV);
		return Root;
	}
	case EViewportLayout::FourPanesRight:
	{
		// H → V([0] / V([1]/[2])) | [3]
		auto* InnerV = new SSplitterV();
		InnerV->SetSideLT(W[1]);
		InnerV->SetSideRB(W[2]);
		auto* LeftV = new SSplitterV();
		LeftV->SetRatio(0.333f);
		LeftV->SetSideLT(W[0]);
		LeftV->SetSideRB(InnerV);
		auto* Root = new SSplitterH();
		Root->SetSideLT(LeftV);
		Root->SetSideRB(W[3]);
		return Root;
	}
	case EViewportLayout::FourPanesTop:
	{
		// V → [0] / H([1] | H([2]|[3]))
		auto* InnerH = new SSplitterH();
		InnerH->SetSideLT(W[2]);
		InnerH->SetSideRB(W[3]);
		auto* BottomH = new SSplitterH();
		BottomH->SetRatio(0.333f);
		BottomH->SetSideLT(W[1]);
		BottomH->SetSideRB(InnerH);
		auto* Root = new SSplitterV();
		Root->SetSideLT(W[0]);
		Root->SetSideRB(BottomH);
		return Root;
	}
	case EViewportLayout::FourPanesBottom:
	{
		// V → H([0] | H([1]|[2])) / [3]
		auto* InnerH = new SSplitterH();
		InnerH->SetSideLT(W[1]);
		InnerH->SetSideRB(W[2]);
		auto* TopH = new SSplitterH();
		TopH->SetRatio(0.333f);
		TopH->SetSideLT(W[0]);
		TopH->SetSideRB(InnerH);
		auto* Root = new SSplitterV();
		Root->SetSideLT(TopH);
		Root->SetSideRB(W[3]);
		return Root;
	}
	default:
		return nullptr;
	}
}

bool FLevelViewportLayout::SubtreeContainsWindow(SWindow* Node, SWindow* TargetWindow) const
{
	if (!Node || !TargetWindow)
	{
		return false;
	}

	if (Node == TargetWindow)
	{
		return true;
	}

	SSplitter* Splitter = SSplitter::AsSplitter(Node);
	return Splitter &&
		(SubtreeContainsWindow(Splitter->GetSideLT(), TargetWindow) ||
		 SubtreeContainsWindow(Splitter->GetSideRB(), TargetWindow));
}

bool FLevelViewportLayout::ConfigureCollapseToSlot(SSplitter* Node, SWindow* TargetWindow, bool bAnimate)
{
	if (!Node || !TargetWindow)
	{
		return false;
	}

	const bool bTargetInLT = SubtreeContainsWindow(Node->GetSideLT(), TargetWindow);
	const bool bTargetInRB = SubtreeContainsWindow(Node->GetSideRB(), TargetWindow);
	if (!bTargetInLT && !bTargetInRB)
	{
		return false;
	}

	Node->SetTargetRatio(bTargetInLT ? 1.0f : 0.0f, bAnimate);
	if (SSplitter* Child = SSplitter::AsSplitter(bTargetInLT ? Node->GetSideLT() : Node->GetSideRB()))
	{
		ConfigureCollapseToSlot(Child, TargetWindow, bAnimate);
	}

	return true;
}

void FLevelViewportLayout::BeginSplitToOnePaneTransition(int32 SlotIndex)
{
	FinishLayoutTransition(true);

	if (!RootSplitter || SlotIndex < 0 || SlotIndex >= static_cast<int32>(LevelViewportClients.size()) || SlotIndex >= MaxViewportSlots)
	{
		TemporaryOnePaneSourceSlot = SlotIndex;
		bRequestPreserveSplitOnOnePane = true;
		bSuppressLayoutTransitionAnimation = true;
		SetLayout(EViewportLayout::OnePane);
		bSuppressLayoutTransitionAnimation = false;
		return;
	}

	LastSplitLayout = CurrentLayout;
	TemporaryOnePaneSourceSlot = SlotIndex;
	TransitionSourceSlot = SlotIndex;
	TransitionTargetLayout = EViewportLayout::OnePane;
	TransitionRestoreRatioCount = 0;

	TArray<SSplitter*> Splitters;
	SSplitter::CollectSplitters(RootSplitter, Splitters);
	TransitionRestoreRatioCount = (std::min)(static_cast<int32>(Splitters.size()), 3);
	for (int32 i = 0; i < TransitionRestoreRatioCount; ++i)
	{
		TransitionRestoreRatios[i] = Splitters[i]->GetRatio();
	}

	LayoutTransition = EViewportLayoutTransition::SplitToOnePane;
	DraggingSplitter = nullptr;
	if (!ConfigureCollapseToSlot(RootSplitter, ViewportWindows[SlotIndex], true))
	{
		FinishLayoutTransition(true);
	}
}

void FLevelViewportLayout::BeginOnePaneToSplitTransition(EViewportLayout TargetLayout)
{
	FinishLayoutTransition(true);
	if (TargetLayout == EViewportLayout::OnePane)
	{
		return;
	}

	const int32 SourceSlot = GetOnePaneSlotIndex();
	TransitionSourceSlot = SourceSlot;
	TransitionTargetLayout = TargetLayout;

	bSuppressLayoutTransitionAnimation = true;
	SetLayout(TargetLayout);
	bSuppressLayoutTransitionAnimation = false;

	if (!RootSplitter || SourceSlot < 0 || SourceSlot >= MaxViewportSlots || !ViewportWindows[SourceSlot])
	{
		return;
	}

	TArray<SSplitter*> Splitters;
	SSplitter::CollectSplitters(RootSplitter, Splitters);
	const int32 RestoreCount = (std::min)(static_cast<int32>(Splitters.size()), 3);
	float TargetRatios[3] = { 0.5f, 0.5f, 0.5f };
	for (int32 i = 0; i < RestoreCount; ++i)
	{
		TargetRatios[i] = (i < TransitionRestoreRatioCount) ? TransitionRestoreRatios[i] : Splitters[i]->GetRatio();
	}

	ConfigureCollapseToSlot(RootSplitter, ViewportWindows[SourceSlot], false);
	for (int32 i = 0; i < RestoreCount; ++i)
	{
		Splitters[i]->SetTargetRatio(TargetRatios[i], true);
	}

	LayoutTransition = EViewportLayoutTransition::OnePaneToSplit;
	DraggingSplitter = nullptr;
}

void FLevelViewportLayout::FinishLayoutTransition(bool bSnapToEnd)
{
	if (LayoutTransition == EViewportLayoutTransition::None)
	{
		return;
	}

	const EViewportLayoutTransition FinishedTransition = LayoutTransition;
	const int32 SourceSlot = TransitionSourceSlot;
	LayoutTransition = EViewportLayoutTransition::None;
	DraggingSplitter = nullptr;

	if (RootSplitter)
	{
		TArray<SSplitter*> Splitters;
		SSplitter::CollectSplitters(RootSplitter, Splitters);
		for (SSplitter* Splitter : Splitters)
		{
			if (Splitter)
			{
				Splitter->StopAnimation(bSnapToEnd);
			}
		}
	}

	if (FinishedTransition == EViewportLayoutTransition::SplitToOnePane)
	{
		TemporaryOnePaneSourceSlot = SourceSlot;
		bRequestPreserveSplitOnOnePane = true;
		bSuppressLayoutTransitionAnimation = true;
		SetLayout(EViewportLayout::OnePane);
		bSuppressLayoutTransitionAnimation = false;
	}
}

bool FLevelViewportLayout::UpdateLayoutTransition(float DeltaTime)
{
	if (LayoutTransition == EViewportLayoutTransition::None || !RootSplitter)
	{
		return false;
	}

	bool bAnyAnimating = false;
	TArray<SSplitter*> Splitters;
	SSplitter::CollectSplitters(RootSplitter, Splitters);
	for (SSplitter* Splitter : Splitters)
	{
		if (Splitter && Splitter->UpdateAnimation(DeltaTime))
		{
			bAnyAnimating = true;
		}
	}

	if (!bAnyAnimating)
	{
		FinishLayoutTransition(false);
		return false;
	}

	return true;
}

// ─── 레이아웃 전환 ──────────────────────────────────────────

void FLevelViewportLayout::SetLayout(EViewportLayout NewLayout)
{
	if (NewLayout == CurrentLayout) return;

	if (!bSuppressLayoutTransitionAnimation)
	{
		if (LayoutTransition != EViewportLayoutTransition::None)
		{
			FinishLayoutTransition(true);
			if (NewLayout == CurrentLayout)
			{
				return;
			}
		}

		if (CurrentLayout != EViewportLayout::OnePane && NewLayout == EViewportLayout::OnePane && bRequestPreserveSplitOnOnePane)
		{
			BeginSplitToOnePaneTransition(TemporaryOnePaneSourceSlot);
			bRequestPreserveSplitOnOnePane = false;
			return;
		}

		if (CurrentLayout == EViewportLayout::OnePane && NewLayout != EViewportLayout::OnePane)
		{
			BeginOnePaneToSplitTransition(NewLayout);
			return;
		}
	}

	const bool bWasTemporaryOnePane = bIsTemporaryOnePane;
	if (CurrentLayout != EViewportLayout::OnePane)
	{
		LastSplitLayout = CurrentLayout;
	}

	// 기존 트리 해제
	SSplitter::DestroyTree(RootSplitter);
	RootSplitter = nullptr;
	DraggingSplitter = nullptr;

	int32 RequiredSlots = GetSlotCount(NewLayout);
	int32 OldSlotCount = static_cast<int32>(LevelViewportClients.size());
	const bool bUseTemporaryOnePane = (NewLayout == EViewportLayout::OnePane && bRequestPreserveSplitOnOnePane);

	if (bUseTemporaryOnePane)
	{
		const int32 MaxSlotIndex = (std::max)(0, OldSlotCount - 1);
		TemporaryOnePaneSourceSlot = (std::max)(0, (std::min)(TemporaryOnePaneSourceSlot, MaxSlotIndex));
		bIsTemporaryOnePane = true;
	}

	// 슬롯 수 조정
	if (!bUseTemporaryOnePane)
	{
		if (RequiredSlots > OldSlotCount)
			EnsureViewportSlots(RequiredSlots);
		else if (RequiredSlots < OldSlotCount)
			ShrinkViewportSlots(RequiredSlots);
	}
	else
	{
		RequiredSlots = OldSlotCount;
	}

	// 분할 전환 시 새로 추가된 슬롯에 Top, Front, Right 순으로 기본 설정
	if (NewLayout != EViewportLayout::OnePane)
	{
		constexpr ELevelViewportType DefaultTypes[] = {
			ELevelViewportType::Top,
			ELevelViewportType::Front,
			ELevelViewportType::Right
		};
		// 기존 슬롯은 유지하고 새로 생긴 슬롯에만 적용
		int32 StartIdx = OldSlotCount;
		for (int32 i = StartIdx; i < RequiredSlots && (i - 1) < 3; ++i)
		{
			LevelViewportClients[i]->SetViewportType(DefaultTypes[i - 1]);
		}
	}

	// 새 트리 빌드
	RootSplitter = BuildSplitterTree(NewLayout);
	ActiveSlotCount = (NewLayout == EViewportLayout::OnePane) ? 1 : RequiredSlots;
	CurrentLayout = NewLayout;

	if (NewLayout != EViewportLayout::OnePane)
	{
		bIsTemporaryOnePane = false;
	}
	else if (!bUseTemporaryOnePane && !bWasTemporaryOnePane)
	{
		TemporaryOnePaneSourceSlot = 0;
	}

	bRequestPreserveSplitOnOnePane = false;
}

void FLevelViewportLayout::ToggleViewportSplit()
{
	if (CurrentLayout == EViewportLayout::OnePane)
	{
		SetLayout(LastSplitLayout == EViewportLayout::OnePane ? EViewportLayout::FourPanes2x2 : LastSplitLayout);
	}
	else
	{
		if (ActiveViewportClient)
		{
			for (int32 i = 0; i < static_cast<int32>(LevelViewportClients.size()); ++i)
			{
				if (LevelViewportClients[i] == ActiveViewportClient)
				{
					TemporaryOnePaneSourceSlot = i;
					break;
				}
			}
		}
		bRequestPreserveSplitOnOnePane = true;
		SetLayout(EViewportLayout::OnePane);
	}
}

void FLevelViewportLayout::RenderSharedGizmoToolbar(float ToolbarLeft, float ToolbarTop)
{
	if (!Editor)
	{
		return;
	}

	UGizmoComponent* Gizmo = Editor->GetGizmo();
	if (!Gizmo)
	{
		return;
	}

	EnsureToolbarIconsLoaded(RendererPtr);

	constexpr float ToolbarHeight = 28.0f;
	constexpr float IconSize = 16.0f;
	constexpr float ButtonPadding = (ToolbarHeight - IconSize) * 0.5f;
	constexpr float ButtonSpacing = 4.0f;
	constexpr float PlayStopButtonWidth = 24.0f;
	constexpr float GroupSpacing = 12.0f;
	constexpr float ToolbarFallbackIconSize = 14.0f;
	constexpr float ToolbarMaxIconSize = 16.0f;

	ImGui::SetCursorScreenPos(ImVec2(
		ToolbarLeft + ButtonPadding + (PlayStopButtonWidth * 2.0f) + ButtonSpacing + GroupSpacing,
		ToolbarTop + ButtonPadding));

	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.15f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 1.0f, 1.0f, 0.3f));

	auto DrawGizmoIcon = [&](const char* Id, EToolbarIcon Icon, EGizmoMode TargetMode, const char* FallbackLabel) -> bool
	{
		const bool bSelected = (Gizmo->GetMode() == TargetMode);
		if (bSelected)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.8f, 1.0f));
		}
		const bool bClicked = DrawToolbarIconButton(Id, Icon, FallbackLabel, ToolbarFallbackIconSize, ToolbarMaxIconSize);
		if (bSelected)
		{
			ImGui::PopStyleColor();
		}
		return bClicked;
	};

	if (DrawGizmoIcon("##SharedTranslateToolIcon", EToolbarIcon::Translate, EGizmoMode::Translate, "Translate"))
	{
		Gizmo->SetTranslateMode();
	}
	ImGui::SameLine(0.0f, ButtonSpacing);
	if (DrawGizmoIcon("##SharedRotateToolIcon", EToolbarIcon::Rotate, EGizmoMode::Rotate, "Rotate"))
	{
		Gizmo->SetRotateMode();
	}
	ImGui::SameLine(0.0f, ButtonSpacing);
	if (DrawGizmoIcon("##SharedScaleToolIcon", EToolbarIcon::Scale, EGizmoMode::Scale, "Scale"))
	{
		Gizmo->SetScaleMode();
	}

	ImGui::PopStyleColor(3);

	FEditorSettings& Settings = Editor->GetSettings();

	ImGui::SameLine(0.0f, GroupSpacing);
	const bool bWorldCoord = Settings.CoordSystem == EEditorCoordSystem::World;
	if (bWorldCoord)
	{
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.8f, 1.0f));
	}
	if (DrawToolbarIconButton("##SharedCoordSystemIcon",
		bWorldCoord ? EToolbarIcon::WorldSpace : EToolbarIcon::LocalSpace,
		bWorldCoord ? "World" : "Local",
		ToolbarFallbackIconSize,
		ToolbarMaxIconSize))
	{
		Editor->ToggleCoordSystem();
	}
	if (bWorldCoord)
	{
		ImGui::PopStyleColor();
	}

	auto DrawSnapControl = [&](const char* Id, EToolbarIcon Icon, const char* FallbackLabel, bool& bEnabled, float& Value, float MinValue)
	{
		ImGui::SameLine(0.0f, 6.0f);
		ImGui::PushID(Id);
		const bool bWasEnabled = bEnabled;
		if (bWasEnabled)
		{
			ImGui::PushStyleColor(ImGuiCol_Button,			ImVec4(0.3f, 0.5f, 0.8f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered,	ImVec4(0.38f, 0.58f, 0.88f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive,	ImVec4(0.22f, 0.42f, 0.72f, 1.0f));
		}
		if (DrawToolbarIconButton("##SnapToggle", Icon, FallbackLabel, ToolbarFallbackIconSize, ToolbarMaxIconSize))
		{
			bEnabled = !bEnabled;
		}
		if (bWasEnabled)
		{
			ImGui::PopStyleColor(3);
		}
		ImGui::SameLine(0.0f, 2.0f);
		ImGui::SetNextItemWidth(48.0f);
		if (ImGui::InputFloat("##Value", &Value, 0.0f, 0.0f, "%.2f"))
		{
			if (Value < MinValue)
			{
				Value = MinValue;
			}
		}
		ImGui::PopID();
	};

	DrawSnapControl("TranslateSnap", EToolbarIcon::TranslateSnap, "T", Settings.bEnableTranslationSnap, Settings.TranslationSnapSize, 0.001f);
	DrawSnapControl("RotateSnap", EToolbarIcon::RotateSnap, "R", Settings.bEnableRotationSnap, Settings.RotationSnapSize, 0.001f);
	DrawSnapControl("ScaleSnap", EToolbarIcon::ScaleSnap, "S", Settings.bEnableScaleSnap, Settings.ScaleSnapSize, 0.001f);

	Editor->ApplyTransformSettingsToGizmo();
}

// ─── Viewport UI 렌더링 ─────────────────────────────────────

void FLevelViewportLayout::RenderViewportUI(float DeltaTime)
{
	bMouseOverViewport = false;
	UpdateLayoutTransition(DeltaTime);

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_None);

	ImVec2 ContentPos = ImGui::GetCursorScreenPos();
	ImVec2 ContentSize = ImGui::GetContentRegionAvail();

	if (ContentSize.x > 0 && ContentSize.y > 0)
	{
		// 상단에 Play/Stop 툴바 영역 확보 후 나머지를 뷰포트에 할당
		const float ToolbarHeight = PlayToolbar.GetDesiredHeight();
		ImGui::SetCursorScreenPos(ContentPos);
		PlayToolbar.Render(ContentSize.x);
		RenderSharedGizmoToolbar(ContentPos.x, ContentPos.y);
		ImGui::SetCursorScreenPos(ImVec2(ContentPos.x, ContentPos.y + ToolbarHeight));

		// Shader hot reload 실패 배너 (최근 5초, 페이드 아웃)
		{
			const uint64 ErrTime = FShader::GetLastCompileErrorTimeMs();
			if (ErrTime > 0)
			{
				const uint64 NowMs = GetTickCount64();
				const uint64 Elapsed = NowMs - ErrTime;
				constexpr uint64 ShowDurationMs = 5000;
				if (Elapsed < ShowDurationMs)
				{
					const float t = static_cast<float>(Elapsed) / static_cast<float>(ShowDurationMs);
					const float Alpha = 1.0f - t;
					const int BgA = static_cast<int>(220.0f * Alpha);
					const int FgA = static_cast<int>(255.0f * Alpha);

					// 에러 메시지의 첫 줄만 표시
					const std::string& FullMsg = FShader::GetLastCompileError();
					std::string FirstLine = FullMsg;
					size_t NewlinePos = FirstLine.find('\n');
					if (NewlinePos != std::string::npos)
						FirstLine.resize(NewlinePos);
					if (FirstLine.size() > 240)
						FirstLine.resize(240);

					const float BannerHeight = 26.0f;
					const float BannerY = ContentPos.y + ToolbarHeight;
					ImVec2 BannerMin(ContentPos.x, BannerY);
					ImVec2 BannerMax(ContentPos.x + ContentSize.x, BannerY + BannerHeight);

					ImDrawList* DL = ImGui::GetForegroundDrawList();
					DL->AddRectFilled(BannerMin, BannerMax, IM_COL32(180, 40, 40, BgA));
					DL->AddRect(BannerMin, BannerMax, IM_COL32(255, 80, 80, FgA));

					char Buf[320];
					std::snprintf(Buf, sizeof(Buf), "[Shader Compile FAIL] %s", FirstLine.c_str());
					ImVec2 TextPos(BannerMin.x + 8.0f, BannerMin.y + 5.0f);
					DL->AddText(TextPos, IM_COL32(255, 255, 255, FgA), Buf);
				}
			}
		}

		FRect ContentRect = {
			ContentPos.x,
			ContentPos.y + ToolbarHeight,
			ContentSize.x,
			ContentSize.y - ToolbarHeight
		};
		const int32 OnePaneSlotIndex = GetOnePaneSlotIndex();
		auto IsSlotVisibleEnough = [&](int32 SlotIndex) -> bool
		{
			if (SlotIndex < 0 || SlotIndex >= MaxViewportSlots || !ViewportWindows[SlotIndex])
			{
				return false;
			}
			const FRect& R = ViewportWindows[SlotIndex]->GetRect();
			return R.Width > 1.0f && R.Height > 1.0f;
		};

		// SSplitter 레이아웃 계산
		if (RootSplitter)
		{
			RootSplitter->ComputeLayout(ContentRect);
		}
		else if (OnePaneSlotIndex < MaxViewportSlots && ViewportWindows[OnePaneSlotIndex])
		{
			ViewportWindows[OnePaneSlotIndex]->SetRect(ContentRect);
		}

		bool bVisibleSlot[MaxViewportSlots] = {};
		if (CurrentLayout == EViewportLayout::OnePane)
		{
			bVisibleSlot[OnePaneSlotIndex] = true;
		}
		else
		{
			for (int32 i = 0; i < GetSlotCount(CurrentLayout); ++i)
			{
				bVisibleSlot[i] = true;
			}
		}

		for (int32 i = 0; i < MaxViewportSlots; ++i)
		{
			if (!bVisibleSlot[i] && ViewportWindows[i])
			{
				ViewportWindows[i]->SetRect(FRect{ 0.0f, 0.0f, 0.0f, 0.0f });
			}
		}

		// 각 ViewportClient에 Rect 반영 + 이미지 렌더
		for (FLevelEditorViewportClient* VC : LevelViewportClients)
		{
			if (VC)
			{
				VC->UpdateLayoutRect();
			}
		}

		for (int32 i = 0; i < ActiveSlotCount; ++i)
		{
			const int32 SlotIndex = (CurrentLayout == EViewportLayout::OnePane) ? OnePaneSlotIndex : i;
			if (SlotIndex < static_cast<int32>(LevelViewportClients.size()) && IsSlotVisibleEnough(SlotIndex))
			{
				FLevelEditorViewportClient* VC = LevelViewportClients[SlotIndex];
				VC->RenderViewportImage(VC == ActiveViewportClient);
			}
		}

		// 각 뷰포트 패인 상단에 툴바 오버레이 렌더
		for (int32 i = 0; i < ActiveSlotCount; ++i)
		{
			const int32 SlotIndex = (CurrentLayout == EViewportLayout::OnePane) ? OnePaneSlotIndex : i;
			const bool bShowPaneToolbar =
				IsSlotVisibleEnough(SlotIndex) &&
				(LayoutTransition == EViewportLayoutTransition::None || SlotIndex == TransitionSourceSlot);
			if (bShowPaneToolbar)
			{
				RenderPaneToolbar(SlotIndex);
			}
		}

		// 분할 바 렌더 (재귀 수집)
		if (RootSplitter)
		{
			TArray<SSplitter*> AllSplitters;
			SSplitter::CollectSplitters(RootSplitter, AllSplitters);

			ImDrawList* DrawList = ImGui::GetWindowDrawList();
			ImU32 BarColor = IM_COL32(80, 80, 80, 255);

			for (SSplitter* S : AllSplitters)
			{
				const FRect& Bar = S->GetSplitBarRect();
				DrawList->AddRectFilled(
					ImVec2(Bar.X, Bar.Y),
					ImVec2(Bar.X + Bar.Width, Bar.Y + Bar.Height),
					BarColor);
			}
		}

		// 입력 처리
		if (ImGui::IsWindowHovered())
		{
			ImVec2 MousePos = ImGui::GetIO().MousePos;
			FPoint MP = { MousePos.x, MousePos.y };

			// 마우스가 어떤 슬롯 위에 있는지
			for (int32 i = 0; i < ActiveSlotCount; ++i)
			{
				const int32 SlotIndex = (CurrentLayout == EViewportLayout::OnePane) ? OnePaneSlotIndex : i;
				if (IsSlotVisibleEnough(SlotIndex) && ViewportWindows[SlotIndex]->IsHover(MP))
				{
					bMouseOverViewport = true;
					break;
				}
			}

			// 분할 바 드래그
			if (RootSplitter && LayoutTransition == EViewportLayoutTransition::None)
			{
				if (ImGui::IsMouseClicked(0))
				{
					DraggingSplitter = SSplitter::FindSplitterAtBar(RootSplitter, MP);
				}

				if (ImGui::IsMouseReleased(0))
				{
					DraggingSplitter = nullptr;
				}

				if (DraggingSplitter)
				{
					const FRect& DR = DraggingSplitter->GetRect();
					if (DraggingSplitter->GetOrientation() == ESplitOrientation::Horizontal)
					{
						float NewRatio = (MousePos.x - DR.X) / DR.Width;
						DraggingSplitter->SetRatio(Clamp(NewRatio, 0.15f, 0.85f));
						ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
					}
					else
					{
						float NewRatio = (MousePos.y - DR.Y) / DR.Height;
						DraggingSplitter->SetRatio(Clamp(NewRatio, 0.15f, 0.85f));
						ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
					}
				}
				else
				{
					// 호버 커서 변경
					SSplitter* Hovered = SSplitter::FindSplitterAtBar(RootSplitter, MP);
					if (Hovered)
					{
						if (Hovered->GetOrientation() == ESplitOrientation::Horizontal)
							ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
						else
							ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
					}
				}
			}

			// 활성 뷰포트 전환 (분할 바 드래그 중이 아닐 때)
			if (!DraggingSplitter && (ImGui::IsMouseClicked(0) || ImGui::IsMouseClicked(1)))
			{
				for (int32 i = 0; i < ActiveSlotCount; ++i)
				{
					const int32 SlotIndex = (CurrentLayout == EViewportLayout::OnePane) ? OnePaneSlotIndex : i;
					if (SlotIndex < static_cast<int32>(LevelViewportClients.size()) &&
						IsSlotVisibleEnough(SlotIndex) && ViewportWindows[SlotIndex]->IsHover(MP))
					{
						if (LevelViewportClients[SlotIndex] != ActiveViewportClient)
							SetActiveViewport(LevelViewportClients[SlotIndex]);
						break;
					}
				}
			}

			if (!DraggingSplitter)
			{
				HandleViewportContextMenuInput(MP);
			}
		}

		RenderViewportPlaceActorPopup();
	}

	ImGui::End();
	ImGui::PopStyleVar();
}

void FLevelViewportLayout::HandleViewportContextMenuInput(const FPoint& MousePos)
{
	constexpr float RightClickPopupThresholdPx = 20.0f;
	constexpr float RightClickPopupThresholdSq = RightClickPopupThresholdPx * RightClickPopupThresholdPx;
	const int32 OnePaneSlotIndex = GetOnePaneSlotIndex();
	auto IsSlotVisibleEnough = [&](int32 SlotIndex) -> bool
	{
		if (SlotIndex < 0 || SlotIndex >= MaxViewportSlots || !ViewportWindows[SlotIndex])
		{
			return false;
		}
		const FRect& R = ViewportWindows[SlotIndex]->GetRect();
		return R.Width > 1.0f && R.Height > 1.0f;
	};

	int32 HoveredSlot = -1;
	for (int32 i = 0; i < ActiveSlotCount; ++i)
	{
		const int32 SlotIndex = (CurrentLayout == EViewportLayout::OnePane) ? OnePaneSlotIndex : i;
		if (IsSlotVisibleEnough(SlotIndex) && ViewportWindows[SlotIndex]->IsHover(MousePos))
		{
			HoveredSlot = SlotIndex;
			break;
		}
	}

	if (ImGui::IsMouseClicked(1) && HoveredSlot >= 0)
	{
		ContextMenuState.bTrackingRightClick[HoveredSlot] = true;
		ContextMenuState.RightClickTravelSq[HoveredSlot] = 0.0f;
		ContextMenuState.RightClickPressPos[HoveredSlot] = MousePos;
	}

	for (int32 i = 0; i < ActiveSlotCount; ++i)
	{
		const int32 SlotIndex = (CurrentLayout == EViewportLayout::OnePane) ? OnePaneSlotIndex : i;
		if (SlotIndex < 0 || SlotIndex >= MaxViewportSlots || !ContextMenuState.bTrackingRightClick[SlotIndex])
		{
			continue;
		}

		const float DX = MousePos.X - ContextMenuState.RightClickPressPos[SlotIndex].X;
		const float DY = MousePos.Y - ContextMenuState.RightClickPressPos[SlotIndex].Y;
		const float TravelSq = DX * DX + DY * DY;
		if (TravelSq > ContextMenuState.RightClickTravelSq[SlotIndex])
		{
			ContextMenuState.RightClickTravelSq[SlotIndex] = TravelSq;
		}
	}

	if (!ImGui::IsMouseReleased(1))
	{
		return;
	}

	for (int32 i = 0; i < ActiveSlotCount; ++i)
	{
		const int32 SlotIndex = (CurrentLayout == EViewportLayout::OnePane) ? OnePaneSlotIndex : i;
		if (SlotIndex < 0 || SlotIndex >= MaxViewportSlots || !ContextMenuState.bTrackingRightClick[SlotIndex])
		{
			continue;
		}

		const bool bReleasedOverSameSlot = IsSlotVisibleEnough(SlotIndex) && ViewportWindows[SlotIndex]->IsHover(MousePos);
		const bool bClickCandidate =
			bReleasedOverSameSlot &&
			ContextMenuState.RightClickTravelSq[SlotIndex] <= RightClickPopupThresholdSq &&
			!InputSystem::Get().GetRightDragging() &&
			!InputSystem::Get().GetRightDragEnd();
		const ImGuiIO& IO = ImGui::GetIO();
		const bool bNoModifiers = !IO.KeyCtrl && !IO.KeyShift && !IO.KeyAlt && !IO.KeySuper;

		if (bClickCandidate && bNoModifiers)
		{
			ContextMenuState.PendingPopupSlot = SlotIndex;
			ContextMenuState.PendingSpawnSlot = SlotIndex;
			ContextMenuState.PendingPopupPos = MousePos;
			ContextMenuState.PendingSpawnPos = ContextMenuState.RightClickPressPos[SlotIndex];
		}

		ContextMenuState.bTrackingRightClick[SlotIndex] = false;
		ContextMenuState.RightClickTravelSq[SlotIndex] = 0.0f;
	}
}

void FLevelViewportLayout::RenderViewportPlaceActorPopup()
{
	constexpr const char* PopupId = "##ViewportPlaceActorPopup";

	if (ContextMenuState.PendingPopupSlot >= 0)
	{
		if (ContextMenuState.PendingPopupSlot < static_cast<int32>(LevelViewportClients.size()))
		{
			SetActiveViewport(LevelViewportClients[ContextMenuState.PendingPopupSlot]);
		}

		ImGui::SetNextWindowPos(ImVec2(ContextMenuState.PendingPopupPos.X, ContextMenuState.PendingPopupPos.Y));
		ImGui::OpenPopup(PopupId);
		ContextMenuState.PendingPopupSlot = -1;
	}

	if (ImGui::BeginPopup(PopupId))
	{
		if (ImGui::BeginMenu("Place Actor"))
		{
			const FPoint SpawnPos = ContextMenuState.PendingSpawnPos;
			const int32 SpawnSlot = ContextMenuState.PendingSpawnSlot;

			auto PlaceActorMenuItem = [&](const char* Label, EViewportPlaceActorType Type)
			{
				if (ImGui::MenuItem(Label))
				{
					FVector Location(0.0f, 0.0f, 0.0f);
					if (TryComputePlacementLocation(SpawnSlot, SpawnPos, Location))
					{
						SpawnActorFromViewportMenu(Type, Location);
					}
				}
			};

			PlaceActorMenuItem("Cube", EViewportPlaceActorType::Cube);
			PlaceActorMenuItem("Sphere", EViewportPlaceActorType::Sphere);
			PlaceActorMenuItem("Cylinder", EViewportPlaceActorType::Cylinder);
			PlaceActorMenuItem("Plane", EViewportPlaceActorType::Plane);
			PlaceActorMenuItem("Decal", EViewportPlaceActorType::Decal);
			PlaceActorMenuItem("Ambient Light", EViewportPlaceActorType::AmbientLight);
			PlaceActorMenuItem("Directional Light", EViewportPlaceActorType::DirectionalLight);
			PlaceActorMenuItem("Point Light", EViewportPlaceActorType::PointLight);
			PlaceActorMenuItem("Spot Light", EViewportPlaceActorType::SpotLight);
			PlaceActorMenuItem("Daylight", EViewportPlaceActorType::Daylight);

			ImGui::EndMenu();
		}

		ImGui::EndPopup();
	}
}

bool FLevelViewportLayout::TryComputePlacementLocation(int32 SlotIndex, const FPoint& ClientPos, FVector& OutLocation) const
{
	if (SlotIndex < 0 ||
		SlotIndex >= static_cast<int32>(LevelViewportClients.size()) ||
		SlotIndex >= MaxViewportSlots ||
		!ViewportWindows[SlotIndex])
	{
		return false;
	}

	FLevelEditorViewportClient* ViewportClient = LevelViewportClients[SlotIndex];
	if (!ViewportClient || !ViewportClient->GetCamera())
	{
		return false;
	}

	const FRect& ViewRect = ViewportWindows[SlotIndex]->GetRect();
	const float VPWidth = ViewportClient->GetViewport()
		? static_cast<float>(ViewportClient->GetViewport()->GetWidth())
		: ViewRect.Width;
	const float VPHeight = ViewportClient->GetViewport()
		? static_cast<float>(ViewportClient->GetViewport()->GetHeight())
		: ViewRect.Height;
	if (VPWidth <= 0.0f || VPHeight <= 0.0f)
	{
		return false;
	}

	const float LocalX = Clamp(ClientPos.X - ViewRect.X, 0.0f, VPWidth - 1.0f);
	const float LocalY = Clamp(ClientPos.Y - ViewRect.Y, 0.0f, VPHeight - 1.0f);
	const FRay Ray = ViewportClient->GetCamera()->DeprojectScreenToWorld(LocalX, LocalY, VPWidth, VPHeight);
	constexpr float SpawnDistanceFromCamera = 10.0f;
	OutLocation = Ray.Origin + Ray.Direction.Normalized() * SpawnDistanceFromCamera;
	return true;
}

AActor* FLevelViewportLayout::SpawnActorFromViewportMenu(EViewportPlaceActorType Type, const FVector& Location)
{
	if (!Editor)
	{
		return nullptr;
	}

	UWorld* World = Editor->GetWorld();
	if (!World)
	{
		return nullptr;
	}

	AActor* SpawnedActor = nullptr;
	switch (Type)
	{
	case EViewportPlaceActorType::Cube:
	{
		AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>();
		if (Actor)
		{
			Actor->InitDefaultComponents("Data/BasicShape/Cube.OBJ");
			SpawnedActor = Actor;
		}
		break;
	}
	case EViewportPlaceActorType::Sphere:
	{
		AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>();
		if (Actor)
		{
			Actor->InitDefaultComponents("Data/BasicShape/Sphere.OBJ");
			SpawnedActor = Actor;
		}
		break;
	}
	case EViewportPlaceActorType::Cylinder:
	{
		AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>();
		if (Actor)
		{
			Actor->InitDefaultComponents("Data/BasicShape/Cylinder.obj");
			SpawnedActor = Actor;
		}
		break;
	}
	case EViewportPlaceActorType::Plane:
	{
		AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>();
		if (Actor)
		{
			Actor->InitDefaultComponents("Data/BasicShape/Plane.obj");
			SpawnedActor = Actor;
		}
		break;
	}
	case EViewportPlaceActorType::Decal:
	{
		ADecalActor* Actor = World->SpawnActor<ADecalActor>();
		if (Actor)
		{
			Actor->InitDefaultComponents();
			SpawnedActor = Actor;
		}
		break;
	}
	case EViewportPlaceActorType::AmbientLight:
	{
		AAmbientLightActor* Actor = World->SpawnActor<AAmbientLightActor>();
		if (Actor)
		{
			Actor->InitDefaultComponents();
			SpawnedActor = Actor;
		}
		break;
	}
	case EViewportPlaceActorType::DirectionalLight:
	{
		ADirectionalLightActor* Actor = World->SpawnActor<ADirectionalLightActor>();
		if (Actor)
		{
			Actor->InitDefaultComponents();
				Actor->SetActorRotation(FVector(0.0f, 45.0f, -45.0f));
			SpawnedActor = Actor;
		}
		break;
	}
	case EViewportPlaceActorType::PointLight:
	{
		APointLightActor* Actor = World->SpawnActor<APointLightActor>();
		if (Actor)
		{
			Actor->InitDefaultComponents();
			SpawnedActor = Actor;
		}
		break;
	}
	case EViewportPlaceActorType::SpotLight:
	{
		ASpotLightActor* Actor = World->SpawnActor<ASpotLightActor>();
		if (Actor)
		{
			Actor->InitDefaultComponents();
			SpawnedActor = Actor;
		}
		break;
	}
	case EViewportPlaceActorType::Daylight:
	{
		ADaylightActor* Actor = World->SpawnActor<ADaylightActor>();
		if (Actor)
		{
			Actor->InitDefaultComponents();
			SpawnedActor = Actor;
		}
		break;
	}
	default:
		break;
	}

	if (!SpawnedActor)
	{
		return nullptr;
	}

	SpawnedActor->SetActorLocation(Location);
	World->InsertActorToOctree(SpawnedActor);
	if (SelectionManager)
	{
		SelectionManager->Select(SpawnedActor);
	}

	return SpawnedActor;
}

// ─── 각 뷰포트 패인 툴바 오버레이 ──────────────────────────

void FLevelViewportLayout::RenderPaneToolbar(int32 SlotIndex)
{
	if (SlotIndex >= MaxViewportSlots || !ViewportWindows[SlotIndex]) return;

	const FRect& PaneRect = ViewportWindows[SlotIndex]->GetRect();
	if (PaneRect.Width <= 0 || PaneRect.Height <= 0) return;

	EnsureToolbarIconsLoaded(RendererPtr);
	ID3D11ShaderResourceView** ToolbarIcons = GetToolbarIconTable();
	constexpr float ToolbarFallbackIconSize = 14.0f;
	constexpr float ToolbarMaxIconSize = 16.0f;

	auto DrawToolbarIconButton = [ToolbarIcons, ToolbarFallbackIconSize, ToolbarMaxIconSize](const char* Id, EToolbarIcon Icon, const char* FallbackLabel) -> bool
	{
		ID3D11ShaderResourceView* IconSRV = ToolbarIcons[static_cast<int32>(Icon)];
		if (!IconSRV)
		{
			return ImGui::Button(FallbackLabel);
		}

		const ImVec2 IconSize = GetToolbarIconRenderSize(Icon, ToolbarFallbackIconSize, ToolbarMaxIconSize);
		return ImGui::ImageButton(Id, reinterpret_cast<ImTextureID>(IconSRV), IconSize);
	};

	// 패인 상단에 오버레이 윈도우
	char OverlayID[64];
	snprintf(OverlayID, sizeof(OverlayID), "##PaneToolbar_%d", SlotIndex);

	ImGui::SetNextWindowPos(ImVec2(PaneRect.X, PaneRect.Y));
	ImGui::SetNextWindowBgAlpha(0.4f);
	ImGui::SetNextWindowSize(ImVec2(0, 0)); // auto-size

	ImGuiWindowFlags OverlayFlags =
		ImGuiWindowFlags_NoDecoration |
		ImGuiWindowFlags_AlwaysAutoResize |
		ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoFocusOnAppearing |
		ImGuiWindowFlags_NoNav |
		ImGuiWindowFlags_NoMove;

	ImGui::Begin(OverlayID, nullptr, OverlayFlags);
	{
		ImGui::PushID(SlotIndex);

		// Layout 드롭다운
		char PopupID[64];
		snprintf(PopupID, sizeof(PopupID), "LayoutPopup_%d", SlotIndex);

		if (DrawToolbarIconButton("##LayoutMenuIcon", EToolbarIcon::Menu, "Layout"))
		{
			ImGui::OpenPopup(PopupID);
		}

		if (ImGui::BeginPopup(PopupID))
		{
			constexpr int32 LayoutCount = static_cast<int32>(EViewportLayout::MAX);
			constexpr int32 Columns = 4;
			constexpr float IconSize = 32.0f;

			for (int32 i = 0; i < LayoutCount; ++i)
			{
				ImGui::PushID(i);

				bool bSelected = (static_cast<EViewportLayout>(i) == CurrentLayout);
				if (bSelected)
				{
					ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.8f, 1.0f));
				}

				bool bClicked = false;
				if (LayoutIcons[i])
				{
					bClicked = ImGui::ImageButton("##icon", (ImTextureID)LayoutIcons[i], ImVec2(IconSize, IconSize));
				}
				else
				{
					char Label[4];
					snprintf(Label, sizeof(Label), "%d", i);
					bClicked = ImGui::Button(Label, ImVec2(IconSize + 8, IconSize + 8));
				}

				if (bSelected)
				{
					ImGui::PopStyleColor();
				}

				if (bClicked)
				{
					const EViewportLayout TargetLayout = static_cast<EViewportLayout>(i);
					if (SlotIndex < static_cast<int32>(LevelViewportClients.size()))
					{
						SetActiveViewport(LevelViewportClients[SlotIndex]);
					}
					if (TargetLayout == EViewportLayout::OnePane && CurrentLayout != EViewportLayout::OnePane)
					{
						TemporaryOnePaneSourceSlot = SlotIndex;
						bRequestPreserveSplitOnOnePane = true;
					}
					SetLayout(TargetLayout);
					ImGui::CloseCurrentPopup();
				}

				if ((i + 1) % Columns != 0 && i + 1 < LayoutCount)
					ImGui::SameLine();

				ImGui::PopID();
			}
			ImGui::EndPopup();
		}

		// 토글 버튼 (같은 행)
		ImGui::SameLine();

		constexpr float ToggleIconSize = 16.0f;
		int32 ToggleIdx = (CurrentLayout == EViewportLayout::OnePane)
			? static_cast<int32>(LastSplitLayout == EViewportLayout::OnePane ? EViewportLayout::FourPanes2x2 : LastSplitLayout)
			: static_cast<int32>(EViewportLayout::OnePane);
		if (ToggleIdx < 0 || ToggleIdx >= static_cast<int32>(EViewportLayout::MAX))
		{
			ToggleIdx = static_cast<int32>(EViewportLayout::FourPanes2x2);
		}

		if (LayoutIcons[ToggleIdx])
		{
			if (ImGui::ImageButton("##toggle", (ImTextureID)LayoutIcons[ToggleIdx], ImVec2(ToggleIconSize, ToggleIconSize)))
			{
				if (SlotIndex < static_cast<int32>(LevelViewportClients.size()))
				{
					SetActiveViewport(LevelViewportClients[SlotIndex]);
				}
				if (CurrentLayout != EViewportLayout::OnePane)
				{
					TemporaryOnePaneSourceSlot = SlotIndex;
				}
				ToggleViewportSplit();
			}
		}
		else
		{
			const char* ToggleLabel = (CurrentLayout == EViewportLayout::OnePane) ? "Split" : "Merge";
			if (ImGui::Button(ToggleLabel))
			{
				if (SlotIndex < static_cast<int32>(LevelViewportClients.size()))
				{
					SetActiveViewport(LevelViewportClients[SlotIndex]);
				}
				if (CurrentLayout != EViewportLayout::OnePane)
				{
					TemporaryOnePaneSourceSlot = SlotIndex;
				}
				ToggleViewportSplit();
			}
		}

		// ViewportType + Settings 팝업
		if (SlotIndex < static_cast<int32>(LevelViewportClients.size()))
		{
			FLevelEditorViewportClient* VC = LevelViewportClients[SlotIndex];
			FViewportRenderOptions& Opts = VC->GetRenderOptions();

			// ── Viewport Type 드롭다운 (Perspective / Ortho 방향) ──
			ImGui::SameLine();

			static const char* ViewportTypeNames[] = {
				"Perspective", "Top", "Bottom", "Left", "Right", "Front", "Back", "Free Orthographic"
			};
			constexpr int32 ViewportTypeCount = sizeof(ViewportTypeNames) / sizeof(ViewportTypeNames[0]);
			int32 CurrentTypeIdx = static_cast<int32>(Opts.ViewportType);
			const char* CurrentTypeName = ViewportTypeNames[CurrentTypeIdx];

			char VTPopupID[64];
			snprintf(VTPopupID, sizeof(VTPopupID), "ViewportTypePopup_%d", SlotIndex);

			if (ImGui::Button(CurrentTypeName))
			{
				ImGui::OpenPopup(VTPopupID);
			}

			if (ImGui::BeginPopup(VTPopupID))
			{
				for (int32 t = 0; t < ViewportTypeCount; ++t)
				{
					bool bSelected = (t == CurrentTypeIdx);
					if (ImGui::Selectable(ViewportTypeNames[t], bSelected))
					{
						VC->SetViewportType(static_cast<ELevelViewportType>(t));
					}
				}
				ImGui::EndPopup();
			}

			// ── Settings 팝업 ──
			ImGui::SameLine();

			char SettingsPopupID[64];
			snprintf(SettingsPopupID, sizeof(SettingsPopupID), "SettingsPopup_%d", SlotIndex);

			if (DrawToolbarIconButton("##SettingsIcon", EToolbarIcon::Setting, "Settings"))
			{
				ImGui::OpenPopup(SettingsPopupID);
			}

			if (ImGui::BeginPopup(SettingsPopupID))
			{
				ImGui::Text("View Mode");

				int32 CurrentMode = static_cast<int32>(Opts.ViewMode);

				if (ImGui::BeginTable("ViewModeTable", 5, ImGuiTableFlags_SizingStretchSame))
				{
					ImGui::TableNextRow();

					ImGui::TableNextColumn();
					ImGui::RadioButton("Unlit", &CurrentMode, static_cast<int32>(EViewMode::Unlit));
					ImGui::TableNextColumn();
					ImGui::RadioButton("Lit_Gouraud", &CurrentMode, static_cast<int32>(EViewMode::Lit_Gouraud));
					ImGui::TableNextColumn();
					ImGui::RadioButton("Lit_Lambert", &CurrentMode, static_cast<int32>(EViewMode::Lit_Lambert));
					ImGui::TableNextColumn();
					ImGui::RadioButton("Lit_BlinnPhong", &CurrentMode, static_cast<int32>(EViewMode::Lit_BlinnPhong));
					ImGui::TableNextColumn();
					ImGui::RadioButton("Lit_BRDF", &CurrentMode, static_cast<int32>(EViewMode::Lit_BRDF));
					ImGui::TableNextColumn();
					ImGui::RadioButton("Lit_Toon", &CurrentMode, static_cast<int32>(EViewMode::Lit_Toon));

					ImGui::TableNextRow();

					ImGui::TableNextColumn();
					ImGui::RadioButton("LightComplexity", &CurrentMode, static_cast<int32>(EViewMode::LightComplexity));
					ImGui::TableNextColumn();
					ImGui::RadioButton("LightingOnly", &CurrentMode, static_cast<int32>(EViewMode::LightingOnly));
					ImGui::TableNextColumn();
					ImGui::RadioButton("LightInfluence", &CurrentMode, static_cast<int32>(EViewMode::LightInfluence));
					ImGui::TableNextColumn();
					ImGui::RadioButton("Heatmap", &CurrentMode, static_cast<int32>(EViewMode::Heatmap));
					ImGui::TableNextColumn();
					ImGui::RadioButton("WorldNormal", &CurrentMode, static_cast<int32>(EViewMode::WorldNormal));

					ImGui::TableNextRow();

					ImGui::TableNextColumn();
					ImGui::RadioButton("Wireframe", &CurrentMode, static_cast<int32>(EViewMode::Wireframe));
					ImGui::TableNextColumn();
					ImGui::RadioButton("SolidWireframe", &CurrentMode, static_cast<int32>(EViewMode::SolidWireframe));
					ImGui::TableNextColumn();
					ImGui::RadioButton("SceneDepth", &CurrentMode, static_cast<int32>(EViewMode::SceneDepth));

					ImGui::EndTable();
				}

				Opts.ViewMode = static_cast<EViewMode>(CurrentMode);

				ImGui::Separator();

				// Show Flags
				if (ImGui::BeginTable("ShowFlagsTable", 5, ImGuiTableFlags_Borders| ImGuiTableFlags_SizingStretchSame))
				{
					ImGui::TableNextRow();

					ImGui::TableNextColumn();
					ImGui::Checkbox("Primitives", &Opts.ShowFlags.bPrimitives);
					ImGui::TableNextColumn();
					ImGui::Checkbox("BillboardText", &Opts.ShowFlags.bBillboardText);
					ImGui::TableNextColumn();
					ImGui::Checkbox("Grid", &Opts.ShowFlags.bGrid);
					ImGui::TableNextColumn();
					ImGui::Checkbox("World Axis", &Opts.ShowFlags.bWorldAxis);
					ImGui::TableNextColumn();
					ImGui::Checkbox("Gizmo", &Opts.ShowFlags.bGizmo);

					ImGui::TableNextRow();

					ImGui::TableNextColumn();
					ImGui::Checkbox("Bounding Volume", &Opts.ShowFlags.bBoundingVolume);
					ImGui::TableNextColumn();
					ImGui::Checkbox("Debug Draw", &Opts.ShowFlags.bDebugDraw);
					ImGui::TableNextColumn();
					ImGui::Checkbox("Octree", &Opts.ShowFlags.bOctree);
					ImGui::TableNextColumn();
					ImGui::Checkbox("Fog", &Opts.ShowFlags.bFog);
					ImGui::TableNextColumn();
					ImGui::Checkbox("FXAA", &Opts.ShowFlags.bFXAA);

					ImGui::TableNextRow();

					ImGui::TableNextColumn();
					ImGui::Checkbox("SSAO", &Opts.ShowFlags.bSSAO);

					ImGui::TableNextColumn();
					ImGui::Checkbox("GodRay", &Opts.ShowFlags.bGodRay);
					ImGui::TableNextColumn();
					ImGui::Checkbox("LensFlare", &Opts.ShowFlags.bLensFlare);

					ImGui::EndTable();
				}

				ImGui::Separator();

				if (ImGui::CollapsingHeader("Viewport Utility Settings (Grid , Camera , SceneDepth , FXAA)"))
				{
					// Grid Settings
					ImGui::Text("Grid");
					ImGui::SliderFloat("Spacing", &Opts.GridSpacing, 0.1f, 10.0f, "%.1f");
					ImGui::SliderInt("Half Line Count", &Opts.GridHalfLineCount, 10, 500);

					ImGui::Separator();

					// Camera Sensitivity
					ImGui::Text("Camera");
					ImGui::SliderFloat("Move Sensitivity", &Opts.CameraMoveSensitivity, 0.1f, 5.0f, "%.1f");
					ImGui::SliderFloat("Rotate Sensitivity", &Opts.CameraRotateSensitivity, 0.1f, 5.0f, "%.1f");

					ImGui::Separator();

					// SceneDepth Settings
					ImGui::Text("SceneDepth");
					ImGui::SliderFloat("Exponent", &Opts.Exponent, 1.0f, 512.0f, "%.0f");
					ImGui::Combo("Mode", &Opts.SceneDepthVisMode, "Power\0Linear\0");

					ImGui::Text("FXAA");
				    ImGui::SliderFloat("EdgeThreshold", &Opts.EdgeThreshold, 0.06f, 0.333f, "%.3f");
				    ImGui::SliderFloat("EdgeThresholdMin", &Opts.EdgeThresholdMin, 0.0312f, 0.0833f, "%.4f");
				}

				ImGui::Separator();
				if (ImGui::CollapsingHeader("GodRay Settings"))
				{
					const FViewportRenderOptions DefaultGodRayOpts;
					ImGui::SliderFloat("Sun Radius",  &Opts.GodRaySunRadius,  0.01f, 0.5f,  "%.3f");
					ImGui::SliderFloat("Density",     &Opts.GodRayDensity,    0.95f, 0.99f,  "%.3f");
					ImGui::SliderFloat("Decay",       &Opts.GodRayDecay,      0.8f,  1.2f,  "%.3f");
					ImGui::SliderFloat("Weight",      &Opts.GodRayWeight,     0.0f,  1.0f,  "%.3f");
					ImGui::SliderFloat("Exposure",    &Opts.GodRayExposure,   0.0f,  2.0f,  "%.3f");
					ImGui::SliderFloat("Intensity",   &Opts.GodRayIntensity,  0.0f,  0.5f,  "%.3f");
					if (ImGui::Button("Reset GodRay Defaults"))
					{
						Opts.GodRaySunRadius = DefaultGodRayOpts.GodRaySunRadius;
						Opts.GodRayDensity = DefaultGodRayOpts.GodRayDensity;
						Opts.GodRayDecay = DefaultGodRayOpts.GodRayDecay;
						Opts.GodRayWeight = DefaultGodRayOpts.GodRayWeight;
						Opts.GodRayExposure = DefaultGodRayOpts.GodRayExposure;
						Opts.GodRayIntensity = DefaultGodRayOpts.GodRayIntensity;
					}
				}

				ImGui::Separator();
				if (ImGui::CollapsingHeader("LensFlare Settings"))
				{
					const FViewportRenderOptions DefaultLensFlareOpts;
					ImGui::SliderFloat("LF Intensity",        &Opts.LensFlareIntensity,        0.0f, 2.0f, "%.3f");
					ImGui::SliderFloat("LF Spread",           &Opts.LensFlareSpread,           0.0f, 1.5f, "%.3f");
					ImGui::SliderFloat("LF Inner Fade Start", &Opts.LensFlareInnerFadeStart,   0.0f, 0.3f, "%.3f");
					ImGui::SliderFloat("LF Inner Fade End",   &Opts.LensFlareInnerFadeEnd,     0.01f, 0.4f, "%.3f");
					ImGui::SliderFloat("LF Outer Fade Start", &Opts.LensFlareOuterFadeStart,   0.05f, 1.0f, "%.3f");
					ImGui::SliderFloat("LF Outer Fade End",   &Opts.LensFlareOuterFadeEnd,     0.1f, 1.2f, "%.3f");
					ImGui::SliderFloat("LF Occlusion",        &Opts.LensFlareOcclusionStrength, 0.0f, 1.0f, "%.3f");
					if (ImGui::Button("Reset LensFlare Defaults"))
					{
						Opts.LensFlareIntensity = DefaultLensFlareOpts.LensFlareIntensity;
						Opts.LensFlareSpread = DefaultLensFlareOpts.LensFlareSpread;
						Opts.LensFlareInnerFadeStart = DefaultLensFlareOpts.LensFlareInnerFadeStart;
						Opts.LensFlareInnerFadeEnd = DefaultLensFlareOpts.LensFlareInnerFadeEnd;
						Opts.LensFlareOuterFadeStart = DefaultLensFlareOpts.LensFlareOuterFadeStart;
						Opts.LensFlareOuterFadeEnd = DefaultLensFlareOpts.LensFlareOuterFadeEnd;
						Opts.LensFlareOcclusionStrength = DefaultLensFlareOpts.LensFlareOcclusionStrength;
					}
				}

				ImGui::Separator();
				ImGui::Text("Viewport Settings");
				ImGui::ColorEdit4("Clear Color", Opts.ClearColor);

				ImGui::EndPopup();
			}
		} // SlotIndex guard

		ImGui::PopID();
	}
	ImGui::End();
}

// ─── FEditorSettings ↔ 뷰포트 상태 동기화 ──────────────────

void FLevelViewportLayout::SaveToSettings()
{
	FEditorSettings& S = FEditorSettings::Get();

	S.LayoutType = static_cast<int32>(CurrentLayout);

	// 뷰포트별 렌더 옵션 저장
	for (int32 i = 0; i < ActiveSlotCount && i < static_cast<int32>(LevelViewportClients.size()); ++i)
	{
		S.SlotOptions[i] = LevelViewportClients[i]->GetRenderOptions();
	}

	// Splitter 비율 저장
	if (LayoutTransition != EViewportLayoutTransition::None && TransitionRestoreRatioCount > 0)
	{
		S.SplitterCount = TransitionRestoreRatioCount;
		if (S.SplitterCount > 3) S.SplitterCount = 3;
		for (int32 i = 0; i < S.SplitterCount; ++i)
		{
			S.SplitterRatios[i] = TransitionRestoreRatios[i];
		}
	}
	else if (RootSplitter)
	{
		TArray<SSplitter*> AllSplitters;
		SSplitter::CollectSplitters(RootSplitter, AllSplitters);
		S.SplitterCount = static_cast<int32>(AllSplitters.size());
		if (S.SplitterCount > 3) S.SplitterCount = 3;
		for (int32 i = 0; i < S.SplitterCount; ++i)
		{
			S.SplitterRatios[i] = AllSplitters[i]->GetRatio();
		}
	}
	else
	{
		S.SplitterCount = 0;
	}

	// Perspective 카메라 (slot 0) 저장
	if (!LevelViewportClients.empty())
	{
		UCameraComponent* Cam = LevelViewportClients[0]->GetCamera();
		if (Cam)
		{
			S.PerspCamLocation = Cam->GetWorldLocation();
			S.PerspCamRotation = Cam->GetRelativeRotation();
			const FCameraState& CS = Cam->GetCameraState();
			S.PerspCamFOV = CS.FOV * (180.0f / 3.14159265358979f); // rad → deg
			S.PerspCamNearClip = CS.NearZ;
			S.PerspCamFarClip = CS.FarZ;
		}
	}
}

void FLevelViewportLayout::LoadFromSettings()
{
	const FEditorSettings& S = FEditorSettings::Get();

	// 레이아웃 전환 (슬롯 생성 + 트리 빌드)
	EViewportLayout NewLayout = static_cast<EViewportLayout>(S.LayoutType);
	if (NewLayout >= EViewportLayout::MAX)
		NewLayout = EViewportLayout::OnePane;

	// OnePane이 아니면 레이아웃 적용 (Initialize에서 이미 OnePane으로 생성됨)
	if (NewLayout != EViewportLayout::OnePane)
	{
		// SetLayout 내부 bWasOnePane 분기를 피하기 위해 직접 전환
		SSplitter::DestroyTree(RootSplitter);
		RootSplitter = nullptr;
		DraggingSplitter = nullptr;

		int32 RequiredSlots = GetSlotCount(NewLayout);
		EnsureViewportSlots(RequiredSlots);

		RootSplitter = BuildSplitterTree(NewLayout);
		ActiveSlotCount = RequiredSlots;
		CurrentLayout = NewLayout;
	}

	// 뷰포트별 렌더 옵션 적용
	for (int32 i = 0; i < ActiveSlotCount && i < static_cast<int32>(LevelViewportClients.size()); ++i)
	{
		FLevelEditorViewportClient* VC = LevelViewportClients[i];
		VC->GetRenderOptions() = S.SlotOptions[i];

		// ViewportType에 따라 카메라 ortho/방향 설정
		VC->SetViewportType(S.SlotOptions[i].ViewportType);
	}

	// Splitter 비율 복원
	if (RootSplitter)
	{
		TArray<SSplitter*> AllSplitters;
		SSplitter::CollectSplitters(RootSplitter, AllSplitters);
		for (int32 i = 0; i < S.SplitterCount && i < static_cast<int32>(AllSplitters.size()); ++i)
		{
			AllSplitters[i]->SetRatio(S.SplitterRatios[i]);
		}
	}

	// Perspective 카메라 (slot 0) 복원
	if (!LevelViewportClients.empty())
	{
		UCameraComponent* Cam = LevelViewportClients[0]->GetCamera();
		if (Cam)
		{
			Cam->SetRelativeLocation(S.PerspCamLocation);
			Cam->SetRelativeRotation(S.PerspCamRotation);

			FCameraState CS = Cam->GetCameraState();
			CS.FOV = S.PerspCamFOV * (3.14159265358979f / 180.0f); // deg → rad
			CS.NearZ = S.PerspCamNearClip;
			CS.FarZ = S.PerspCamFarClip;
			Cam->SetCameraState(CS);
		}
	}
}
