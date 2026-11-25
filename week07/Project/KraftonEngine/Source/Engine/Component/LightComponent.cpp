#include "LightComponent.h"
#include "GameFramework/AActor.h"
#include "Render/Proxy/FScene.h"
#include "GameFramework/World.h"
#include "Serialization/Archive.h"
#include "Render/Proxy/LightSceneProxy.h"
#include "Component/BillboardComponent.h"
#include "Materials/MaterialManager.h"
#include "Core/EngineTypes.h"
#include "Math/MathUtils.h"
#include <cmath>
#include <cstring>

namespace
{
	const FColor LightDebugColor(255, 220, 80);
	const FColor SpotInnerDebugColor(255, 240, 140);
	const FColor SpotOuterDebugColor(255, 160, 40);

	void AddDebugCircle(FScene& Scene, const FVector& Center, const FVector& AxisU, const FVector& AxisV, float Radius, int32 Segments, const FColor& Color)
	{
		if (Segments < 4) Segments = 4;
		FVector Prev = Center + AxisU * Radius;
		for (int32 i = 1; i <= Segments; ++i)
		{
			const float T = (static_cast<float>(i) / Segments) * 2.0f * M_PI;
			const FVector Next = Center + AxisU * (Radius * std::cos(T)) + AxisV * (Radius * std::sin(T));
			Scene.AddDebugLine(Prev, Next, Color);
			Prev = Next;
		}
	}

	void AddDebugSphereWire(FScene& Scene, const FVector& Center, float Radius, const FColor& Color)
	{
		constexpr int32 Segments = 24;
		const FVector X(1, 0, 0);
		const FVector Y(0, 1, 0);
		const FVector Z(0, 0, 1);
		AddDebugCircle(Scene, Center, X, Y, Radius, Segments, Color);
		AddDebugCircle(Scene, Center, X, Z, Radius, Segments, Color);
		AddDebugCircle(Scene, Center, Y, Z, Radius, Segments, Color);
	}

	void AddDebugCone(FScene& Scene, const FVector& Apex, const FVector& Direction, float HalfAngleRad, float Length, const FColor& Color)
	{
		if (Length <= 0.0f || HalfAngleRad <= 0.0f) return;

		FVector Forward = Direction.Normalized();
		FVector Ref = std::abs(Forward.Dot(FVector(0, 0, 1))) > 0.98f ? FVector(0, 1, 0) : FVector(0, 0, 1);
		FVector Right = Forward.Cross(Ref).Normalized();
		FVector Up = Forward.Cross(Right).Normalized();

		// Inner/Outer 두 콘의 cap 평면이 같은 깊이에 놓이도록 축 길이를 AttenuationRadius로 고정.
		// (슬랜트 기준으로 그리면 각도가 클수록 axial depth가 짧아져서 inner/outer 끝이 안 맞음)
		// base radius = Length * tan(angle). 극단적으로 큰 각(>=~89°)에서는 tan 발산을 막기 위해 클램프.
		const float SafeHalfAngle = std::min(HalfAngleRad, 1.5533f); // ~89°
		const float CapDistance = Length;
		const float CapRadius = Length * std::tan(SafeHalfAngle);
		const FVector CapCenter = Apex + Forward * CapDistance;

		constexpr int32 Segments = 24;
		constexpr int32 Spokes = 8;

		AddDebugCircle(Scene, CapCenter, Right, Up, CapRadius, Segments, Color);

		for (int32 i = 0; i < Spokes; ++i)
		{
			const float T = (static_cast<float>(i) / Spokes) * 2.0f * M_PI;
			const FVector Edge = CapCenter + Right * (CapRadius * std::cos(T)) + Up * (CapRadius * std::sin(T));
			Scene.AddDebugLine(Apex, Edge, Color);
		}
	}

	void AddDebugArrow(FScene& Scene, const FVector& Start, const FVector& Direction, float Length, const FColor& Color)
	{
		const FVector Dir = Direction.Normalized();
		const FVector End = Start + Dir * Length;
		Scene.AddDebugLine(Start, End, Color);

		FVector Ref = std::abs(Dir.Dot(FVector(0, 0, 1))) > 0.98f ? FVector(0, 1, 0) : FVector(0, 0, 1);
		FVector Right = Dir.Cross(Ref).Normalized();
		FVector Up = Dir.Cross(Right).Normalized();

		const float HeadLen = Length * 0.2f;
		const float HeadRad = Length * 0.08f;
		const FVector Back = End - Dir * HeadLen;

		Scene.AddDebugLine(End, Back + Right * HeadRad, Color);
		Scene.AddDebugLine(End, Back - Right * HeadRad, Color);
		Scene.AddDebugLine(End, Back + Up * HeadRad, Color);
		Scene.AddDebugLine(End, Back - Up * HeadRad, Color);
	}
}

IMPLEMENT_ABSTRACT_CLASS(ULightComponentBase, USceneComponent) // 에디터에 보이는 걸 막음
IMPLEMENT_ABSTRACT_CLASS(ULightComponent, ULightComponentBase) // 에디터에 보이는 걸 막음
IMPLEMENT_CLASS(UAmbientLightComponent, ULightComponent)
IMPLEMENT_CLASS(UDirectionalLightComponent, ULightComponent)
IMPLEMENT_CLASS(UPointLightComponent, ULightComponent)
IMPLEMENT_CLASS(USpotLightComponent, UPointLightComponent)

ULightComponentBase::ULightComponentBase()
{
	SetComponentTickEnabled(false);
}

void ULightComponentBase::CreateRenderState()
{
	if (!SceneProxy)
	{
		SceneProxy = CreateSceneProxy();
	}
	PushToScene();
}

void ULightComponentBase::DestroyRenderState()
{
	if (!Owner) return;
	UWorld* World = Owner->GetWorld();
	if (!World) return;

	if (SceneProxy)
	{
		World->GetScene().RemoveLight(SceneProxy);
		delete SceneProxy;
		SceneProxy = nullptr;
	}
}

void ULightComponentBase::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
	USceneComponent::GetEditableProperties(OutProps);

	OutProps.push_back({ "Light Intensity",	EPropertyType::Float,  &Intensity});
	OutProps.push_back({ "Light Enabled",    EPropertyType::Bool ,  &bVisible});
	OutProps.push_back({ "Light Color",		EPropertyType::Color4, &LightColor });
	OutProps.push_back({ "Affected By Daylight", EPropertyType::Bool, &bAffectedByDaylight });
}

void ULightComponentBase::PostEditProperty(const char* PropertyName)
{
	USceneComponent::PostEditProperty(PropertyName);
	MarkRenderStateDirty();
}

void ULightComponentBase::Serialize(FArchive& Ar)
{
	USceneComponent::Serialize(Ar);
	
	Ar << Intensity;
	Ar << bVisible;
	Ar << LightColor;
	Ar << bAffectedByDaylight;
}

FLightSceneProxy* ULightComponentBase::CreateSceneProxy() const
{
	return new FLightSceneProxy(this);
}

FLightSceneProxy* UAmbientLightComponent::CreateSceneProxy() const
{
	return new FLightSceneProxy(this);
}

FLightSceneProxy* UDirectionalLightComponent::CreateSceneProxy() const
{
	return new FDirectionalLightSceneProxy(this);
}

FLightSceneProxy* UPointLightComponent::CreateSceneProxy() const
{
	return new FPointLightSceneProxy(this);
}

FLightSceneProxy* USpotLightComponent::CreateSceneProxy() const
{
	return new FSpotLightSceneProxy(this);
}

/**
 * 광원 관련 컴포넌트들에 icon을 붙여줍니다.
 * 
 * \return 
 */
UBillboardComponent* ULightComponentBase::EnsureEditorBillboard()
{
	if (!Owner)
	{
		return nullptr;
	}
	const char* IconMaterialPath = nullptr;
	switch (GetLightType())
	{
	case ELightType::Ambient:
		IconMaterialPath = "Asset/Materials/Icon_AmbientLight.json";
		break;
	case ELightType::Directional:
		IconMaterialPath = "Asset/Materials/Icon_DirectionalLight.json";
		break;
	case ELightType::Point:
		IconMaterialPath = "Asset/Materials/Icon_PointLight.json";
		break;
	case ELightType::Spot:
		IconMaterialPath = "Asset/Materials/Icon_SpotLight.json";
		break;
	default:
		return nullptr;
	}

	//이미 에디터에서 자동생성된 빌보드가 있는지 확인.
	for (USceneComponent* Child : GetChildren())
	{
		UBillboardComponent* Billboard = Cast<UBillboardComponent>(Child);
		if (Billboard && Billboard->IsEditorOnlyComponent())
		{
			Billboard->SetHiddenInComponentTree(true);
			return Billboard;
		}
	}

	//없다면 만들어서 부착
	UBillboardComponent* Billboard = Owner->AddComponent<UBillboardComponent>();
	if (!Billboard)
	{
		return nullptr;
	}

	//해당 빌보드를 자식으로 설정하고
	Billboard->AttachToComponent(this);
	// 부모의 Scale 영향을 받지 않도록 설정
	Billboard->SetAbsoluteScale(true);
	//에디터에서만 보이도록 PIE에서는 없애고
	Billboard->SetEditorOnlyComponent(true);
	//컴포넌트 트리에서는 안 보이도록 숨깁니다.
	Billboard->SetHiddenInComponentTree(true);

	Billboard->SetMaterial(FMaterialManager::Get().GetOrCreateMaterial(IconMaterialPath));
	Billboard->SetTintColor(LightColor);
	return Billboard;
}

void ULightComponentBase::PushToScene()
{
	if (!Owner || !SceneProxy) return;
	UWorld* World = Owner->GetWorld();
	if (!World) return;

	if (SceneProxy->LightSceneId == -1)
	{
		World->GetScene().AddLight(SceneProxy);
	}
	else
	{
		SceneProxy->UpdateProperties(this);
		World->GetScene().UpdateLight(SceneProxy);
	}
}

void ULightComponentBase::OnTransformDirty()
{
	USceneComponent::OnTransformDirty();
	if (SceneProxy)
	{
		SceneProxy->UpdateTransform(this);
		if (Owner && Owner->GetWorld())
		{
			Owner->GetWorld()->GetScene().UpdateLight(SceneProxy);
		}
	}
}

void ULightComponentBase::MarkRenderStateDirty()
{
	if (Owner)
	{
		for (auto* Comp : Owner->GetComponents())
		{
			if (Comp->IsA<UBillboardComponent>())
			{
				UBillboardComponent* BillboardComp = static_cast<UBillboardComponent*>(Comp);
				if (BillboardComp->IsEditorOnlyComponent() && BillboardComp->GetParent() == this)
				{
					BillboardComp->SetTintColor(LightColor);
				}
			}
		}
	}

	if (SceneProxy)
	{
		SceneProxy->UpdateProperties(this);
		if (Owner && Owner->GetWorld())
		{
			Owner->GetWorld()->GetScene().UpdateLight(SceneProxy);
		}
	}
}

void ULightComponentBase::SetIntensity(float NewIntensity)
{
	Intensity = NewIntensity;
	MarkRenderStateDirty();
}

void ULightComponentBase::SetVisible(bool NewbVisible)
{
	bVisible = NewbVisible;
	MarkRenderStateDirty();
}

void ULightComponentBase::SetColor(const FVector4& NewColor)
{
	LightColor = NewColor;
	MarkRenderStateDirty();
}

void UPointLightComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
	ULightComponentBase::GetEditableProperties(OutProps);
	OutProps.push_back({ "Attenuation Radius",     EPropertyType::Float, &AttenuationRadius,    0.0f, 100000.0f, 1.0f  });
	OutProps.push_back({ "Light Falloff Exponent", EPropertyType::Float, &LightFalloffExponent, 0.0f,     16.0f, 0.05f });
	OutProps.push_back({ "Always Show Range",       EPropertyType::Bool,  &bAlwaysShowRange });
}

void UPointLightComponent::Serialize(FArchive& Ar)
{
	ULightComponentBase::Serialize(Ar);
	Ar << AttenuationRadius;
	Ar << LightFalloffExponent;
	Ar << bAlwaysShowRange;
}

void USpotLightComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
	UPointLightComponent::GetEditableProperties(OutProps);
	OutProps.push_back({ "Inner Cone Angle", EPropertyType::Float, &InnerConeAngle, 0.0f, 89.0f, 0.1f });
	OutProps.push_back({ "Outer Cone Angle", EPropertyType::Float, &OuterConeAngle, 0.0f, 89.0f, 0.1f });
	// bAlwaysShowRange is already added by UPointLightComponent::GetEditableProperties
}

void USpotLightComponent::PostEditProperty(const char* PropertyName)
{
	if (std::strcmp(PropertyName, "Inner Cone Angle") == 0 && InnerConeAngle > OuterConeAngle)
	{
		InnerConeAngle = OuterConeAngle;
	}
	else if (std::strcmp(PropertyName, "Outer Cone Angle") == 0 && OuterConeAngle < InnerConeAngle)
	{
		OuterConeAngle = InnerConeAngle;
	}
	ULightComponentBase::PostEditProperty(PropertyName);
}

void USpotLightComponent::Serialize(FArchive& Ar)
{
	UPointLightComponent::Serialize(Ar);
	Ar << InnerConeAngle;
	Ar << OuterConeAngle;
}

void UDirectionalLightComponent::ContributeSelectedVisuals(FScene& Scene) const
{
	const FVector Origin = GetWorldLocation();
	const FVector Forward = GetForwardVector();
	AddDebugArrow(Scene, Origin, Forward, 2.0f, LightDebugColor);
}

void UDirectionalLightComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
	ULightComponentBase::GetEditableProperties(OutProps);
	OutProps.push_back({ "Always Show Arrow", EPropertyType::Bool, &bAlwaysShowRange });
}

void UDirectionalLightComponent::Serialize(FArchive& Ar)
{
	ULightComponentBase::Serialize(Ar);
	Ar << bAlwaysShowRange;
}

void UPointLightComponent::ContributeSelectedVisuals(FScene& Scene) const
{
	const FVector Origin = GetWorldLocation();
	AddDebugSphereWire(Scene, Origin, AttenuationRadius, LightDebugColor);

	if (LightFalloffExponent > 0.0f)
	{
		const float HalfFalloffRatio = 1.0f - std::pow(0.5f, 1.0f / LightFalloffExponent);
		const float HalfRadius = AttenuationRadius * HalfFalloffRatio;
		if (HalfRadius > 0.0f)
		{
			AddDebugSphereWire(Scene, Origin, HalfRadius, SpotInnerDebugColor);
		}
	}
}

void USpotLightComponent::ContributeSelectedVisuals(FScene& Scene) const
{
	const FVector Apex = GetWorldLocation();
	const FVector Forward = GetForwardVector();
	const float Length = AttenuationRadius;

	AddDebugCone(Scene, Apex, Forward, OuterConeAngle * DEG_TO_RAD, Length, SpotOuterDebugColor);
	if (InnerConeAngle > 0.0f && InnerConeAngle < OuterConeAngle)
	{
		AddDebugCone(Scene, Apex, Forward, InnerConeAngle * DEG_TO_RAD, Length, SpotInnerDebugColor);
	}
}
