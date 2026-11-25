#pragma once
#include "GameFramework/AActor.h"

class UDaylightComponent;
class UBillboardComponent;

// 씬에 하나 배치하면 DirectionalLight + AmbientLight를 자동으로 제어해
// 시간대별 태양 방향/색상/강도를 실시간으로 바꿔준다.
class ADaylightActor : public AActor
{
public:
    DECLARE_CLASS(ADaylightActor, AActor)

    ADaylightActor() {}
    void InitDefaultComponents();

    UDaylightComponent* GetDaylightComponent() const { return DaylightComponent; }

private:
    UDaylightComponent* DaylightComponent = nullptr;
	UBillboardComponent* BillboardComponent = nullptr;
};
