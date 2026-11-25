#include "SubUVComponent.h"
#include "Object/ObjectFactory.h"

#include <cstring>
#include "Render/Resource/MeshBufferManager.h"
#include "Resource/ResourceManager.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Component/CameraComponent.h"
#include "Render/Proxy/SubUVSceneProxy.h"
#include "Serialization/Archive.h"

#include <algorithm>

IMPLEMENT_CLASS(USubUVComponent, UBillboardComponent)

FPrimitiveSceneProxy* USubUVComponent::CreateSceneProxy()
{
	return new FSubUVSceneProxy(this);
}

void USubUVComponent::Serialize(FArchive& Ar)
{
	// UBillboardComponent::Serialize가 TextureName/Width/Height를 처리한다.
	// SubUV는 자체 ParticleResource를 쓰므로 Billboard의 TextureName은 사용하지 않지만,
	// 직렬화 일관성을 위해 Super 호출은 유지한다 (Width/Height는 공통).
	UBillboardComponent::Serialize(Ar);

	Ar << ParticleName;
	Ar << FrameIndex;
	Ar << Columns;
	Ar << Rows;
	Ar << StartFrameIndex;
	Ar << EndFrameIndex;
	Ar << PlayRate;
	Ar << bLoop;

	ClampFrameSettings();
	SyncFrameDisplayFromInternal();
}

void USubUVComponent::PostDuplicate()
{
	UBillboardComponent::PostDuplicate();
	// 파티클 리소스 재바인딩
	SetParticle(ParticleName);
}

USubUVComponent::USubUVComponent()
{
	SetVisibility(false);
}

void USubUVComponent::SetParticle(const FName& InParticleName)
{
	ParticleName = InParticleName;
	CachedParticle = FResourceManager::Get().FindParticle(InParticleName);
	ClampFrameSettings();
}

void USubUVComponent::SetFrameIndex(uint32 InIndex)
{
	ClampFrameSettings();
	FrameIndex = std::clamp(InIndex, StartFrameIndex, EndFrameIndex);
}

uint32 USubUVComponent::GetTotalFrameCount() const
{
	const int32 ClampedColumns = std::max(Columns, 1);
	const int32 ClampedRows = std::max(Rows, 1);
	return static_cast<uint32>(ClampedColumns * ClampedRows);
}

void USubUVComponent::SetStartFrameDisplay(int32 InDisplayFrame)
{
	ClampFrameSettings();
	const uint32 TotalFrames = GetTotalFrameCount();
	const int32 MaxDisplayFrame = static_cast<int32>(TotalFrames);
	StartFrameDisplay = std::clamp(InDisplayFrame, 1, MaxDisplayFrame);
	StartFrameIndex = static_cast<uint32>(StartFrameDisplay - 1);
	if (StartFrameIndex > EndFrameIndex)
	{
		EndFrameIndex = StartFrameIndex;
	}
	ClampFrameSettings();
	SyncFrameDisplayFromInternal();
}

void USubUVComponent::SetEndFrameDisplay(int32 InDisplayFrame)
{
	ClampFrameSettings();
	const uint32 TotalFrames = GetTotalFrameCount();
	const int32 MaxDisplayFrame = static_cast<int32>(TotalFrames);
	EndFrameDisplay = std::clamp(InDisplayFrame, 1, MaxDisplayFrame);
	EndFrameIndex = static_cast<uint32>(EndFrameDisplay - 1);
	if (EndFrameIndex < StartFrameIndex)
	{
		StartFrameIndex = EndFrameIndex;
	}
	ClampFrameSettings();
	SyncFrameDisplayFromInternal();
}

void USubUVComponent::Play()
{
	ClampFrameSettings();
	FrameIndex = StartFrameIndex;
	TimeAccumulator = 0.0f;
	bIsExecute = false;
}

void USubUVComponent::SetFrameRate(float InFPS)
{
	PlayRate = std::max(InFPS, 1.0f);
}

void USubUVComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
	ClampFrameSettings();
	SyncFrameDisplayFromInternal();

	// Billboard의 Texture 프로퍼티는 SubUV에서 의미가 없으므로 의도적으로 스킵.
	// UPrimitiveComponent로 직접 올라가 공통 트랜스폼 등만 가져온 뒤,
	// Width/Height(상속 멤버)와 SubUV 고유 프로퍼티만 노출한다.
	UPrimitiveComponent::GetEditableProperties(OutProps);
	OutProps.push_back({ "Particle",  EPropertyType::Name,  &ParticleName });
	OutProps.push_back({ "Width",     EPropertyType::Float, &Width,  0.1f, 100.0f, 0.1f });
	OutProps.push_back({ "Height",    EPropertyType::Float, &Height, 0.1f, 100.0f, 0.1f });
	OutProps.push_back({ "Columns",   EPropertyType::Int,   &Columns });
	OutProps.push_back({ "Rows",      EPropertyType::Int,   &Rows });
	OutProps.push_back({ "Start Frame", EPropertyType::Int, &StartFrameDisplay });
	OutProps.push_back({ "End Frame", EPropertyType::Int,   &EndFrameDisplay });
	OutProps.push_back({ "Play Rate", EPropertyType::Float, &PlayRate, 1.0f, 120.0f, 1.0f });
	OutProps.push_back({ "bLoop",     EPropertyType::Bool,  &bLoop });
}

void USubUVComponent::PostEditProperty(const char* PropertyName)
{
	// SubUV는 GetEditableProperties에서 Billboard의 Texture를 의도적으로 스킵하므로
	// PostEditProperty도 Billboard를 거치지 않고 Primitive로 직접 올라간다.
	UPrimitiveComponent::PostEditProperty(PropertyName);

	if (strcmp(PropertyName, "Particle") == 0)
	{
		SetParticle(ParticleName);
		// 파티클 교체 시 UV 그리드/텍스처가 바뀌므로 Mesh 단계까지 dirty.
		MarkProxyDirty(EDirtyFlag::Mesh);
	}
	else if (strcmp(PropertyName, "Columns") == 0 || strcmp(PropertyName, "Rows") == 0)
	{
		ClampFrameSettings();
		SyncFrameDisplayFromInternal();
		MarkProxyDirty(EDirtyFlag::Mesh);
	}
	else if (strcmp(PropertyName, "Start Frame") == 0)
	{
		SetStartFrameDisplay(StartFrameDisplay);
		MarkProxyDirty(EDirtyFlag::Mesh);
	}
	else if (strcmp(PropertyName, "End Frame") == 0)
	{
		SetEndFrameDisplay(EndFrameDisplay);
		MarkProxyDirty(EDirtyFlag::Mesh);
	}
	else if (strcmp(PropertyName, "Width") == 0 || strcmp(PropertyName, "Height") == 0)
	{
		MarkProxyDirty(EDirtyFlag::Transform);
		MarkWorldBoundsDirty();
	}
}

void USubUVComponent::UpdateWorldAABB() const
{
	FVector LExt = { 0.01f, 0.5f, 0.5f };

	float NewEx = std::abs(CachedWorldMatrix.M[0][0]) * LExt.X +
		std::abs(CachedWorldMatrix.M[1][0]) * LExt.Y +
		std::abs(CachedWorldMatrix.M[2][0]) * LExt.Z;

	float NewEy = std::abs(CachedWorldMatrix.M[0][1]) * LExt.X +
		std::abs(CachedWorldMatrix.M[1][1]) * LExt.Y +
		std::abs(CachedWorldMatrix.M[2][1]) * LExt.Z;

	float NewEz = std::abs(CachedWorldMatrix.M[0][2]) * LExt.X +
		std::abs(CachedWorldMatrix.M[1][2]) * LExt.Y +
		std::abs(CachedWorldMatrix.M[2][2]) * LExt.Z;

	FVector WorldCenter = GetWorldLocation();

	WorldAABBMinLocation = WorldCenter - FVector(NewEx, NewEy, NewEz);
	WorldAABBMaxLocation = WorldCenter + FVector(NewEx, NewEy, NewEz);
}

void USubUVComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	UBillboardComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bLoop && bIsExecute) return; // 단발 재생 완료 후 정지

	ClampFrameSettings();
	if (GetTotalFrameCount() == 0) return;

	PlayRate = std::max(PlayRate, 1.0f);
	TimeAccumulator += DeltaTime;
	const float FrameDuration = 1.0f / PlayRate;
	while (TimeAccumulator >= FrameDuration)
	{
		TimeAccumulator -= FrameDuration;

		if (bLoop)
		{
			bIsExecute = false;
			const uint32 Range = EndFrameIndex - StartFrameIndex + 1;
			FrameIndex = StartFrameIndex + ((FrameIndex - StartFrameIndex + 1) % Range);
		}
		else
		{
			if (FrameIndex < EndFrameIndex)
			{
				FrameIndex++;
			}
			else
			{
				bIsExecute = true;    // 마지막 프레임 도달 → 완료
				TimeAccumulator = 0.0f;
				break;
			}
		}
	}
}

void USubUVComponent::ClampFrameSettings()
{
	Columns = std::max(Columns, 1);
	Rows = std::max(Rows, 1);

	const uint32 TotalFrames = GetTotalFrameCount();
	const uint32 LastFrameIndex = TotalFrames > 0 ? TotalFrames - 1 : 0;

	StartFrameIndex = std::min(StartFrameIndex, LastFrameIndex);
	EndFrameIndex = std::min(EndFrameIndex, LastFrameIndex);
	if (StartFrameIndex > EndFrameIndex)
	{
		EndFrameIndex = StartFrameIndex;
	}

	FrameIndex = std::clamp(FrameIndex, StartFrameIndex, EndFrameIndex);
}

void USubUVComponent::SyncFrameDisplayFromInternal()
{
	StartFrameDisplay = static_cast<int32>(StartFrameIndex + 1);
	EndFrameDisplay = static_cast<int32>(EndFrameIndex + 1);
}

