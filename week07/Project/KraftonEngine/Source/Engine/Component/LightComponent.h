#pragma once
#include "SceneComponent.h"
#include "Engine/Core/EngineTypes.h"

enum class ELightType : uint8
{
	Ambient,
	Directional,
	Point,
	Spot
};

class FLightSceneProxy;

struct FLightParams
{
	ELightType LightType = ELightType::Ambient;
	float Intensity = 1.0f;
	bool bVisible = true;
	FVector4 LightColor = FVector4(1.f, 1.f, 1.f, 1.f);
	FVector Position = FVector(0.f, 0.f, 0.f);
	FVector Direction = FVector(1.f, 0.f, 0.f);
	
	// Point & Spot Light Params
	float AttenuationRadius = 1000.0f;
	float LightFalloffExponent = 8.0f;
	
	// Spot Light Params
	float InnerConeAngle = 0.0f;
	float OuterConeAngle = 44.0f;
};

class ULightComponentBase : public USceneComponent
{
public:

	DECLARE_CLASS(ULightComponentBase, USceneComponent)

	ULightComponentBase();
	void CreateRenderState() override;
	void DestroyRenderState() override;

	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	void PostEditProperty(const char* PropertyName) override;

	void Serialize(FArchive& Ar) override;

	void OnTransformDirty() override;

	virtual ELightType GetLightType() const { return ELightType::Ambient; }
	virtual class FLightSceneProxy* CreateSceneProxy() const;

	float GetIntensity() const { return Intensity; }
	float GetVisible() const { return bVisible; }
	FVector4 GetColor() const { return LightColor; }

	void SetIntensity(float NewIntensity);
	void SetVisible(bool NewbVisible);
	void SetColor(const FVector4& NewColor);

	void MarkRenderStateDirty();
	class UBillboardComponent* EnsureEditorBillboard();

	// Daylight 시스템이 이 라이트를 제어할지 (Unreal의 bAtmosphereSunLight와 같은 opt-in 패턴).
	// false면 Daylight가 덮어쓰지 않아 아티스트가 독립적인 톤/색을 유지할 수 있음.
	bool bAffectedByDaylight = true;

protected:
	void PushToScene();

	class FLightSceneProxy* SceneProxy = nullptr;

	float Intensity = 1.0f;
	bool bVisible = true;
	FVector4 LightColor = FVector4(1.f, 1.f, 1.f, 1.f);
};
class ULightComponent : public ULightComponentBase
{
public:
	DECLARE_CLASS(ULightComponent, ULightComponentBase)
};
class UAmbientLightComponent : public ULightComponent
{
public:
	DECLARE_CLASS(UAmbientLightComponent, ULightComponent)
	ELightType GetLightType() const override { return ELightType::Ambient; }
	class FLightSceneProxy* CreateSceneProxy() const override;
};
class UDirectionalLightComponent : public ULightComponent
{
public:
	DECLARE_CLASS(UDirectionalLightComponent, ULightComponent)
	ELightType GetLightType() const override { return ELightType::Directional; }
	class FLightSceneProxy* CreateSceneProxy() const override;

	void ContributeSelectedVisuals(class FScene& Scene) const override;
	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	void Serialize(FArchive& Ar) override;

	// true 이면 선택 여부에 관계없이 (Release 포함) 방향 화살표 시각화
	bool bAlwaysShowRange = false;
};
class UPointLightComponent : public ULightComponent
{
public:
	DECLARE_CLASS(UPointLightComponent, ULightComponent)
	ELightType GetLightType() const override { return ELightType::Point; }
	class FLightSceneProxy* CreateSceneProxy() const override;

	void ContributeSelectedVisuals(class FScene& Scene) const override;
	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	void Serialize(FArchive& Ar) override;

	float AttenuationRadius = 10.0f;
	float LightFalloffExponent = 2.0f;

	// true 이면 선택 여부에 관계없이 (Release 포함) 라이트 범위 시각화
	bool bAlwaysShowRange = false;
};
class USpotLightComponent : public UPointLightComponent
{
public:
	DECLARE_CLASS(USpotLightComponent, UPointLightComponent)
	ELightType GetLightType() const override { return ELightType::Spot; }
	class FLightSceneProxy* CreateSceneProxy() const override;

	void ContributeSelectedVisuals(class FScene& Scene) const override;
	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	void PostEditProperty(const char* PropertyName) override;
	void Serialize(FArchive& Ar) override;

	float InnerConeAngle = 0.0f;
	float OuterConeAngle = 44.0f;
};