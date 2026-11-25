#include "Editor/EditorEngine.h"

#include "Engine/Runtime/WindowsWindow.h"
#include "Engine/Serialization/SceneSaveManager.h"
#include "Component/CameraComponent.h"
#include "Component/GizmoComponent.h"
#include "Component/BillboardComponent.h"
#include "GameFramework/DecalActor.h"
#include "GameFramework/LightActor.h"
#include "GameFramework/DaylightActor.h"
#include "GameFramework/StaticMeshActor.h"
#include "GameFramework/World.h"
#include "Editor/EditorRenderPipeline.h"
#include "Editor/UI/EditorFileUtils.h"
#include "Editor/Viewport/LevelEditorViewportClient.h"
#include "Object/ObjectFactory.h"
#include "Mesh/ObjManager.h"
#include "Input/InputSystem.h"
#include "GameFramework/AActor.h"
#include "Materials/MaterialManager.h"
#include "Engine/Platform/Paths.h"
#include "ImGui/imgui.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <filesystem>
#include <utility>

IMPLEMENT_CLASS(UEditorEngine, UEngine)

namespace
{
UCameraComponent* FindPerspectiveViewportCamera(const UEditorEngine* InEditor)
{
	if (!InEditor)
	{
		return nullptr;
	}

	for (FLevelEditorViewportClient* VC : InEditor->GetLevelViewportClients())
	{
		if (!VC)
		{
			continue;
		}
		if (VC->GetRenderOptions().ViewportType == ELevelViewportType::Perspective
			|| VC->GetRenderOptions().ViewportType == ELevelViewportType::FreeOrthographic)
		{
			return VC->GetCamera();
		}
	}
	return nullptr;
}

FString BuildScenePathFromStem(const FString& InStem)
{
	std::filesystem::path ScenePath = std::filesystem::path(FSceneSaveManager::GetSceneDirectory())
		/ (FPaths::ToWide(InStem) + FSceneSaveManager::SceneExtension);
	return FPaths::ToUtf8(ScenePath.wstring());
}

FString GetFileStem(const FString& InPath)
{
	const std::filesystem::path P(FPaths::ToWide(InPath));
	return FPaths::ToUtf8(P.stem().wstring());
}

constexpr const char* GPlaceableIdEmptyActor = "basic_actor_empty";
constexpr const char* GPlaceableIdCube = "basic_shape_cube";
constexpr const char* GPlaceableIdSphere = "basic_shape_sphere";
constexpr const char* GPlaceableIdCylinder = "basic_shape_cylinder";
constexpr const char* GPlaceableIdPlane = "basic_shape_plane";
constexpr const char* GPlaceableIdDecal = "basic_actor_decal";
constexpr const char* GPlaceableIdAmbientLight = "basic_actor_ambient_light";
constexpr const char* GPlaceableIdDirectionalLight = "basic_actor_directional_light";
constexpr const char* GPlaceableIdPointLight = "basic_actor_point_light";
constexpr const char* GPlaceableIdSpotLight = "basic_actor_spot_light";
constexpr const char* GPlaceableIdDaylight  = "basic_actor_daylight";
}

void UEditorEngine::Init(FWindowsWindow* InWindow)
{
	// 엔진 공통 초기화 (Renderer, D3D, 싱글턴 등)
	UEngine::Init(InWindow);

	FObjManager::ScanMeshAssets();
	FMaterialManager::Get().ScanMaterialAssets();
	RegisterDefaultPlaceableActors();

	// 에디터 전용 초기화
	FEditorSettings::Get().LoadFromFile(FEditorSettings::GetDefaultSettingsPath());

	MainPanel.Create(Window, Renderer, this);

	// World
	if (WorldList.empty())
	{
		CreateWorldContext(EWorldType::Editor, FName("Default"));
	}
	SetActiveWorld(WorldList[0].ContextHandle);
	GetWorld()->InitWorld();

	// Selection & Gizmo
	SelectionManager.Init();
	SelectionManager.SetWorld(GetWorld());

	// 뷰포트 레이아웃 초기화 + 저장된 설정 복원
	ViewportLayout.Initialize(this, Window, Renderer, &SelectionManager);
	ViewportLayout.LoadFromSettings();
	ApplyTransformSettingsToGizmo();

	// Editor render pipeline
	SetRenderPipeline(std::make_unique<FEditorRenderPipeline>(this, Renderer));
}

void UEditorEngine::Shutdown()
{
	// 에디터 해제 (엔진보다 먼저)
	ViewportLayout.SaveToSettings();
	FEditorSettings::Get().SaveToFile(FEditorSettings::GetDefaultSettingsPath());
	CloseScene();
	SelectionManager.Shutdown();
	MainPanel.Release();

	// 뷰포트 레이아웃 해제
	ViewportLayout.Release();

	// 엔진 공통 해제 (Renderer, D3D 등)
	UEngine::Shutdown();
}

void UEditorEngine::OnWindowResized(uint32 Width, uint32 Height)
{
	UEngine::OnWindowResized(Width, Height);
	// 윈도우 리사이즈 시에는 ImGui 패널이 실제 크기를 결정하므로
	// FViewport RT는 SSplitter 레이아웃에서 지연 리사이즈로 처리됨
}

void UEditorEngine::Tick(float DeltaTime)
{
	// --- PIE 요청 처리 (프레임 경계에서 처리되도록 Tick 선두에서 소비) ---
	if (bRequestEndPlayMapQueued)
	{
		bRequestEndPlayMapQueued = false;
		EndPlayMap();
	}
	if (PlaySessionRequest.has_value())
	{
		StartQueuedPlaySessionRequest();
	}

	UpdateViewportMouseSuppression();
	ApplyTransformSettingsToGizmo();

	for (FEditorViewportClient* VC : ViewportLayout.GetAllViewportClients())
	{
		VC->Tick(DeltaTime);
	}

	MainPanel.Update();
	UEngine::Tick(DeltaTime);
	SelectionManager.Tick();
}

UCameraComponent* UEditorEngine::GetCamera() const
{
	if (FLevelEditorViewportClient* ActiveVC = ViewportLayout.GetActiveViewport())
	{
		return ActiveVC->GetCamera();
	}
	return nullptr;
}

void UEditorEngine::RenderUI(float DeltaTime)
{
	MainPanel.Render(DeltaTime);
}

void UEditorEngine::ToggleCoordSystem()
{
	FEditorSettings& Settings = FEditorSettings::Get();
	Settings.CoordSystem = (Settings.CoordSystem == EEditorCoordSystem::World)
		? EEditorCoordSystem::Local
		: EEditorCoordSystem::World;
	ApplyTransformSettingsToGizmo();
}

void UEditorEngine::ApplyTransformSettingsToGizmo()
{
	UGizmoComponent* Gizmo = GetGizmo();
	if (!Gizmo)
	{
		return;
	}

	const FEditorSettings& Settings = FEditorSettings::Get();
	Gizmo->SetWorldSpace(Settings.CoordSystem == EEditorCoordSystem::World);
	Gizmo->SetSnapSettings(
		Settings.bEnableTranslationSnap, Settings.TranslationSnapSize,
		Settings.bEnableRotationSnap, Settings.RotationSnapSize,
		Settings.bEnableScaleSnap, Settings.ScaleSnapSize);
}

bool UEditorEngine::RegisterPlaceableActor(FPlaceableActorEntry InEntry)
{
	if (InEntry.Id.empty() || InEntry.DisplayName.empty() || !InEntry.SpawnFactory)
	{
		return false;
	}

	if (FindPlaceableActorEntryById(InEntry.Id))
	{
		return false;
	}

	PlaceableActorEntries.push_back(std::move(InEntry));
	return true;
}

const UEditorEngine::FPlaceableActorEntry* UEditorEngine::FindPlaceableActorEntryById(const FString& InPlaceableId) const
{
	for (const FPlaceableActorEntry& Entry : PlaceableActorEntries)
	{
		if (Entry.Id == InPlaceableId)
		{
			return &Entry;
		}
	}
	return nullptr;
}

bool UEditorEngine::PlaceActorById(const FString& InPlaceableId, int32 InCount)
{
	const FPlaceableActorEntry* Entry = FindPlaceableActorEntryById(InPlaceableId);
	UWorld* World = GetWorld();
	if (!Entry || !World)
	{
		return false;
	}

	const int32 SpawnCount = (std::max)(1, InCount);
	bool bSpawnedAny = false;
	FVector BaseLocation(0.0f, 0.0f, 0.0f);
	if (FLevelEditorViewportClient* ActiveVC = GetActiveViewport())
	{
		if (UCameraComponent* Camera = ActiveVC->GetCamera())
		{
			BaseLocation = Camera->GetWorldLocation() + Camera->GetForwardVector() * 10.0f;
		}
	}

	for (int32 Index = 0; Index < SpawnCount; ++Index)
	{
		AActor* Actor = Entry->SpawnFactory(World);
		if (!Actor)
		{
			continue;
		}

		if (Entry->Initializer && !Entry->Initializer(Actor))
		{
			World->DestroyActor(Actor);
			continue;
		}

		Actor->SetActorLocation(BaseLocation + FVector(static_cast<float>(Index) * 3.0f, 0.0f, 0.0f));
		World->InsertActorToOctree(Actor);
		SelectionManager.Select(Actor);
		bSpawnedAny = true;
	}

	return bSpawnedAny;
}

void UEditorEngine::RegisterDefaultPlaceableActors()
{
	PlaceableActorEntries.clear();

	RegisterPlaceableActor({
		GPlaceableIdEmptyActor,
		"Empty Actor",
		[](UWorld* World) -> AActor*
		{
			return World ? static_cast<AActor*>(World->SpawnActor<AActor>()) : nullptr;
		},
		[](AActor* Actor) -> bool
		{
			if (!Actor)
			{
				return false;
			}

			UBillboardComponent* Billboard = Actor->AddComponent<UBillboardComponent>();
			if (!Billboard)
			{
				return false;
			}

			Actor->SetRootComponent(Billboard);
			Billboard->SetSpriteSize(0.9f, 0.9f);
			return true;
		}
	});

	RegisterPlaceableActor({
		GPlaceableIdCube,
		"Cube",
		[](UWorld* World) -> AActor*
		{
			return World ? static_cast<AActor*>(World->SpawnActor<AStaticMeshActor>()) : nullptr;
		},
		[](AActor* Actor) -> bool
		{
			AStaticMeshActor* StaticMeshActor = Cast<AStaticMeshActor>(Actor);
			if (!StaticMeshActor)
			{
				return false;
			}
			StaticMeshActor->InitDefaultComponents("Data/BasicShape/Cube.OBJ");
			return true;
		}
	});

	RegisterPlaceableActor({
		GPlaceableIdSphere,
		"Sphere",
		[](UWorld* World) -> AActor*
		{
			return World ? static_cast<AActor*>(World->SpawnActor<AStaticMeshActor>()) : nullptr;
		},
		[](AActor* Actor) -> bool
		{
			AStaticMeshActor* StaticMeshActor = Cast<AStaticMeshActor>(Actor);
			if (!StaticMeshActor)
			{
				return false;
			}
			StaticMeshActor->InitDefaultComponents("Data/BasicShape/Sphere.OBJ");
			return true;
		}
	});

	RegisterPlaceableActor({
		GPlaceableIdCylinder,
		"Cylinder",
		[](UWorld* World) -> AActor*
		{
			return World ? static_cast<AActor*>(World->SpawnActor<AStaticMeshActor>()) : nullptr;
		},
		[](AActor* Actor) -> bool
		{
			AStaticMeshActor* StaticMeshActor = Cast<AStaticMeshActor>(Actor);
			if (!StaticMeshActor)
			{
				return false;
			}
			StaticMeshActor->InitDefaultComponents("Data/BasicShape/Cylinder.obj");
			return true;
		}
	});
	
	RegisterPlaceableActor({
		GPlaceableIdPlane,
		"Plane",
		[](UWorld* World) -> AActor*
		{
			return World ? static_cast<AActor*>(World->SpawnActor<AStaticMeshActor>()) : nullptr;
		},
		[](AActor* Actor) -> bool
		{
			AStaticMeshActor* StaticMeshActor = Cast<AStaticMeshActor>(Actor);
			if (!StaticMeshActor)
			{
				return false;
			}
			StaticMeshActor->InitDefaultComponents("Data/BasicShape/Plane.obj");
			return true;
		}
	});

	RegisterPlaceableActor({
		GPlaceableIdDecal,
		"Decal",
		[](UWorld* World) -> AActor*
		{
			return World ? static_cast<AActor*>(World->SpawnActor<ADecalActor>()) : nullptr;
		},
		[](AActor* Actor) -> bool
		{
			ADecalActor* DecalActor = Cast<ADecalActor>(Actor);
			if (!DecalActor)
			{
				return false;
			}
			DecalActor->InitDefaultComponents();
			return true;
		}
	});

	RegisterPlaceableActor({
		GPlaceableIdAmbientLight,
		"Ambient Light",
		[](UWorld* World) -> AActor*
		{
			return World ? static_cast<AActor*>(World->SpawnActor<AAmbientLightActor>()) : nullptr;
		},
		[](AActor* Actor) -> bool
		{
			AAmbientLightActor* LightActor = Cast<AAmbientLightActor>(Actor);
			if (!LightActor)
			{
				return false;
			}
			LightActor->InitDefaultComponents();
			return true;
		}
	});

	RegisterPlaceableActor({
		GPlaceableIdDirectionalLight,
		"Directional Light",
		[](UWorld* World) -> AActor*
		{
			return World ? static_cast<AActor*>(World->SpawnActor<ADirectionalLightActor>()) : nullptr;
		},
		[](AActor* Actor) -> bool
		{
			ADirectionalLightActor* LightActor = Cast<ADirectionalLightActor>(Actor);
			if (!LightActor)
			{
				return false;
			}
			LightActor->InitDefaultComponents();
				LightActor->SetActorRotation(FVector(0.0f, 45.0f, -45.0f));
			return true;
		}
	});

	RegisterPlaceableActor({
		GPlaceableIdPointLight,
		"Point Light",
		[](UWorld* World) -> AActor*
		{
			return World ? static_cast<AActor*>(World->SpawnActor<APointLightActor>()) : nullptr;
		},
		[](AActor* Actor) -> bool
		{
			APointLightActor* LightActor = Cast<APointLightActor>(Actor);
			if (!LightActor)
			{
				return false;
			}
			LightActor->InitDefaultComponents();
			return true;
		}
	});

	RegisterPlaceableActor({
		GPlaceableIdSpotLight,
		"Spot Light",
		[](UWorld* World) -> AActor*
		{
			return World ? static_cast<AActor*>(World->SpawnActor<ASpotLightActor>()) : nullptr;
		},
		[](AActor* Actor) -> bool
		{
			ASpotLightActor* LightActor = Cast<ASpotLightActor>(Actor);
			if (!LightActor)
			{
				return false;
			}
			LightActor->InitDefaultComponents();
			return true;
		}
	});

	RegisterPlaceableActor({
		GPlaceableIdDaylight,
		"Daylight",
		[](UWorld* World) -> AActor*
		{
			return World ? static_cast<AActor*>(World->SpawnActor<ADaylightActor>()) : nullptr;
		},
		[](AActor* Actor) -> bool
		{
			ADaylightActor* DLActor = Cast<ADaylightActor>(Actor);
			if (!DLActor) return false;
			DLActor->InitDefaultComponents();
			return true;
		}
	});
}

void UEditorEngine::UpdateViewportMouseSuppression()
{
	const bool bAnyPopupOpen = ImGui::GetCurrentContext()
		&& ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
	if (bHadAnyPopupOpenLastFrame && !bAnyPopupOpen)
	{
		bSuppressViewportMouseUntilButtonsReleased = true;
	}

	const bool bAnyMouseButtonDown =
		InputSystem::Get().GetKey(VK_LBUTTON)
		|| InputSystem::Get().GetKey(VK_RBUTTON)
		|| InputSystem::Get().GetKey(VK_MBUTTON)
		|| InputSystem::Get().GetKey(VK_XBUTTON1)
		|| InputSystem::Get().GetKey(VK_XBUTTON2);
	if (bSuppressViewportMouseUntilButtonsReleased && !bAnyMouseButtonDown)
	{
		bSuppressViewportMouseUntilButtonsReleased = false;
	}

	InputSystem::Get().SetForceViewportMouseBlock(bSuppressViewportMouseUntilButtonsReleased);
	bHadAnyPopupOpenLastFrame = bAnyPopupOpen;
}

// ─── PIE (Play In Editor) ────────────────────────────────
// UE 패턴 요약: Request는 단일 슬롯(std::optional)에 저장만 하고 즉시 실행하지 않는다.
// 실제 StartPIE는 다음 Tick 선두의 StartQueuedPlaySessionRequest에서 일어난다.
// 이유는 UI 콜백/트랜잭션 도중 같은 불안정한 타이밍을 피하기 위함.

void UEditorEngine::RequestPlaySession(const FRequestPlaySessionParams& InParams)
{
	// 동시 요청은 UE와 동일하게 덮어쓴다 (진짜 큐 아님 — 단일 슬롯).
	PlaySessionRequest = InParams;
	EnqueueFooterEventLog("PIE start requested");
}

void UEditorEngine::CancelRequestPlaySession()
{
	PlaySessionRequest.reset();
}

void UEditorEngine::RequestEndPlayMap()
{
	if (!PlayInEditorSessionInfo.has_value())
	{
		return;
	}
	bRequestEndPlayMapQueued = true;
	EnqueueFooterEventLog("PIE stop requested");
}

void UEditorEngine::StartQueuedPlaySessionRequest()
{
	if (!PlaySessionRequest.has_value())
	{
		return;
	}

	const FRequestPlaySessionParams Params = *PlaySessionRequest;
	PlaySessionRequest.reset();

	// 이미 PIE 중이면 기존 세션을 정리 후 새로 시작 (단순화).
	if (PlayInEditorSessionInfo.has_value())
	{
		EndPlayMap();
	}

	switch (Params.SessionDestination)
	{
	case EPIESessionDestination::InProcess:
		StartPlayInEditorSession(Params);
		break;
	}
}

void UEditorEngine::StartPlayInEditorSession(const FRequestPlaySessionParams& Params)
{
	// 1) 현재 에디터 월드를 복제해 PIE 월드 생성 (UE의 CreatePIEWorldByDuplication 대응).
	UWorld* EditorWorld = GetWorld();
	if (!EditorWorld)
	{
		return;
	}
	UWorld* PIEWorld = Cast<UWorld>(EditorWorld->Duplicate(nullptr));
	if (!PIEWorld)
	{
		return;
	}

	// 2) PIE WorldContext를 WorldList에 등록.
	FWorldContext Ctx;
	Ctx.WorldType = EWorldType::PIE;
	Ctx.ContextHandle = FName("PIE");
	Ctx.ContextName = "PIE";
	Ctx.World = PIEWorld;
	WorldList.push_back(Ctx);

	// 3) 세션 정보 기록 (이전 활성 핸들 포함 — EndPlayMap에서 복원).
	FPlayInEditorSessionInfo Info;
	Info.OriginalRequestParams = Params;
	Info.PIEStartTime = 0.0;
	Info.PreviousActiveWorldHandle = GetActiveWorldHandle();
	if (FLevelEditorViewportClient* ActiveVC = ViewportLayout.GetActiveViewport())
	{
		if (UCameraComponent* VCCamera = ActiveVC->GetCamera())
		{
			Info.SavedViewportCamera.Location = VCCamera->GetWorldLocation();
			Info.SavedViewportCamera.Rotation = VCCamera->GetRelativeRotation();
			Info.SavedViewportCamera.CameraState = VCCamera->GetCameraState();
			Info.SavedViewportCamera.bValid = true;
		}
	}
	PlayInEditorSessionInfo = Info;

	// 4) ActiveWorldHandle을 PIE로 전환 — 이후 GetWorld()는 PIE 월드를 반환.
	SetActiveWorld(FName("PIE"));

	// GPU Occlusion readback은 ProxyId 기반이라 월드가 갈리면 stale.
	// 이전 프레임 결과를 무효화해야 wrong-proxy hit 방지.
	if (IRenderPipeline* Pipeline = GetRenderPipeline())
	{
		Pipeline->OnSceneCleared();
	}

	// 5) 활성 뷰포트 카메라를 PIE 월드의 ActiveCamera로 설정 —
	//    LOD 갱신 등에서 ActiveCamera를 참조하므로 설정 필요.
	if (FLevelEditorViewportClient* ActiveVC = ViewportLayout.GetActiveViewport())
	{
		if (UCameraComponent* VCCamera = ActiveVC->GetCamera())
		{
			PIEWorld->SetActiveCamera(VCCamera);
		}
	}

	// 6) Selection을 PIE 월드 기준으로 재바인딩 — 에디터 액터를 가리킨 채로 두면
	//    픽킹(=PIE 월드) / outliner / outline 렌더가 모두 어긋난다.
	SelectionManager.ClearSelection();
	//SelectionManager.SetGizmoEnabled(false); //PIE가 시작되면 gizmo 비활성화
	SelectionManager.SetWorld(PIEWorld);
	
	//이 코드와 대응되는 게 아래 EndPlayMap()에 있음.
	//MainPanel.HideEditorWindowsForPIE(); //PIE 중에는 에디터 패널을 숨김.
	//ViewportLayout.DisableWorldAxisForPIE(); //PIE 중에는 월드 축 렌더링을 비활성화.

	// 7) BeginPlay 트리거 — 모든 등록/바인딩이 끝난 다음 첫 Tick 이전에 호출.
	//    UWorld::BeginPlay가 bHasBegunPlay를 먼저 세팅하므로 BeginPlay 도중
	//    SpawnActor로 만든 신규 액터도 자동으로 BeginPlay된다.
	PIEWorld->BeginPlay();
}

void UEditorEngine::EndPlayMap()
{
	if (!PlayInEditorSessionInfo.has_value())
	{
		return;
	}

	// 활성 월드를 PIE 시작 전 핸들로 복원.
	const FName PrevHandle = PlayInEditorSessionInfo->PreviousActiveWorldHandle;
	SetActiveWorld(PrevHandle);

	// 복귀한 Editor 월드의 VisibleProxies/캐시된 카메라 상태를 강제 무효화.
	// PIE 중 Editor WorldTick이 skip되어 캐시가 PIE 시작 전 시점 그대로 남아 있고,
	// NeedsVisibleProxyRebuild()가 카메라 변화 기반이라 false를 반환하면 stale
	// VisibleProxies가 그대로 재사용되어 dangling proxy 참조로 크래시가 날 수 있다.
	//
	// 또한 Renderer::PerObjectCBPool은 ProxyId로 인덱싱되는 월드 간 공유 풀이라,
	// PIE 중 PIE 프록시가 덮어쓴 슬롯이 그대로 남아 있으면 Editor 프록시의
	// bPerObjectCBDirty=false 상태로 인해 업로드가 skip되어 PIE 마지막 transform으로
	// 렌더된다. 모든 Editor 프록시를 PerObjectCB dirty로 마킹해 재업로드 강제.
	if (UWorld* EditorWorld = GetWorld())
	{
		EditorWorld->GetScene().MarkAllPerObjectCBDirty();

		// ActiveCamera는 PIE 시작 시 PIE 월드로 옮겨졌고 PIE 월드와 함께 파괴됐다.
		// Editor 월드의 ActiveCamera는 여전히 그 dangling 포인터를 가리킬 수 있으므로
		// 활성 뷰포트의 카메라로 다시 바인딩해 줘야 frustum culling이 정상 동작한다.
		if (FLevelEditorViewportClient* ActiveVC = ViewportLayout.GetActiveViewport())
		{
			if (UCameraComponent* VCCamera = ActiveVC->GetCamera())
			{
				if (PlayInEditorSessionInfo->SavedViewportCamera.bValid)
				{
					const FPIEViewportCameraSnapshot& SavedCamera = PlayInEditorSessionInfo->SavedViewportCamera;
					VCCamera->SetWorldLocation(SavedCamera.Location);
					VCCamera->SetRelativeRotation(SavedCamera.Rotation);
					VCCamera->SetCameraState(SavedCamera.CameraState);
				}

				EditorWorld->SetActiveCamera(VCCamera);
			}
		}
	}

	// Selection을 에디터 월드로 복원 — PIE 액터는 곧 파괴되므로 먼저 비운다.
	SelectionManager.ClearSelection();
	//SelectionManager.SetGizmoEnabled(true); //PIE가 끝나면 gizmo 활성화
	SelectionManager.SetWorld(GetWorld());
	
	//이 코드와 대응되는 게 위의 StartPlayInEditorSession()에 있음.
	//MainPanel.RestoreEditorWindowsAfterPIE();
	//ViewportLayout.RestoreWorldAxisAfterPIE();

	// PIE WorldContext 제거 (DestroyWorldContext가 EndPlay + DestroyObject 수행).
	DestroyWorldContext(FName("PIE"));

	// PIE 월드의 프록시가 모두 파괴됐으므로 GPU Occlusion readback 무효화.
	if (IRenderPipeline* Pipeline = GetRenderPipeline())
	{
		Pipeline->OnSceneCleared();
	}

	PlayInEditorSessionInfo.reset();
}

// ─── 기존 메서드 ──────────────────────────────────────────

void UEditorEngine::ResetViewport()
{
	ViewportLayout.ResetViewport(GetWorld());
}

void UEditorEngine::CloseScene()
{
	ClearScene();
}

void UEditorEngine::NewScene()
{
	StopPlayInEditorImmediate();
	ClearScene();
	FWorldContext& Ctx = CreateWorldContext(EWorldType::Editor, FName("NewScene"), "New Scene");
	Ctx.World->InitWorld();
	SetActiveWorld(Ctx.ContextHandle);
	SelectionManager.SetWorld(GetWorld());

	// 언리얼식 기본 라이트 — 새 씬에 DirectionalLight를 비스듬히 스폰
	if (UWorld* W = GetWorld())
	{
		ADirectionalLightActor* SunLight = W->SpawnActor<ADirectionalLightActor>();
		SunLight->InitDefaultComponents();
		SunLight->SetActorLocation(FVector(0.0f, 0.0f, 500.0f));
		SunLight->SetActorRotation(FVector(0.0f, 45.0f, -45.0f));
	}

	ResetViewport();
	CurrentLevelFilePath.clear();
	EnqueueFooterEventLog("New Scene created");
}

bool UEditorEngine::SaveSceneAs(const FString& InSceneName)
{
	if (InSceneName.empty())
	{
		return false;
	}

	StopPlayInEditorImmediate();
	FWorldContext* Ctx = GetWorldContextFromHandle(GetActiveWorldHandle());
	if (!Ctx || !Ctx->World)
	{
		return false;
	}

	UCameraComponent* PerspectiveCam = FindPerspectiveViewportCamera(this);
	FSceneSaveManager::SaveSceneAsJSON(InSceneName, *Ctx, PerspectiveCam);
	CurrentLevelFilePath = BuildScenePathFromStem(InSceneName);
	return true;
}

bool UEditorEngine::SaveScene()
{
	if (HasCurrentLevelFilePath())
	{
		const bool bSaved = SaveSceneAs(GetFileStem(CurrentLevelFilePath));
		if (bSaved)
		{
			EnqueueFooterEventLog("Scene saved");
		}
		return bSaved;
	}

	return SaveSceneAsWithDialog();
}

bool UEditorEngine::SaveSceneAsWithDialog()
{
	const std::wstring InitialDir = FSceneSaveManager::GetSceneDirectory();
	const std::wstring DefaultFile = HasCurrentLevelFilePath()
		? std::filesystem::path(FPaths::ToWide(CurrentLevelFilePath)).filename().wstring()
		: std::wstring(L"Untitled.Scene");
	const FString SelectedPath = FEditorFileUtils::SaveFileDialog({
		.Filter = L"Scene Files (*.Scene)\0*.Scene\0All Files (*.*)\0*.*\0",
		.Title = L"Save Scene As",
		.DefaultExtension = L"Scene",
		.InitialDirectory = InitialDir.c_str(),
		.DefaultFileName = DefaultFile.c_str(),
		.OwnerWindowHandle = Window ? Window->GetHWND() : nullptr,
		.bFileMustExist = false,
		.bPathMustExist = true,
		.bPromptOverwrite = true,
		.bReturnRelativeToProjectRoot = false,
	});
	if (SelectedPath.empty())
	{
		return false;
	}

	const bool bSaved = SaveSceneAs(GetFileStem(SelectedPath));
	if (bSaved)
	{
		EnqueueFooterEventLog("Scene saved as");
	}
	return bSaved;
}

bool UEditorEngine::LoadSceneFromPath(const FString& InScenePath)
{
	if (InScenePath.empty())
	{
		return false;
	}

	StopPlayInEditorImmediate();
	ClearScene();

	FWorldContext LoadCtx;
	FPerspectiveCameraData CamData;
	FSceneSaveManager::LoadSceneFromJSON(InScenePath, LoadCtx, CamData);
	if (!LoadCtx.World)
	{
		return false;
	}

	WorldList.push_back(LoadCtx);
	SetActiveWorld(LoadCtx.ContextHandle);
	SelectionManager.SetWorld(LoadCtx.World);
	LoadCtx.World->WarmupPickingData();
	ResetViewport();

	if (CamData.bValid)
	{
		if (UCameraComponent* Cam = FindPerspectiveViewportCamera(this))
		{
			Cam->SetWorldLocation(CamData.Location);
			Cam->SetRelativeRotation(CamData.Rotation);
			FCameraState CS = Cam->GetCameraState();
			CS.FOV = CamData.FOV;
			CS.NearZ = CamData.NearClip;
			CS.FarZ = CamData.FarClip;
			Cam->SetCameraState(CS);
		}
	}

	CurrentLevelFilePath = InScenePath;
	EnqueueFooterEventLog("Scene loaded");
	return true;
}

bool UEditorEngine::LoadSceneWithDialog()
{
	const std::wstring InitialDir = FSceneSaveManager::GetSceneDirectory();
	const FString SelectedPath = FEditorFileUtils::OpenFileDialog({
		.Filter = L"Scene Files (*.Scene)\0*.Scene\0All Files (*.*)\0*.*\0",
		.Title = L"Load Scene",
		.InitialDirectory = InitialDir.c_str(),
		.OwnerWindowHandle = Window ? Window->GetHWND() : nullptr,
		.bFileMustExist = true,
		.bPathMustExist = true,
		.bPromptOverwrite = false,
		.bReturnRelativeToProjectRoot = false,
	});
	if (SelectedPath.empty())
	{
		return false;
	}

	return LoadSceneFromPath(SelectedPath);
}

void UEditorEngine::EnqueueFooterEventLog(const FString& InMessage)
{
	if (InMessage.empty())
	{
		return;
	}

	PendingFooterEventLogs.push_back(InMessage);
}

bool UEditorEngine::DequeueFooterEventLog(FString& OutMessage)
{
	if (PendingFooterEventLogs.empty())
	{
		return false;
	}

	OutMessage = PendingFooterEventLogs.front();
	PendingFooterEventLogs.erase(PendingFooterEventLogs.begin());
	return true;
}

void UEditorEngine::ClearScene()
{
	StopPlayInEditorImmediate();
	SelectionManager.ClearSelection();
	SelectionManager.SetWorld(nullptr);

	// 씬 프록시 파괴 전 GPU Occlusion 스테이징 데이터 무효화
	if (IRenderPipeline* Pipeline = GetRenderPipeline())
		Pipeline->OnSceneCleared();

	for (FWorldContext& Ctx : WorldList)
	{
		Ctx.World->EndPlay();
		UObjectManager::Get().DestroyObject(Ctx.World);
	}

	WorldList.clear();
	ActiveWorldHandle = FName::None;
	CurrentLevelFilePath.clear();

	ViewportLayout.DestroyAllCameras();
}
