#include "LightSceneProxy.h"

FLightSceneProxy::FLightSceneProxy(const ULightComponentBase* InComponent)
{
	if (InComponent)
	{
		LightType = InComponent->GetLightType();
		Intensity = InComponent->GetIntensity();
		bVisible = InComponent->GetVisible();
		LightColor = InComponent->GetColor();
	}
}

void FLightSceneProxy::UpdateProperties(const ULightComponentBase* InComponent)
{
	if (InComponent)
	{
		Intensity = InComponent->GetIntensity();
		bVisible = InComponent->GetVisible();
		LightColor = InComponent->GetColor();
	}
}

void FLightSceneProxy::GetLightParameters(FLightParams& OutParams) const
{
	OutParams.LightType = LightType;
	OutParams.Intensity = Intensity;
	OutParams.bVisible = bVisible;
	OutParams.LightColor = LightColor;
}

// ----------------------------------------------------

FDirectionalLightSceneProxy::FDirectionalLightSceneProxy(const UDirectionalLightComponent* InComponent)
	: FLightSceneProxy(InComponent)
{
	if (InComponent)
	{
		Direction = InComponent->GetForwardVector();
	}
}

void FDirectionalLightSceneProxy::UpdateTransform(const ULightComponentBase* InComponent)
{
	if (InComponent)
	{
		Direction = InComponent->GetForwardVector();
	}
}

void FDirectionalLightSceneProxy::GetLightParameters(FLightParams& OutParams) const
{
	FLightSceneProxy::GetLightParameters(OutParams);
	OutParams.Direction = Direction;
}

// ----------------------------------------------------

FLocalLightSceneProxy::FLocalLightSceneProxy(const UPointLightComponent* InComponent)
	: FLightSceneProxy(InComponent)
	, AttenuationRadius(0.0f)
	, LightFalloffExponent(0.0f)
{
	if (InComponent)
	{
		Position = InComponent->GetWorldLocation();
		AttenuationRadius = InComponent->AttenuationRadius;
		LightFalloffExponent = InComponent->LightFalloffExponent;
	}
}

void FLocalLightSceneProxy::UpdateTransform(const ULightComponentBase* InComponent)
{
	if (InComponent)
	{
		Position = InComponent->GetWorldLocation();
	}
}

void FLocalLightSceneProxy::UpdateProperties(const ULightComponentBase* InComponent)
{
	FLightSceneProxy::UpdateProperties(InComponent);
	if (const UPointLightComponent* PointComp = dynamic_cast<const UPointLightComponent*>(InComponent))
	{
		AttenuationRadius = PointComp->AttenuationRadius;
		LightFalloffExponent = PointComp->LightFalloffExponent;
	}
}

void FLocalLightSceneProxy::GetLightParameters(FLightParams& OutParams) const
{
	FLightSceneProxy::GetLightParameters(OutParams);
	OutParams.Position = Position;
	OutParams.AttenuationRadius = AttenuationRadius;
	OutParams.LightFalloffExponent = LightFalloffExponent;
}

// ----------------------------------------------------

FPointLightSceneProxy::FPointLightSceneProxy(const UPointLightComponent* InComponent)
	: FLocalLightSceneProxy(InComponent)
{
}

// ----------------------------------------------------

FSpotLightSceneProxy::FSpotLightSceneProxy(const USpotLightComponent* InComponent)
	: FPointLightSceneProxy(InComponent)
	, InnerConeAngle(0.0f)
	, OuterConeAngle(0.0f)
{
	if (InComponent)
	{
		Direction = InComponent->GetForwardVector();
		InnerConeAngle = InComponent->InnerConeAngle;
		OuterConeAngle = InComponent->OuterConeAngle;
	}
}

void FSpotLightSceneProxy::UpdateTransform(const ULightComponentBase* InComponent)
{
	FPointLightSceneProxy::UpdateTransform(InComponent);
	if (InComponent)
	{
		Direction = InComponent->GetForwardVector();
	}
}

void FSpotLightSceneProxy::UpdateProperties(const ULightComponentBase* InComponent)
{
	FPointLightSceneProxy::UpdateProperties(InComponent);
	if (const USpotLightComponent* SpotComp = dynamic_cast<const USpotLightComponent*>(InComponent))
	{
		InnerConeAngle = SpotComp->InnerConeAngle;
		OuterConeAngle = SpotComp->OuterConeAngle;
	}
}

void FSpotLightSceneProxy::GetLightParameters(FLightParams& OutParams) const
{
	FPointLightSceneProxy::GetLightParameters(OutParams);
	OutParams.Direction = Direction;
	OutParams.InnerConeAngle = InnerConeAngle;
	OutParams.OuterConeAngle = OuterConeAngle;
}
