#pragma once

#include "Engine/Core/EngineTypes.h"
#include "Component/LightComponent.h"
enum class ELightType : uint8;
struct FLightParams;

class ULightComponentBase;
class UDirectionalLightComponent;
class UPointLightComponent;
class USpotLightComponent;

// Base class for Light proxies
class FLightSceneProxy
{
public:
	FLightSceneProxy(const class ULightComponentBase* InComponent);
	virtual ~FLightSceneProxy() = default;

	virtual void UpdateTransform(const class ULightComponentBase* InComponent) {}
	virtual void UpdateProperties(const class ULightComponentBase* InComponent);

	virtual void GetLightParameters(FLightParams& OutParams) const;

	int32 LightSceneId = -1;

	ELightType LightType = ELightType::Ambient;
	float Intensity = 1.0f;
	bool bVisible = true;
	FVector4 LightColor;
};

class FDirectionalLightSceneProxy : public FLightSceneProxy
{
public:
	FDirectionalLightSceneProxy(const class UDirectionalLightComponent* InComponent);

	void UpdateTransform(const class ULightComponentBase* InComponent) override;
	void GetLightParameters(FLightParams& OutParams) const override;

	FVector Direction;
};

class FLocalLightSceneProxy : public FLightSceneProxy
{
public:
	FLocalLightSceneProxy(const class UPointLightComponent* InComponent);

	void UpdateTransform(const class ULightComponentBase* InComponent) override;
	void UpdateProperties(const class ULightComponentBase* InComponent) override;
	void GetLightParameters(FLightParams& OutParams) const override;

	FVector Position;
	float AttenuationRadius;
	float LightFalloffExponent;
};

class FPointLightSceneProxy : public FLocalLightSceneProxy
{
public:
	FPointLightSceneProxy(const class UPointLightComponent* InComponent);
};

class FSpotLightSceneProxy : public FPointLightSceneProxy
{
public:
	FSpotLightSceneProxy(const class USpotLightComponent* InComponent);

	void UpdateTransform(const class ULightComponentBase* InComponent) override;
	void UpdateProperties(const class ULightComponentBase* InComponent) override;
	void GetLightParameters(FLightParams& OutParams) const override;

	FVector Direction;
	float InnerConeAngle;
	float OuterConeAngle;
};
