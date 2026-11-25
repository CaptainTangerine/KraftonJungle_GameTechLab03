#include "DaylightActor.h"
#include "Component/DaylightComponent.h"

IMPLEMENT_CLASS(ADaylightActor, AActor)

void ADaylightActor::InitDefaultComponents()
{
    // 에디터에서도 매 프레임 틱되도록 설정
    bTickInEditor = true;
    bNeedsTick    = true;

    DaylightComponent = AddComponent<UDaylightComponent>();
    SetRootComponent(DaylightComponent);
	BillboardComponent = DaylightComponent->EnsureEditorBillboard();
}
