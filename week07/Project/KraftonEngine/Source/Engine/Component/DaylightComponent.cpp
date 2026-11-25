#include "DaylightComponent.h"
#include "Component/LightComponent.h"
#include "Component/BillboardComponent.h"
#include "GameFramework/LightActor.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Render/Proxy/FScene.h"
#include "Serialization/Archive.h"
#include "Materials/MaterialManager.h"
#include "Math/MathUtils.h"
#include <cmath>

IMPLEMENT_CLASS(UDaylightComponent, USceneComponent)

// ────────────────── 태양 색상 키프레임 (고도 기준) ──────────────────
static const struct FSunColorKey { float Elevation; FVector4 Color; } SunColorKeys[] =
{
	{ -0.1f, FVector4(1.00f, 0.20f, 0.00f, 1.0f) }, // 지평선 직하 
	{  0.0f, FVector4(1.00f, 0.35f, 0.05f, 1.0f) }, // 일출/일몰 
	{  0.2f, FVector4(1.00f, 0.85f, 0.55f, 1.0f) }, // 낮은 고도
	{  1.0f, FVector4(1.00f, 0.97f, 0.90f, 1.0f) }, // 천정
};

static const struct FAmbColorKey { float Elevation; FVector4 Color; } AmbColorKeys[] =
{
	{ -0.5f, FVector4(0.04f, 0.05f, 0.12f, 1.0f) }, // 깊은 밤
	{ -0.1f, FVector4(0.08f, 0.06f, 0.16f, 1.0f) }, // 여명/황혼
	{  0.0f, FVector4(0.42f, 0.28f, 0.22f, 1.0f) }, // 일출/일몰
	{  0.2f, FVector4(0.28f, 0.42f, 0.65f, 1.0f) }, // 아침/오후
	{  1.0f, FVector4(0.25f, 0.42f, 0.72f, 1.0f) }, // 천정
};

static FVector4 LerpColor(const FVector4& A, const FVector4& B, float T)
{
	return FVector4(
		A.X + (B.X - A.X) * T,
		A.Y + (B.Y - A.Y) * T,
		A.Z + (B.Z - A.Z) * T,
		1.0f);
}

static FColor OrbitArcColor(float worldZ)
{
	if (worldZ > 0.5f)
	{
		float t = (worldZ - 0.5f) / 0.5f;
		return FColor(
			static_cast<uint8>(220 + (int)(35 * t)),
			static_cast<uint8>(200 + (int)(55 * t)),
			static_cast<uint8>(120 + (int)(135 * t)));
	}
	else if (worldZ > 0.0f)
	{
		float t = worldZ / 0.5f;
		return FColor(
			static_cast<uint8>(220),
			static_cast<uint8>(100 + (int)(100 * t)),
			static_cast<uint8>(30 + (int)(90 * t)));
	}
	else if (worldZ > -0.3f)
	{
		float t = (worldZ + 0.3f) / 0.3f;
		return FColor(
			static_cast<uint8>(20 * t),
			static_cast<uint8>(20 + (int)(30 * t)),
			static_cast<uint8>(60 + (int)(60 * t)));
	}
	else
	{
		return FColor(10, 15, 40);
	}
}

UDaylightComponent::UDaylightComponent()
{
	bTickEnable = true;
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bTickEnabled = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

UDaylightComponent::~UDaylightComponent()
{

	if (Owner)
	{
		if (UWorld* World = Owner->GetWorld())
		{
			World->GetScene().ClearSkyColor();
		}
	}
}

void UDaylightComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	USceneComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bDaylightEnabled)
		return;


	// 캐시를 항상 무효화하고 월드에서 다시 찾는다 라이트는 소수라 비용 무시 
	ControlledSuns.clear();
	ControlledAmbients.clear();
	FindLightActors();

	if (bAutoAdvanceTime)
	{
		bool bShouldAdvance = false;
		if (Owner)
		{
			UWorld* World = Owner->GetWorld();
			bool bIsPlaying = World && World->HasBegunPlay();
			bShouldAdvance = bIsPlaying || bAutoAdvanceInEditor;
		}
		if (bShouldAdvance)
		{
			TimeOfDay += DeltaTime * TimeScale / 3600.0f;
			if (TimeOfDay >= 24.0f)
				TimeOfDay -= 24.0f;
		}
	}

	UpdateLighting();

	if (bShowOrbitDebug && Owner)
	{
		UWorld* World = Owner->GetWorld();
		if (World)
			DrawOrbitWireframe(World->GetScene());
	}
}

void UDaylightComponent::FindLightActors()
{
	if (!Owner) return;
	UWorld* World = Owner->GetWorld();
	if (!World) return;

	// 씬 전역의 모든 DirectionalLight/AmbientLight를 수집한다.


	for (AActor* Actor : World->GetActors())
	{
		if (!Actor) continue;

		for (UActorComponent* Comp : Actor->GetComponents())
		{
			// Unreal의 bAtmosphereSunLight opt-in 패턴: 플래그가 꺼진 라이트는 아티스트가
			// 독립적인 톤/색을 유지하도록 Daylight가 건드리지 않는다.
			if (UDirectionalLightComponent* DirComp = Cast<UDirectionalLightComponent>(Comp))
			{
				if (DirComp->bAffectedByDaylight)
					ControlledSuns.push_back(DirComp);
			}
			else if (UAmbientLightComponent* AmbComp = Cast<UAmbientLightComponent>(Comp))
			{
				if (AmbComp->bAffectedByDaylight)
					ControlledAmbients.push_back(AmbComp);
			}
		}
	}
}

void UDaylightComponent::UpdateLighting()
{
	FVector SunPos = CalculateSunPosition();
	float SunElevation = SunPos.Z; // 고도 추출
	bool bSunAboveHorizon = SunElevation > 0.0f;

	// ── Directional Lights: 모든 태양 광원에 동일한 방향/색/강도 적용 ──
	{
		FVector LightDir(-SunPos.X, -SunPos.Y, -SunPos.Z);
		float len = LightDir.Length();
		if (len > 0.0001f) LightDir = LightDir / len;

		float pitch = -asinf(FMath::Clamp(LightDir.Z, -1.0f, 1.0f)) * FMath::RadToDeg;
		float yaw = atan2f(LightDir.Y, LightDir.X) * FMath::RadToDeg;
		const FRotator SunRot(pitch, yaw, 0.0f);
		const float    SunI   = bSunAboveHorizon ? CalculateSunIntensity(SunPos) : 0.0f;
		const FVector4 SunCol = CalculateSunColor(SunElevation);
		const bool     bSunVisible = bSunAboveHorizon && SunI > 0.001f;

		for (UDirectionalLightComponent* Sun : ControlledSuns)
		{
			if (!Sun) continue;
			Sun->SetWorldRotation(SunRot);
			Sun->SetIntensity(SunI);
			if (bSunAboveHorizon)
			{
				Sun->SetColor(SunCol);
				Sun->SetVisible(bSunVisible);
			}
			else
			{
				Sun->SetVisible(false);
			}
		}
	}


	{
		const float    AmbI   = CalculateAmbientIntensity(SunPos);
		const FVector4 AmbCol = CalculateAmbientColor(SunElevation);
		for (UAmbientLightComponent* Amb : ControlledAmbients)
		{
			if (!Amb) continue;
			Amb->SetIntensity(AmbI);
			Amb->SetColor(AmbCol);
		}
	}

	if (Owner)
	{
		UWorld* World = Owner->GetWorld();
		if (World)
		{
			FScene& Scene = World->GetScene();
			if (bOverrideSkyColor && bDaylightEnabled)
			{
				FVector4 SkyCol = CalculateAmbientColor(SunElevation); // 고도 전달
				Scene.SetSkyColor(SkyCol.X, SkyCol.Y, SkyCol.Z, 1.0f);
			}
			else
			{
				Scene.ClearSkyColor();
			}
		}
	}
}

void UDaylightComponent::DrawOrbitWireframe(FScene& Scene) const
{
	constexpr int32 N = 72;
	constexpr float OrbitRadius = 3.0f;

	FVector Origin = GetWorldLocation();
	FVector OrbitForward = GetForwardVector();
	FVector OrbitUp = GetUpVector();

	FVector SunPos = CalculateSunPosition();
	FColor  SunMarkerColor(255, 220, 60);

	FVector PrevPt;
	for (int32 i = 0; i <= N; ++i)
	{
		float t = static_cast<float>(i) / N;
		float theta = t * 2.0f * FMath::Pi - FMath::Pi * 0.5f;

		FVector dir = OrbitForward * cosf(theta) + OrbitUp * sinf(theta);
		FVector pt = Origin + dir * OrbitRadius;

		if (i > 0)
		{
			float midZ = (dir.Z + (OrbitForward * cosf(theta - 2.0f * FMath::Pi / N) + OrbitUp * sinf(theta - 2.0f * FMath::Pi / N)).Z) * 0.5f;
			FColor segColor = OrbitArcColor(midZ);
			Scene.AddDebugLine(PrevPt, pt, segColor);
		}
		PrevPt = pt;
	}

	FVector sunPt = Origin + SunPos * OrbitRadius;
	FVector right = OrbitForward.Cross(OrbitUp).Normalized();
	float markerLen = OrbitRadius * 0.15f;
	Scene.AddDebugLine(sunPt - right * markerLen, sunPt + right * markerLen, SunMarkerColor);
	Scene.AddDebugLine(sunPt - OrbitUp * markerLen, sunPt + OrbitUp * markerLen, SunMarkerColor);
	Scene.AddDebugLine(sunPt - OrbitForward * markerLen, sunPt + OrbitForward * markerLen, SunMarkerColor);

	if (fabsf(OrbitForward.Z) + fabsf(OrbitUp.Z) > 0.01f)
	{
		float tRise = atan2f(-OrbitForward.Z, OrbitUp.Z);
		float tSet = tRise + FMath::Pi;
		for (float tc : {tRise, tSet})
		{
			FVector dir2 = OrbitForward * cosf(tc) + OrbitUp * sinf(tc);
			FVector crossPt = Origin + dir2 * OrbitRadius;
			FColor horizColor(180, 120, 60);
			Scene.AddDebugLine(crossPt - dir2 * markerLen * 0.5f, crossPt + dir2 * markerLen * 0.5f, horizColor);
		}
	}
}

void UDaylightComponent::ContributeSelectedVisuals(FScene& Scene) const
{
	DrawOrbitWireframe(Scene);
}

FVector UDaylightComponent::CalculateSunPosition() const
{
	float theta = (TimeOfDay / 24.0f) * 2.0f * FMath::Pi - FMath::Pi * 0.5f;

	FVector OrbitForward = GetForwardVector();
	FVector OrbitUp = GetUpVector();

	FVector pos = OrbitForward * cosf(theta) + OrbitUp * sinf(theta);
	float len = pos.Length();
	return (len > 0.0001f) ? pos / len : FVector(0.0f, 0.0f, 1.0f);
}

FVector4 UDaylightComponent::CalculateSunColor(float SunElevation) const
{
	int keyCount = static_cast<int>(sizeof(SunColorKeys) / sizeof(SunColorKeys[0]));
	if (SunElevation <= SunColorKeys[0].Elevation)
		return SunColorKeys[0].Color;
	if (SunElevation >= SunColorKeys[keyCount - 1].Elevation)
		return SunColorKeys[keyCount - 1].Color;

	for (int i = 0; i < keyCount - 1; ++i)
	{
		if (SunElevation < SunColorKeys[i + 1].Elevation)
		{
			float span = SunColorKeys[i + 1].Elevation - SunColorKeys[i].Elevation;
			float t = (SunElevation - SunColorKeys[i].Elevation) / span;
			return LerpColor(SunColorKeys[i].Color, SunColorKeys[i + 1].Color, t);
		}
	}
	return SunColorKeys[keyCount - 1].Color;
}

float UDaylightComponent::CalculateSunIntensity(const FVector& SunPos) const
{
	float elev = SunPos.Z;
	if (elev <= 0.0f) return 0.0f;
	float fade = FMath::Clamp(elev / 0.1f, 0.0f, 1.0f);
	return SunMaxIntensity * elev * fade;
}

FVector4 UDaylightComponent::CalculateAmbientColor(float SunElevation) const
{
	int keyCount = static_cast<int>(sizeof(AmbColorKeys) / sizeof(AmbColorKeys[0]));
	if (SunElevation <= AmbColorKeys[0].Elevation)
		return AmbColorKeys[0].Color;
	if (SunElevation >= AmbColorKeys[keyCount - 1].Elevation)
		return AmbColorKeys[keyCount - 1].Color;

	for (int i = 0; i < keyCount - 1; ++i)
	{
		if (SunElevation < AmbColorKeys[i + 1].Elevation)
		{
			float span = AmbColorKeys[i + 1].Elevation - AmbColorKeys[i].Elevation;
			float t = (SunElevation - AmbColorKeys[i].Elevation) / span;
			return LerpColor(AmbColorKeys[i].Color, AmbColorKeys[i + 1].Color, t);
		}
	}
	return AmbColorKeys[keyCount - 1].Color;
}

float UDaylightComponent::CalculateAmbientIntensity(const FVector& SunPos) const
{
	float elev = SunPos.Z;
	float transWidth = 1.5f / 24.0f * 2.0f;
	float blend = FMath::Clamp((elev + transWidth) / (transWidth * 2.0f), 0.0f, 1.0f);
	return AmbientNightIntensity + (AmbientMaxIntensity - AmbientNightIntensity) * blend;
}

void UDaylightComponent::SetTimeOfDay(float InTime)
{
	TimeOfDay = FMath::Clamp(InTime, 0.0f, 24.0f);
}

float UDaylightComponent::GetSunElevationDeg() const
{
	FVector SunPos = CalculateSunPosition();
	return asinf(FMath::Clamp(SunPos.Z, -1.0f, 1.0f)) * FMath::RadToDeg;
}

void UDaylightComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
	USceneComponent::GetEditableProperties(OutProps);

	OutProps.push_back({ "Daylight Enabled",    EPropertyType::Bool,  &bDaylightEnabled });
	OutProps.push_back({ "Time of Day",         EPropertyType::Float, &TimeOfDay,            0.0f,  24.0f,   0.01f });
	OutProps.push_back({ "Time Scale",          EPropertyType::Float, &TimeScale,             0.0f, 3600.0f,  1.0f });
	OutProps.push_back({ "Auto Advance Time",   EPropertyType::Bool,  &bAutoAdvanceTime });
	OutProps.push_back({ "Advance In Editor",   EPropertyType::Bool,  &bAutoAdvanceInEditor });

	OutProps.push_back({ "Sun Max Intensity",   EPropertyType::Float, &SunMaxIntensity,       0.0f,  10.0f,   0.05f });
	OutProps.push_back({ "Ambient Max Int.",    EPropertyType::Float, &AmbientMaxIntensity,   0.0f,   5.0f,   0.01f });
	OutProps.push_back({ "Ambient Night Int.",  EPropertyType::Float, &AmbientNightIntensity, 0.0f,   2.0f,   0.01f });
	OutProps.push_back({ "Override Sky Color",  EPropertyType::Bool,  &bOverrideSkyColor });
	OutProps.push_back({ "Show Orbit Debug",    EPropertyType::Bool,  &bShowOrbitDebug });
}

void UDaylightComponent::PostEditProperty(const char* PropertyName)
{
	USceneComponent::PostEditProperty(PropertyName);
	// Tick 사이에 라이트가 삭제됐을 수 있으니 UpdateLighting 이전에 캐시 재구성
	ControlledSuns.clear();
	ControlledAmbients.clear();
	FindLightActors();
	UpdateLighting();
}

void UDaylightComponent::Serialize(FArchive& Ar)
{
	USceneComponent::Serialize(Ar);
	Ar << bDaylightEnabled;
	Ar << TimeOfDay;
	Ar << TimeScale;
	Ar << bAutoAdvanceTime;
	Ar << bAutoAdvanceInEditor;
	// SunriseHour, SunsetHour 직렬화 제거됨
	Ar << SunMaxIntensity;
	Ar << AmbientMaxIntensity;
	Ar << AmbientNightIntensity;
	Ar << bOverrideSkyColor;
	Ar << bShowOrbitDebug;
}

UBillboardComponent* UDaylightComponent::EnsureEditorBillboard()
{
	if (!Owner)
		return nullptr;

	const char* IconMaterialPath = "Asset/Materials/Icon_DayLight.json";

	for (USceneComponent* Child : GetChildren())
	{
		UBillboardComponent* Billboard = Cast<UBillboardComponent>(Child);
		if (Billboard && Billboard->IsEditorOnlyComponent())
		{
			Billboard->SetHiddenInComponentTree(true);
			return Billboard;
		}
	}

	UBillboardComponent* Billboard = Owner->AddComponent<UBillboardComponent>();
	if (!Billboard)
		return nullptr;

	Billboard->AttachToComponent(this);
	Billboard->SetAbsoluteScale(true);
	Billboard->SetEditorOnlyComponent(true);
	Billboard->SetHiddenInComponentTree(true);
	Billboard->SetMaterial(FMaterialManager::Get().GetOrCreateMaterial(IconMaterialPath));
	return Billboard;
}