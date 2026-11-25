#include "Editor/UI/EditorMainPanel.h"

#include "Editor/EditorEngine.h"
#include "Editor/Settings/EditorSettings.h"
#include "Editor/UI/EditorTheme.h"
#include "Editor/UI/UIStringHelpers.h"
#include "Editor/Viewport/LevelEditorViewportClient.h"
#include "Engine/Runtime/WindowsWindow.h"
#include "Component/CameraComponent.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Object/Object.h"

#include "ImGui/imgui.h"
#include "ImGui/imgui_internal.h"
#include "ImGui/imgui_impl_dx11.h"
#include "ImGui/imgui_impl_win32.h"

#include "Render/Pipeline/Renderer.h"
#include "Engine/Input/InputSystem.h"

#include <algorithm>
#include <random>
#include <utility>

namespace
{
	constexpr float EditorToolbarHeight = 40.0f;
	constexpr float FooterHeight = 32.0f;

	float GetUsableDeltaTime(float DeltaTime)
	{
		if (DeltaTime > 0.0f)
		{
			return DeltaTime;
		}

		const float ImGuiDeltaTime = ImGui::GetIO().DeltaTime;
		return ImGuiDeltaTime > 0.0f ? ImGuiDeltaTime : 1.0f / 60.0f;
	}

	bool IsAliveObject(const UObject* Object)
	{
		return Object && std::find(GUObjectArray.begin(), GUObjectArray.end(), Object) != GUObjectArray.end();
	}
}

void FEditorMainPanel::Create(FWindowsWindow* InWindow, FRenderer& InRenderer, UEditorEngine* InEditorEngine)
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	ImGuiIO& IO = ImGui::GetIO();
	FEditorTheme::ConfigureIO(IO);
	FEditorTheme::ApplyEditorStyle(ImGui::GetStyle());

	Window = InWindow;
	EditorEngine = InEditorEngine;

	// 위젯이 엔진 상태를 캐시하거나 UI를 제출하기 전에 ImGui 컨텍스트와 백엔드 초기화가 끝나 있어야 한다.
	FEditorTheme::ApplyFonts(IO);

	ImGui_ImplWin32_Init((void*)InWindow->GetHWND());
	ImGui_ImplDX11_Init(InRenderer.GetFD3DDevice().GetDevice(), InRenderer.GetFD3DDevice().GetDeviceContext());

	// 전제 조건: 이 시점에는 EditorEngine이 이미 유효하며, 위젯은 이를 질의 용도로만 참조한다.
	ConsoleWidget.Initialize(InEditorEngine);
	ControlWidget.Initialize(InEditorEngine);
	PropertyWidget.Initialize(InEditorEngine);
	SceneWidget.Initialize(InEditorEngine);
	StatWidget.Initialize(InEditorEngine);

	ControlWidget.SetOpen(FEditorSettings::Get().UI.bControl);
	PropertyWidget.SetOpen(FEditorSettings::Get().UI.bProperty);
	SceneWidget.SetOpen(FEditorSettings::Get().UI.bScene);
	StatWidget.SetOpen(FEditorSettings::Get().UI.bStat);
}

void FEditorMainPanel::Release()
{
	// 위젯은 ImGui 백엔드와 컨텍스트가 사라지기 전에 자신이 가진 일시적 UI 자원을 먼저 정리해야 한다.
	ConsoleWidget.Shutdown();
	ControlWidget.Shutdown();
	PropertyWidget.Shutdown();
	SceneWidget.Shutdown();
	StatWidget.Shutdown();
	FEditorConsoleWidget::ClearHistory();

	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
}

void FEditorMainPanel::Render(float DeltaTime)
{
	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	RenderEditorToolbar();
	RenderDockSpace();

	if (EditorEngine)
	{
		SCOPE_STAT_CAT("EditorEngine->RenderViewportUI", "5_UI");
		EditorEngine->RenderViewportUI(DeltaTime);
	}

	FEditorSettings& Settings = FEditorSettings::Get();

	ControlWidget.SetOpen(Settings.UI.bControl);
	if (!bHideEditorWindows && ControlWidget.IsOpen())
	{
		SCOPE_STAT_CAT("ControlWidget.Render", "5_UI");
		ControlWidget.Render(DeltaTime);
		Settings.UI.bControl = ControlWidget.IsOpen();
	}

	PropertyWidget.SetOpen(Settings.UI.bProperty);
	if (!bHideEditorWindows && PropertyWidget.IsOpen())
	{
		SCOPE_STAT_CAT("PropertyWidget.Render", "5_UI");
		PropertyWidget.Render(DeltaTime);
		Settings.UI.bProperty = PropertyWidget.IsOpen();
	}

	SceneWidget.SetOpen(Settings.UI.bScene);
	if (!bHideEditorWindows && SceneWidget.IsOpen())
	{
		SCOPE_STAT_CAT("SceneWidget.Render", "5_UI");
		SceneWidget.Render(DeltaTime);
		Settings.UI.bScene = SceneWidget.IsOpen();
	}

	StatWidget.SetOpen(Settings.UI.bStat);
	if (!bHideEditorWindows && StatWidget.IsOpen())
	{
		SCOPE_STAT_CAT("StatWidget.Render", "5_UI");
		StatWidget.Render(DeltaTime);
		Settings.UI.bStat = StatWidget.IsOpen();
	}

	if (!bHideEditorWindows)
	{
		RenderEditorDebugPanel();
	}

	const float EffectiveDeltaTime = GetUsableDeltaTime(DeltaTime);
	ConsoleWidget.UpdateDrawerAnimation(EffectiveDeltaTime, bConsoleDrawerVisible);

	FooterLogSystem.Tick(EffectiveDeltaTime);

	RenderShortcutOverlay();
	RenderConsoleDrawer(DeltaTime);
	RenderFooterOverlay(DeltaTime);

	ImGui::Render();
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

void FEditorMainPanel::RenderDockSpace()
{
	// 이 호스트 윈도우는 고정된 툴바와 푸터 사이의 중앙 도킹 영역을 소유한다.
	// 다른 에디터 패널은 이 영역 안에 도킹되는 것을 전제로 하므로,
	// 시각적 이음새와 레이아웃 충돌을 막기 위해 여기서는 패딩/보더/배경을 의도적으로 제거한다.
	const ImGuiViewport* MainViewport = ImGui::GetMainViewport();
	if (!MainViewport)
	{
		return;
	}

	const ImVec2 DockPos(MainViewport->WorkPos.x, MainViewport->WorkPos.y + EditorToolbarHeight);
	const ImVec2 DockSize(
		MainViewport->WorkSize.x,
		MainViewport->WorkSize.y > EditorToolbarHeight + FooterHeight
			? MainViewport->WorkSize.y - EditorToolbarHeight - FooterHeight
			: 0.0f);

	ImGui::SetNextWindowPos(DockPos);
	ImGui::SetNextWindowSize(DockSize);
	ImGui::SetNextWindowViewport(MainViewport->ID);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

	const ImGuiWindowFlags DockHostFlags =
		ImGuiWindowFlags_NoDocking
		| ImGuiWindowFlags_NoTitleBar
		| ImGuiWindowFlags_NoCollapse
		| ImGuiWindowFlags_NoResize
		| ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoBringToFrontOnFocus
		| ImGuiWindowFlags_NoNavFocus
		| ImGuiWindowFlags_NoBackground;

	ImGui::Begin("##MainDockHost", nullptr, DockHostFlags);
	ImGui::PopStyleVar(3);
	// 에디터 전체 레이아웃의 루트 도킹 노드다. 일반적인 컨텐츠 패널이 아니다.
	ImGui::DockSpace(ImGui::GetID("MainDockSpace"), ImVec2(0.0f, 0.0f));
	ImGui::End();
}

void FEditorMainPanel::RenderEditorToolbar()
{
	const ImGuiViewport* MainViewport = ImGui::GetMainViewport();
	if (!MainViewport)
	{
		return;
	}

	ImGui::SetNextWindowPos(MainViewport->WorkPos, ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(MainViewport->WorkSize.x, EditorToolbarHeight), ImGuiCond_Always);
	ImGui::SetNextWindowViewport(MainViewport->ID);

	const ImGuiWindowFlags BarFlags =
		ImGuiWindowFlags_NoDecoration
		| ImGuiWindowFlags_NoDocking
		| ImGuiWindowFlags_NoSavedSettings
		| ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoResize
		| ImGuiWindowFlags_NoNav
		| ImGuiWindowFlags_NoFocusOnAppearing;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 8.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.13f, 0.14f, 0.17f, 0.98f));
	ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.30f, 0.33f, 0.39f, 1.0f));

	if (ImGui::Begin("##EditorToolbar", nullptr, BarFlags))
	{
		FEditorSettings& Settings = FEditorSettings::Get();
		const bool bPlaying = EditorEngine && EditorEngine->IsPlayingInEditor();

		if (ImGui::Button("File", ImVec2(58.0f, 24.0f)))
		{
			ImGui::OpenPopup("##FileMenuPopup");
		}
		if (ImGui::BeginPopup("##FileMenuPopup"))
		{
			if (ImGui::MenuItem("New Scene", "Ctrl+N"))
			{
				EditorEngine->NewScene();
			}
			if (ImGui::MenuItem("Open Scene...", "Ctrl+O"))
			{
				EditorEngine->LoadSceneWithDialog();
			}
			if (ImGui::MenuItem("Save Scene", "Ctrl+S"))
			{
				EditorEngine->SaveScene();
			}
			if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S"))
			{
				EditorEngine->SaveSceneAsWithDialog();
			}

			ImGui::Separator();
			const FString CurrentLabel = EditorEngine->HasCurrentLevelFilePath()
				? FString("Current: ") + FUIStringHelpers::MakeRootRelativePath(EditorEngine->GetCurrentLevelFilePath())
				: FString("Current: Unsaved Scene");
			ImGui::BeginDisabled();
			ImGui::MenuItem(CurrentLabel.c_str(), nullptr, false, false);
			ImGui::EndDisabled();
			ImGui::EndPopup();
		}

		ImGui::SameLine();
		if (ImGui::Button("Windows", ImVec2(82.0f, 24.0f)))
		{
			bShowWidgetList = true;
			ImGui::OpenPopup("##WidgetListPopup");
		}
		if (ImGui::BeginPopup("##WidgetListPopup"))
		{
			ImGui::Checkbox("Control", &Settings.UI.bControl);
			ImGui::Checkbox("Property", &Settings.UI.bProperty);
			ImGui::Checkbox("Scene", &Settings.UI.bScene);
			ImGui::Checkbox("Stat", &Settings.UI.bStat);
			ImGui::Separator();
			ImGui::Checkbox("Editor Debug", &bShowEditorDebugPanel);
			ImGui::EndPopup();
		}
		else
		{
			bShowWidgetList = false;
		}

		ImGui::SameLine();
		if (ImGui::Button("Shortcuts", ImVec2(86.0f, 24.0f)))
		{
			bShowShortcutOverlay = !bShowShortcutOverlay;
		}

		const char* Domain = bPlaying ? "PIE" : "Editor";
		const float DomainWidth = ImGui::CalcTextSize(Domain).x;
		ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - DomainWidth - 16.0f);
		ImGui::TextUnformatted(Domain);
	}
	ImGui::End();

	ImGui::PopStyleColor(2);
	ImGui::PopStyleVar(3);
}

void FEditorMainPanel::RenderEditorDebugPanel()
{
	if (!bShowEditorDebugPanel || !EditorEngine)
	{
		return;
	}

	ImGui::SetNextWindowSize(ImVec2(520.0f, 320.0f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Editor Debug", &bShowEditorDebugPanel))
	{
		ImGui::End();
		return;
	}

	if (ImGui::CollapsingHeader("Place Actors (Grid)", ImGuiTreeNodeFlags_DefaultOpen))
	{
		const TArray<UEditorEngine::FPlaceableActorEntry>& Entries = EditorEngine->GetPlaceableActorEntries();
		if (Entries.empty())
		{
			ImGui::TextUnformatted("No placeable actor entries registered.");
		}
		else
		{
			if (DebugPlaceActorTypeIndex < 0)
			{
				DebugPlaceActorTypeIndex = 0;
			}
			if (DebugPlaceActorTypeIndex >= static_cast<int32>(Entries.size()))
			{
				DebugPlaceActorTypeIndex = static_cast<int32>(Entries.size()) - 1;
			}

			const char* CurrentActorLabel = Entries[DebugPlaceActorTypeIndex].DisplayName.c_str();
			if (ImGui::BeginCombo("Actor Type", CurrentActorLabel))
			{
				for (int32 Index = 0; Index < static_cast<int32>(Entries.size()); ++Index)
				{
					const bool bSelected = (DebugPlaceActorTypeIndex == Index);
					if (ImGui::Selectable(Entries[Index].DisplayName.c_str(), bSelected))
					{
						DebugPlaceActorTypeIndex = Index;
					}
					if (bSelected)
					{
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}

			ImGui::DragInt("Rows", &DebugGridRows, 1.0f, 1, 1024, "%d");
			ImGui::DragInt("Cols", &DebugGridCols, 1.0f, 1, 1024, "%d");
			ImGui::DragInt("Layers", &DebugGridLayers, 1.0f, 1, 256, "%d");
			ImGui::DragFloat("Grid Spacing", &DebugGridSpacing, 0.1f, 0.1f, 1000.0f, "%.2f");
			ImGui::Checkbox("Center Grid Around Origin", &bDebugGridCenter);

			ImGui::Separator();
			ImGui::Checkbox("Use Camera Forward Origin", &bDebugUseCameraOrigin);
			if (bDebugUseCameraOrigin)
			{
				ImGui::DragFloat("Distance", &DebugCameraForwardDistance, 0.5f, 0.0f, 100000.0f, "%.1f");
			}
			else
			{
				ImGui::DragFloat3("Manual Origin", &DebugManualGridOrigin.X, 0.1f, -100000.0f, 100000.0f, "%.2f");
			}

			ImGui::Separator();
			ImGui::Checkbox("Random Yaw", &bDebugRandomYaw);
			ImGui::BeginDisabled(!bDebugRandomYaw);
			ImGui::DragFloat("Yaw Range", &DebugRandomYawRange, 1.0f, 0.0f, 180.0f, "%.1f");
			ImGui::EndDisabled();

			ImGui::Checkbox("Apply Position Jitter", &bDebugApplyJitter);
			ImGui::BeginDisabled(!bDebugApplyJitter);
			ImGui::DragFloat("Jitter XY", &DebugJitterXY, 0.05f, 0.0f, 1000.0f, "%.2f");
			ImGui::DragFloat("Jitter Z", &DebugJitterZ, 0.05f, 0.0f, 1000.0f, "%.2f");
			ImGui::EndDisabled();

			const int64 TotalSpawnCount =
				static_cast<int64>(DebugGridRows) *
				static_cast<int64>(DebugGridCols) *
				static_cast<int64>(DebugGridLayers);
			ImGui::Text("Total Actors: %lld", static_cast<long long>(TotalSpawnCount));
			ImGui::SameLine();
			ImGui::Text("Last Batch: %u", static_cast<uint32>(DebugLastSpawnedActors.size()));

			if (ImGui::Button("Spawn Grid Actors"))
			{
				UWorld* World = EditorEngine->GetWorld();
				if (!World)
				{
					EditorEngine->EnqueueFooterEventLog("Grid spawn failed: invalid world");
				}
				else if (DebugPlaceActorTypeIndex < 0 || DebugPlaceActorTypeIndex >= static_cast<int32>(Entries.size()))
				{
					EditorEngine->EnqueueFooterEventLog("Grid spawn failed: invalid actor type");
				}
				else
				{
					const UEditorEngine::FPlaceableActorEntry& Entry = Entries[DebugPlaceActorTypeIndex];

					FVector GridOrigin = DebugManualGridOrigin;
					FVector GridRight(1.0f, 0.0f, 0.0f);
					FVector GridForward(0.0f, 1.0f, 0.0f);
					if (bDebugUseCameraOrigin)
					{
						if (FLevelEditorViewportClient* ActiveViewport = EditorEngine->GetActiveViewport())
						{
							if (UCameraComponent* ActiveCamera = ActiveViewport->GetCamera())
							{
								FVector CameraForward = ActiveCamera->GetForwardVector();
								CameraForward.Z = 0.0f;
								if (CameraForward.Length() > 0.0001f)
								{
									CameraForward.Normalize();
									GridForward = CameraForward;
									GridRight = FVector(-CameraForward.Y, CameraForward.X, 0.0f);
								}
								GridOrigin = ActiveCamera->GetWorldLocation() + ActiveCamera->GetForwardVector() * DebugCameraForwardDistance;
							}
						}
					}

					if (DebugGridRows < 1) DebugGridRows = 1;
					if (DebugGridCols < 1) DebugGridCols = 1;
					if (DebugGridLayers < 1) DebugGridLayers = 1;
					if (DebugGridSpacing < 0.1f) DebugGridSpacing = 0.1f;
					if (DebugRandomYawRange < 0.0f) DebugRandomYawRange = 0.0f;
					if (DebugRandomYawRange > 180.0f) DebugRandomYawRange = 180.0f;
					if (DebugJitterXY < 0.0f) DebugJitterXY = 0.0f;
					if (DebugJitterZ < 0.0f) DebugJitterZ = 0.0f;

					const float RowOffset = bDebugGridCenter ? (static_cast<float>(DebugGridRows - 1) * 0.5f) : 0.0f;
					const float ColOffset = bDebugGridCenter ? (static_cast<float>(DebugGridCols - 1) * 0.5f) : 0.0f;
					const float LayerOffset = bDebugGridCenter ? (static_cast<float>(DebugGridLayers - 1) * 0.5f) : 0.0f;

					std::mt19937 RNG{ std::random_device{}() };
					std::uniform_real_distribution<float> YawDist(-DebugRandomYawRange, DebugRandomYawRange);
					std::uniform_real_distribution<float> JitterXYDist(-DebugJitterXY, DebugJitterXY);
					std::uniform_real_distribution<float> JitterZDist(-DebugJitterZ, DebugJitterZ);

					TArray<AActor*> SpawnedActors;
					SpawnedActors.reserve(static_cast<size_t>(TotalSpawnCount));
					int32 SpawnedCount = 0;

					for (int32 Layer = 0; Layer < DebugGridLayers; ++Layer)
					{
						for (int32 Row = 0; Row < DebugGridRows; ++Row)
						{
							for (int32 Col = 0; Col < DebugGridCols; ++Col)
							{
								AActor* SpawnedActor = Entry.SpawnFactory ? Entry.SpawnFactory(World) : nullptr;
								if (!SpawnedActor)
								{
									continue;
								}

								if (Entry.Initializer && !Entry.Initializer(SpawnedActor))
								{
									World->DestroyActor(SpawnedActor);
									continue;
								}

								FVector SpawnLocation = GridOrigin
									+ GridRight * ((static_cast<float>(Col) - ColOffset) * DebugGridSpacing)
									+ GridForward * ((static_cast<float>(Row) - RowOffset) * DebugGridSpacing)
									+ FVector(0.0f, 0.0f, (static_cast<float>(Layer) - LayerOffset) * DebugGridSpacing);

								if (bDebugApplyJitter)
								{
									SpawnLocation
										+= GridRight * JitterXYDist(RNG)
										+ GridForward * JitterXYDist(RNG)
										+ FVector(0.0f, 0.0f, JitterZDist(RNG));
								}

								SpawnedActor->SetActorLocation(SpawnLocation);
								if (bDebugRandomYaw)
								{
									SpawnedActor->SetActorRotation(FVector(0.0f, YawDist(RNG), 0.0f));
								}
								World->InsertActorToOctree(SpawnedActor);

								SpawnedActors.push_back(SpawnedActor);
								++SpawnedCount;
							}
						}
					}
					DebugLastSpawnedActors = std::move(SpawnedActors);
					EditorEngine->EnqueueFooterEventLog(
						"Grid placed: " + std::to_string(SpawnedCount) + " actors");
				}
			}

			ImGui::SameLine();
			if (ImGui::Button("Clear Last Batch"))
			{
				bPendingClearLastBatch = true;
			}
		}
	}

	ImGui::End();
}

void FEditorMainPanel::RenderShortcutOverlay()
{
	if (!bShowShortcutOverlay)
	{
		return;
	}

	const ImGuiViewport* MainViewport = ImGui::GetMainViewport();
	if (!MainViewport)
	{
		return;
	}

	ImGui::SetNextWindowPos(
		ImVec2(MainViewport->WorkPos.x + MainViewport->WorkSize.x * 0.5f, MainViewport->WorkPos.y + MainViewport->WorkSize.y * 0.5f),
		ImGuiCond_Always,
		ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(430.0f, 0.0f), ImGuiCond_Always);
	ImGui::SetNextWindowBgAlpha(0.92f);

	const ImGuiWindowFlags Flags =
		ImGuiWindowFlags_NoDocking
		| ImGuiWindowFlags_NoSavedSettings
		| ImGuiWindowFlags_AlwaysAutoResize;

	if (ImGui::Begin("Editor Shortcuts", &bShowShortcutOverlay, Flags))
	{
		ImGui::TextUnformatted("Viewport");
		ImGui::Separator();
		ImGui::TextUnformatted("RMB + WASDQE : Move camera");
		ImGui::TextUnformatted("MMB / Arrow keys : Pan or rotate camera");
		ImGui::TextUnformatted("LMB : Select or drag gizmo");
		ImGui::TextUnformatted("Space : Cycle gizmo mode");
		ImGui::Spacing();
		ImGui::TextUnformatted("File");
		ImGui::Separator();
		ImGui::TextUnformatted("Ctrl+N : New Scene");
		ImGui::TextUnformatted("Ctrl+O : Open Scene");
		ImGui::TextUnformatted("Ctrl+S : Save Scene");
		ImGui::TextUnformatted("Ctrl+Shift+S : Save Scene As");
		ImGui::Spacing();
		ImGui::TextUnformatted("Commands");
		ImGui::Separator();
		ImGui::TextUnformatted("Delete : Delete selection");
		ImGui::TextUnformatted("Ctrl+D : Duplicate selection");
		ImGui::TextUnformatted("Esc : Stop PIE");
		ImGui::TextUnformatted("~ : Cycle console mode");
		ImGui::TextUnformatted("Console: stat fps, stat memory, stat none");
	}
	ImGui::End();
}

void FEditorMainPanel::RenderConsoleDrawer(float DeltaTime)
{
	(void)DeltaTime;

	ConsoleWidget.RenderDrawer(FooterHeight, bBringConsoleDrawerToFrontNextFrame);
	bBringConsoleDrawerToFrontNextFrame = false;
}

void FEditorMainPanel::RenderFooterOverlay(float DeltaTime)
{
	(void)DeltaTime;

	const ImGuiViewport* MainViewport = ImGui::GetMainViewport();
	if (!MainViewport)
	{
		return;
	}

	ImGui::SetNextWindowPos(
		ImVec2(MainViewport->WorkPos.x, MainViewport->WorkPos.y + MainViewport->WorkSize.y - FooterHeight),
		ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(MainViewport->WorkSize.x, FooterHeight), ImGuiCond_Always);
	ImGui::SetNextWindowViewport(MainViewport->ID);

	const ImGuiWindowFlags Flags =
		ImGuiWindowFlags_NoDecoration
		| ImGuiWindowFlags_NoDocking
		| ImGuiWindowFlags_NoSavedSettings
		| ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoResize
		| ImGuiWindowFlags_NoNav
		| ImGuiWindowFlags_NoFocusOnAppearing;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 5.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.11f, 0.12f, 0.15f, 0.98f));
	ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.30f, 0.33f, 0.39f, 1.0f));

	if (ImGui::Begin("##EditorFooter", nullptr, Flags))
	{
		const TArray<FString> ActiveLogs = FooterLogSystem.GetActiveMessages();
		if (ImGui::IsKeyPressed(ImGuiKey_GraveAccent, false))
		{
			switch (ConsoleBacktickCycleState)
			{
			case 0:
				ConsoleBacktickCycleState = 1;
				bConsoleDrawerVisible = false;
				bFocusConsoleInputNextFrame = true;
				break;
			case 1:
				ConsoleBacktickCycleState = 2;
				bConsoleDrawerVisible = true;
				bBringConsoleDrawerToFrontNextFrame = true;
				bFocusConsoleInputNextFrame = true;
				break;
			default:
				ConsoleBacktickCycleState = 0;
				bConsoleDrawerVisible = false;
				bFocusConsoleInputNextFrame = false;
				bFocusConsoleButtonNextFrame = true;
				break;
			}
		}

		if (bFocusConsoleButtonNextFrame)
		{
			ImGui::SetKeyboardFocusHere();
			bFocusConsoleButtonNextFrame = false;
		}

		if (ImGui::Button("Console"))
		{
			ToggleConsoleDrawer(true);
		}

		ImGui::SameLine();
		const bool bDrawerOpen = ConsoleWidget.IsDrawerMostlyOpen();
		const float InputWidth = MainViewport->WorkSize.x * (bDrawerOpen ? 0.35f : 0.175f);
		ConsoleWidget.RenderInputLine("##FooterConsoleInput", InputWidth, bFocusConsoleInputNextFrame);
		if (bFocusConsoleInputNextFrame)
		{
			ConsoleBacktickCycleState = bConsoleDrawerVisible ? 2 : 1;
		}
		bFocusConsoleInputNextFrame = false;

		ImGui::SameLine();
		ImGui::Text("Domain: %s", EditorEngine && EditorEngine->IsPlayingInEditor() ? "PIE" : "Editor");

		const FString LevelLabel = EditorEngine && EditorEngine->HasCurrentLevelFilePath()
			? FString("Level: ") + FUIStringHelpers::MakeRootRelativePath(EditorEngine->GetCurrentLevelFilePath())
			: FString("Level: Unsaved");
		const float LevelWidth = ImGui::CalcTextSize(LevelLabel.c_str()).x;
		const float LevelX = MainViewport->WorkSize.x - ImGui::GetStyle().WindowPadding.x - LevelWidth;

		if (!ActiveLogs.empty())
		{
			const char* LatestLog = ActiveLogs.back().c_str();
			const float LogWidth = ImGui::CalcTextSize(LatestLog).x;
			float LogX = LevelX - 16.0f - LogWidth;
			const float MinLogX = ImGui::GetCursorPosX() + 8.0f;
			if (LogX < MinLogX) LogX = MinLogX;
			ImGui::SameLine(LogX);
			ImGui::TextUnformatted(LatestLog);
		}

		ImGui::SameLine(LevelX);
		ImGui::TextUnformatted(LevelLabel.c_str());
	}
	ImGui::End();

	ImGui::PopStyleColor(2);
	ImGui::PopStyleVar(3);
}

void FEditorMainPanel::Update()
{
	UpdateFooterEventLogs();
	HandleGlobalShortcuts();
	ProcessPendingDebugActions();

	ImGuiIO& IO = ImGui::GetIO();

	bool bWantMouse = IO.WantCaptureMouse;
	bool bWantKeyboard = IO.WantCaptureKeyboard;
	const bool bWantTextInput = IO.WantTextInput;
	const bool bAnyUIItemActive = ImGui::IsAnyItemActive();
	const bool bAnyUIItemHovered = ImGui::IsAnyItemHovered();
	const bool bAnyPopupOpen = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
	const bool bMouseOverViewportRect = EditorEngine && EditorEngine->IsMouseOverViewport();

	bool bHoveredViewportContentWindow = false;
	bool bHoveredNonViewportWindow = false;
	if (ImGuiContext* Context = ImGui::GetCurrentContext())
	{
		if (ImGuiWindow* HoveredWindow = Context->HoveredWindow)
		{
			const char* HoveredName = HoveredWindow->Name ? HoveredWindow->Name : "";
			bHoveredViewportContentWindow =
				std::strcmp(HoveredName, "Viewport") == 0
				|| std::strncmp(HoveredName, "Viewport###", 11) == 0;
			bHoveredNonViewportWindow = !bHoveredViewportContentWindow;
		}
	}

	if (bShowShortcutOverlay)
	{
		bWantMouse = true;
		bWantKeyboard = true;
	}
	else if (bMouseOverViewportRect
		&& !bHoveredNonViewportWindow
		&& !bAnyPopupOpen)
	{
		bWantMouse = false;
		if (!bWantTextInput && !bAnyUIItemActive)
		{
			bWantKeyboard = false;
		}
	}
	else if (bHoveredNonViewportWindow)
	{
		bWantMouse = true;
	}

	InputSystem::Get().SetGuiMouseCapture(bWantMouse);
	InputSystem::Get().SetGuiKeyboardCapture(bWantKeyboard);
	InputSystem::Get().SetGuiTextInputCapture(bWantTextInput);

	if (Window)
	{
		HWND hWnd = Window->GetHWND();
		if (IO.WantTextInput)
		{
			ImmAssociateContextEx(hWnd, NULL, IACE_DEFAULT);
		}
		else
		{
			ImmAssociateContext(hWnd, NULL);
		}
	}
}

void FEditorMainPanel::HandleGlobalShortcuts()
{
	if (!EditorEngine)
	{
		return;
	}

	ImGuiIO& IO = ImGui::GetIO();
	if (IO.WantTextInput)
	{
		return;
	}

	InputSystem& Input = InputSystem::Get();
	if (!Input.GetKey(VK_CONTROL) && Input.GetKeyDown('X'))
	{
		EditorEngine->ToggleCoordSystem();
		return;
	}

	const bool bCtrl = Input.GetKey(VK_CONTROL);
	if (!bCtrl)
	{
		return;
	}

	const bool bShift = Input.GetKey(VK_SHIFT);
	if (Input.GetKeyDown('N'))
	{
		EditorEngine->NewScene();
	}
	else if (Input.GetKeyDown('O'))
	{
		EditorEngine->LoadSceneWithDialog();
	}
	else if (Input.GetKeyDown('S'))
	{
		if (bShift)
		{
			EditorEngine->SaveSceneAsWithDialog();
		}
		else
		{
			EditorEngine->SaveScene();
		}
	}
}

void FEditorMainPanel::ToggleConsoleDrawer(bool bFocusInput)
{
	bConsoleDrawerVisible = !bConsoleDrawerVisible;
	bBringConsoleDrawerToFrontNextFrame = bConsoleDrawerVisible;
	bFocusConsoleInputNextFrame = bConsoleDrawerVisible && bFocusInput;
	ConsoleBacktickCycleState = bConsoleDrawerVisible ? 2 : 0;
	if (!bConsoleDrawerVisible)
	{
		bFocusConsoleButtonNextFrame = true;
	}
}

void FEditorMainPanel::ProcessPendingDebugActions()
{
	if (!bPendingClearLastBatch || !EditorEngine)
	{
		return;
	}
	bPendingClearLastBatch = false;

	UWorld* World = EditorEngine->GetWorld();
	int32 DestroyedCount = 0;

	if (!World)
	{
		DebugLastSpawnedActors.clear();
		EditorEngine->EnqueueFooterEventLog("Grid cleared: 0 actors");
		return;
	}

	EditorEngine->GetSelectionManager().ClearSelection();

	for (AActor* Actor : DebugLastSpawnedActors)
	{
		if (!Actor || !IsAliveObject(Actor) || Actor->GetWorld() != World)
		{
			continue;
		}

		World->DestroyActor(Actor);
		++DestroyedCount;
	}

	DebugLastSpawnedActors.clear();
	EditorEngine->EnqueueFooterEventLog(
		"Grid cleared: " + std::to_string(DestroyedCount) + " actors");
}

void FEditorMainPanel::UpdateFooterEventLogs()
{
	if (!EditorEngine)
	{
		return;
	}

	FString EventLog;
	while (EditorEngine->DequeueFooterEventLog(EventLog))
	{
		FooterLogSystem.Push(EventLog);
	}

	const bool bPIEPlaying = EditorEngine->IsPlayingInEditor();
	if (!bFooterEventStateInitialized)
	{
		bPrevPIEPlaying = bPIEPlaying;
		bFooterEventStateInitialized = true;
		return;
	}

	if (!bPrevPIEPlaying && bPIEPlaying)
	{
		FooterLogSystem.Push("PIE started");
	}
	else if (bPrevPIEPlaying && !bPIEPlaying)
	{
		FooterLogSystem.Push("PIE ended");
	}
	bPrevPIEPlaying = bPIEPlaying;
}

void FEditorMainPanel::HideEditorWindowsForPIE()
{
	if (bHasSavedUIVisibility)
	{
		bHideEditorWindows = true;
		bShowWidgetList = false;
		return;
	}

	FEditorSettings& Settings = FEditorSettings::Get();
	SavedUIVisibility = Settings.UI;
	bSavedShowWidgetList = bShowWidgetList;
	bHasSavedUIVisibility = true;
	bHideEditorWindows = true;
	bShowWidgetList = false;

	Settings.UI.bConsole = false;
	Settings.UI.bControl = false;
	Settings.UI.bProperty = false;
	Settings.UI.bScene = false;
	Settings.UI.bStat = false;
}

void FEditorMainPanel::RestoreEditorWindowsAfterPIE()
{
	if (!bHasSavedUIVisibility)
	{
		bHideEditorWindows = false;
		return;
	}

	FEditorSettings& Settings = FEditorSettings::Get();
	Settings.UI = SavedUIVisibility;
	bShowWidgetList = bSavedShowWidgetList;
	bHideEditorWindows = false;
	bHasSavedUIVisibility = false;
}
