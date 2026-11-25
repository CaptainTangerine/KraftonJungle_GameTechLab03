#include "LightActor.h"
#include "Component/BillboardComponent.h"

IMPLEMENT_CLASS(AAmbientLightActor, AActor)
void AAmbientLightActor::InitDefaultComponents()
{
	LightComponent = AddComponent<UAmbientLightComponent>();
	SetRootComponent(LightComponent);
	BillboardComponent = LightComponent->EnsureEditorBillboard();
}

IMPLEMENT_CLASS(ADirectionalLightActor, AActor)
void ADirectionalLightActor::InitDefaultComponents()
{
	LightComponent = AddComponent<UDirectionalLightComponent>();
	SetRootComponent(LightComponent);
	BillboardComponent = LightComponent->EnsureEditorBillboard();
}

IMPLEMENT_CLASS(APointLightActor, AActor)
void APointLightActor::InitDefaultComponents()
{
	LightComponent = AddComponent<UPointLightComponent>();
	SetRootComponent(LightComponent);
	BillboardComponent = LightComponent->EnsureEditorBillboard();
}

IMPLEMENT_CLASS(ASpotLightActor, AActor)
void ASpotLightActor::InitDefaultComponents()
{
	LightComponent = AddComponent<USpotLightComponent>();
	SetRootComponent(LightComponent);
	BillboardComponent = LightComponent->EnsureEditorBillboard();
}

