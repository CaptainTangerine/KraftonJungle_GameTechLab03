#pragma once
#include "GameFramework/AActor.h"
#include "Component/LightComponent.h"

class UBillboardComponent;


class AAmbientLightActor : public AActor
{
public:
	DECLARE_CLASS(AAmbientLightActor, AActor)
	AAmbientLightActor() {}
	void InitDefaultComponents();
private:
	UAmbientLightComponent* LightComponent = nullptr;
	UBillboardComponent* BillboardComponent = nullptr;
};

class ADirectionalLightActor : public AActor
{
public:
	DECLARE_CLASS(ADirectionalLightActor, AActor)
	ADirectionalLightActor() {}
	void InitDefaultComponents();
private:
	UDirectionalLightComponent* LightComponent = nullptr;
	UBillboardComponent* BillboardComponent = nullptr;
};

class APointLightActor : public AActor
{
public:
	DECLARE_CLASS(APointLightActor, AActor)
	APointLightActor() {}
	void InitDefaultComponents();
private:
	UPointLightComponent* LightComponent = nullptr;
	UBillboardComponent* BillboardComponent = nullptr;
};

class ASpotLightActor : public AActor
{
public:
	DECLARE_CLASS(ASpotLightActor, AActor)
	ASpotLightActor() {}
	void InitDefaultComponents();
private:
	USpotLightComponent* LightComponent = nullptr;
	UBillboardComponent* BillboardComponent = nullptr;
};
